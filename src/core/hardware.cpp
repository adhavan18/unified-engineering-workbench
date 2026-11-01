#include "core/hardware.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace wb::hw {

namespace {

std::string fixed(double v, int decimals) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

// smooth, deterministic "sensor" signal so telemetry is reproducible
double wave(std::uint64_t seq, double period, double amplitude) {
    return amplitude * std::sin(6.283185307179586 * static_cast<double>(seq) / period);
}

class MockMcu final : public DeviceAdapter {
public:
    DeviceInfo info() const override {
        return {"mcu0", DeviceKind::microcontroller, "stm32f4-class mcu board (simulated)",
                "usb cdc-acm virtual serial, 115200 8n1", "3.3 V lvcmos", true};
    }
    std::vector<Capability> capabilities() const override {
        return {{"firmware execution", "runs its own firmware; the host sends commands over serial"},
                {"gpio", "3.3 V push-pull, not 5 V tolerant on analog pins"},
                {"adc", "12-bit, 0-3.3 V input range"},
                {"uart telemetry", "line-based messages at 115200 baud"},
                {"swd/jtag tap", "debug port; owned by the jtag adapter when attached"}};
    }

protected:
    std::vector<TelemetryField> sample(std::uint64_t seq) const override {
        return {{"die_temperature", fixed(36.5 + wave(seq, 40, 1.5), 2), "C"},
                {"vdd", fixed(3.30 + wave(seq, 25, 0.01), 3), "V"},
                {"uptime", std::to_string(seq * 2), "s"},
                {"adc0_raw", std::to_string(2048 + static_cast<int>(wave(seq, 16, 300))), "counts"}};
    }
    double nominal_latency_ms() const override { return 40.0; }
};

class MockFpga final : public DeviceAdapter {
public:
    DeviceInfo info() const override {
        return {"fpga0", DeviceKind::fpga, "artix-7-class fpga board (simulated)",
                "usb-uart bridge (ftdi-class), 921600 8n1", "bank-dependent: 3.3 V / 1.8 V", true};
    }
    std::vector<Capability> capabilities() const override {
        return {{"programmable logic", "runs a loaded bitstream; programming is not performed in this mock"},
                {"i/o banks", "each bank has its own vcco (3.3 V or 1.8 V); check before wiring"},
                {"uart telemetry", "soft uart in fabric, host-facing via the usb bridge"},
                {"jtag tap", "configuration and boundary scan through the jtag adapter"}};
    }

protected:
    std::vector<TelemetryField> sample(std::uint64_t seq) const override {
        return {{"die_temperature", fixed(48.0 + wave(seq, 60, 2.0), 2), "C"},
                {"vccint", fixed(1.000 + wave(seq, 30, 0.004), 3), "V"},
                {"vccaux", fixed(1.800 + wave(seq, 35, 0.006), 3), "V"},
                {"bitstream", "none loaded (simulated)", ""}};
    }
    double nominal_latency_ms() const override { return 60.0; }
};

class MockJtag final : public DeviceAdapter {
public:
    DeviceInfo info() const override {
        return {"jtag0", DeviceKind::jtag_adapter, "ft2232h-class jtag adapter (simulated)",
                "usb 2.0 high-speed (vendor protocol, not a serial port)", "follows target vref: 1.2-3.3 V", false};
    }
    std::vector<Capability> capabilities() const override {
        return {{"debug interface only", "drives tck/tms/tdi, samples tdo of a target; runs no user workloads"},
                {"vref sensing", "i/o level follows the target's vref pin; never drives vref"},
                {"chain scan", "reads idcodes of devices in the scan chain"},
                {"reset lines", "optional ntrst / nsrst, open-drain"}};
    }

protected:
    std::vector<TelemetryField> sample(std::uint64_t seq) const override {
        return {{"target_vref", fixed(3.30 + wave(seq, 20, 0.005), 3), "V"},
                {"tck_frequency", "1000000", "Hz"},
                {"chain_length", "1", "devices"},
                {"idcode_0", "0x0362d093 (simulated artix-7 xc7a35t)", ""}};
    }
    double nominal_latency_ms() const override { return 25.0; }
};

}  // namespace

const char* to_string(DeviceKind k) {
    switch (k) {
        case DeviceKind::microcontroller: return "microcontroller";
        case DeviceKind::fpga: return "fpga";
        case DeviceKind::jtag_adapter: return "jtag_adapter";
    }
    return "unknown";
}

const char* to_string(LinkState s) {
    switch (s) {
        case LinkState::disconnected: return "disconnected";
        case LinkState::connected: return "connected";
        case LinkState::faulted: return "faulted";
    }
    return "unknown";
}

const char* to_string(FaultMode f) {
    switch (f) {
        case FaultMode::none: return "none";
        case FaultMode::timeout_once: return "timeout_once";
        case FaultMode::timeout_always: return "timeout_always";
    }
    return "unknown";
}

bool parse_fault_mode(const std::string& s, FaultMode& out) {
    if (s == "none") out = FaultMode::none;
    else if (s == "timeout_once") out = FaultMode::timeout_once;
    else if (s == "timeout_always") out = FaultMode::timeout_always;
    else return false;
    return true;
}

LinkState DeviceAdapter::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void DeviceAdapter::set_fault(FaultMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    fault_ = mode;
}

FaultMode DeviceAdapter::fault() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fault_;
}

void DeviceAdapter::set_latency_enabled(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    latency_ = on;
}

bool DeviceAdapter::consume_timeout() {
    if (fault_ == FaultMode::timeout_always) return true;
    if (fault_ == FaultMode::timeout_once) {
        fault_ = FaultMode::none;  // the next attempt succeeds: this is the recoverable case
        return true;
    }
    return false;
}

void DeviceAdapter::wait_ms(double ms) const {
    if (latency_) std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(ms));
}

OpResult DeviceAdapter::connect() {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    OpResult r;
    if (state_ == LinkState::connected) {
        r.code = "already_connected";
        r.message = "already connected";
    } else if (consume_timeout()) {
        wait_ms(kTimeoutMs);
        state_ = LinkState::faulted;
        r = {false, "timeout", "no handshake reply within 400 ms (simulated timeout). retry connect to recover",
             true};
    } else {
        wait_ms(nominal_latency_ms());
        state_ = LinkState::connected;
        r.message = "connected (simulated link)";
    }
    r.elapsed_ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    return r;
}

OpResult DeviceAdapter::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    OpResult r;
    if (state_ == LinkState::disconnected) {
        r.code = "not_connected";
        r.message = "already disconnected";
    } else {
        state_ = LinkState::disconnected;
        r.message = "disconnected; link released";
    }
    return r;
}

OpResult DeviceAdapter::read_telemetry(Telemetry& out) {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    OpResult r;
    if (state_ != LinkState::connected) {
        r = {false, "not_connected",
             state_ == LinkState::faulted ? "link is faulted after a timeout; reconnect first"
                                          : "connect to the device before reading telemetry",
             true};
    } else if (consume_timeout()) {
        wait_ms(kTimeoutMs);
        state_ = LinkState::faulted;
        r = {false, "timeout", "telemetry frame not received within 400 ms (simulated). reconnect to recover", true};
    } else {
        wait_ms(nominal_latency_ms());
        out.device_id = info().id;
        out.sequence = ++sequence_;
        out.fields = sample(out.sequence);
        r.message = "telemetry frame received (simulated)";
    }
    r.elapsed_ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    return r;
}

std::unique_ptr<DeviceAdapter> make_mock_mcu() { return std::make_unique<MockMcu>(); }
std::unique_ptr<DeviceAdapter> make_mock_fpga() { return std::make_unique<MockFpga>(); }
std::unique_ptr<DeviceAdapter> make_mock_jtag() { return std::make_unique<MockJtag>(); }

DeviceRegistry::DeviceRegistry(bool simulate_latency) : latency_(simulate_latency) {}

std::vector<DeviceAdapter*> DeviceRegistry::discover() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (devices_.empty()) {
        devices_.push_back(make_mock_mcu());
        devices_.push_back(make_mock_fpga());
        devices_.push_back(make_mock_jtag());
        for (auto& d : devices_) d->set_latency_enabled(latency_);
    }
    std::vector<DeviceAdapter*> out;
    for (auto& d : devices_) out.push_back(d.get());
    return out;
}

DeviceAdapter* DeviceRegistry::find(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& d : devices_) {
        if (d->info().id == id) return d.get();
    }
    return nullptr;
}

}  // namespace wb::hw
