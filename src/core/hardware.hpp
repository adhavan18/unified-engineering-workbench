#pragma once
// simulated device adapters for task e. nothing here talks to real hardware:
// every adapter is a mock with deterministic telemetry and injectable faults.
//
// the three device types are deliberately not interchangeable. the jtag adapter
// is a debug/boundary-scan probe that drives another board's tap; it runs no
// user workload, so it reports no compute capability.

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace wb::hw {

enum class DeviceKind { microcontroller, fpga, jtag_adapter };
enum class LinkState { disconnected, connected, faulted };
enum class FaultMode { none, timeout_once, timeout_always };

const char* to_string(DeviceKind k);
const char* to_string(LinkState s);
const char* to_string(FaultMode f);
bool parse_fault_mode(const std::string& s, FaultMode& out);

struct Capability {
    std::string name;
    std::string detail;
};

struct DeviceInfo {
    std::string id;
    DeviceKind kind;
    std::string model;          // device class, e.g. "stm32f4-class mcu board (simulated)"
    std::string host_link;      // how the host would reach it, e.g. "usb cdc-acm serial"
    std::string io_voltage;     // signalling level the host side must respect
    bool can_run_workloads;     // false for the jtag probe
};

struct TelemetryField {
    std::string name;
    std::string value;
    std::string unit;
};

struct Telemetry {
    std::string device_id;
    std::uint64_t sequence = 0;
    std::vector<TelemetryField> fields;
};

// result of one adapter operation. `code` is stable for programs, `message`
// is for people, `recoverable` tells the ui whether a retry makes sense.
struct OpResult {
    bool ok = true;
    std::string code = "ok";
    std::string message;
    bool recoverable = false;
    double elapsed_ms = 0.0;
};

class DeviceAdapter {
public:
    virtual ~DeviceAdapter() = default;

    virtual DeviceInfo info() const = 0;
    virtual std::vector<Capability> capabilities() const = 0;

    OpResult connect();
    OpResult disconnect();
    OpResult read_telemetry(Telemetry& out);

    LinkState state() const;
    void set_fault(FaultMode mode);
    FaultMode fault() const;

    // simulated latency is real wall-clock sleep so the ui shows genuine
    // in-flight states; tests turn it off.
    void set_latency_enabled(bool on);

protected:
    virtual std::vector<TelemetryField> sample(std::uint64_t sequence) const = 0;
    virtual double nominal_latency_ms() const = 0;

private:
    bool consume_timeout();  // true if this operation should time out
    void wait_ms(double ms) const;

    mutable std::mutex mutex_;
    LinkState state_ = LinkState::disconnected;
    FaultMode fault_ = FaultMode::none;
    std::uint64_t sequence_ = 0;
    bool latency_ = true;
};

std::unique_ptr<DeviceAdapter> make_mock_mcu();
std::unique_ptr<DeviceAdapter> make_mock_fpga();
std::unique_ptr<DeviceAdapter> make_mock_jtag();

// owns the adapters. discovery is simulated: it always "finds" one of each.
class DeviceRegistry {
public:
    explicit DeviceRegistry(bool simulate_latency = true);
    std::vector<DeviceAdapter*> discover();
    DeviceAdapter* find(const std::string& id);

private:
    std::mutex mutex_;
    bool latency_;
    std::vector<std::unique_ptr<DeviceAdapter>> devices_;
};

constexpr double kTimeoutMs = 400.0;

}  // namespace wb::hw
