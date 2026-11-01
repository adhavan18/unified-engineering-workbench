#include "core/hardware.hpp"
#include "test_framework.hpp"

using namespace wb::hw;

TEST(hardware, discovery_finds_one_of_each_kind) {
    DeviceRegistry reg(false);
    const auto devices = reg.discover();
    CHECK(devices.size() == 3);
    CHECK(devices[0]->info().kind == DeviceKind::microcontroller);
    CHECK(devices[1]->info().kind == DeviceKind::fpga);
    CHECK(devices[2]->info().kind == DeviceKind::jtag_adapter);
    for (auto* d : devices) {
        CHECK(d->info().model.find("simulated") != std::string::npos);  // simulation is always labelled
        CHECK(!d->capabilities().empty());
    }
    CHECK(reg.discover()[0] == devices[0]);  // rediscovery keeps state
}

TEST(hardware, jtag_adapter_is_not_a_compute_target) {
    DeviceRegistry reg(false);
    DeviceAdapter* jtag = reg.find("jtag0");
    CHECK(jtag == nullptr);  // nothing until discovery has run
    reg.discover();
    jtag = reg.find("jtag0");
    CHECK(jtag != nullptr);
    CHECK(!jtag->info().can_run_workloads);
    CHECK(reg.find("mcu0")->info().can_run_workloads);
    CHECK(jtag->info().io_voltage.find("vref") != std::string::npos);
}

TEST(hardware, connect_telemetry_disconnect_cycle) {
    auto d = make_mock_mcu();
    d->set_latency_enabled(false);
    Telemetry t;
    OpResult r = d->read_telemetry(t);
    CHECK(!r.ok && r.code == "not_connected");

    CHECK(d->connect().ok);
    CHECK(d->state() == LinkState::connected);
    CHECK(d->connect().code == "already_connected");

    r = d->read_telemetry(t);
    CHECK(r.ok);
    CHECK(t.sequence == 1);
    CHECK(t.device_id == "mcu0");
    CHECK(!t.fields.empty());

    CHECK(d->disconnect().ok);
    CHECK(d->state() == LinkState::disconnected);
}

TEST(hardware, telemetry_is_deterministic) {
    auto a = make_mock_fpga();
    auto b = make_mock_fpga();
    a->set_latency_enabled(false);
    b->set_latency_enabled(false);
    a->connect();
    b->connect();
    Telemetry ta, tb;
    a->read_telemetry(ta);
    b->read_telemetry(tb);
    CHECK(ta.fields[0].value == tb.fields[0].value);
}

TEST(hardware, timeout_once_is_recoverable) {
    auto d = make_mock_jtag();
    d->set_latency_enabled(false);
    d->set_fault(FaultMode::timeout_once);

    OpResult r = d->connect();
    CHECK(!r.ok && r.code == "timeout" && r.recoverable);
    CHECK(d->state() == LinkState::faulted);

    Telemetry t;
    r = d->read_telemetry(t);
    CHECK(!r.ok && r.code == "not_connected");
    CHECK(r.message.find("reconnect") != std::string::npos);

    r = d->connect();  // retry succeeds
    CHECK(r.ok);
    CHECK(d->state() == LinkState::connected);
    CHECK(d->read_telemetry(t).ok);
}

TEST(hardware, timeout_during_telemetry_faults_the_link) {
    auto d = make_mock_mcu();
    d->set_latency_enabled(false);
    CHECK(d->connect().ok);
    d->set_fault(FaultMode::timeout_once);
    Telemetry t;
    CHECK(d->read_telemetry(t).code == "timeout");
    CHECK(d->state() == LinkState::faulted);
    CHECK(d->connect().ok);
    CHECK(d->read_telemetry(t).ok);
}

TEST(hardware, persistent_timeout_keeps_failing) {
    auto d = make_mock_fpga();
    d->set_latency_enabled(false);
    d->set_fault(FaultMode::timeout_always);
    CHECK(d->connect().code == "timeout");
    CHECK(d->connect().code == "timeout");
    d->set_fault(FaultMode::none);
    CHECK(d->connect().ok);
}

TEST(hardware, fault_mode_parsing) {
    FaultMode m;
    CHECK(parse_fault_mode("timeout_once", m) && m == FaultMode::timeout_once);
    CHECK(!parse_fault_mode("explode", m));
}
