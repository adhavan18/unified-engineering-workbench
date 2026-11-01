#pragma once
// maps http routes onto the c++ core. this is the only file that knows both
// about http and about the engineering modules; the core never sees a request.

#include <filesystem>
#include <functional>
#include <string>

#include "core/hardware.hpp"
#include "core/sysinfo.hpp"
#include "server/event_log.hpp"
#include "server/http.hpp"
#include "server/jobs.hpp"

namespace wb::server {

struct AppContext {
    std::filesystem::path www_root;   // static frontend files
    std::filesystem::path data_root;  // example data shipped with the app
    std::string version = "1.0.0";
    double ready_ms = 0.0;            // process start -> listening socket
    double process_start_epoch_ms = 0.0;
    SystemInfo system;
    std::function<void()> request_shutdown;
};

class Api {
public:
    Api(AppContext ctx, JobManager& jobs, hw::DeviceRegistry& devices, EventLog& log);
    http::Response handle(const http::Request& req);

private:
    http::Response route(const http::Request& req);
    http::Response serve_static(const std::string& path);

    http::Response status();
    http::Response parse_stl(const http::Request& req);
    http::Response parse_csv(const http::Request& req);
    http::Response example(const std::string& which);
    http::Response start_thermal(const http::Request& req);
    http::Response start_benchmark(const http::Request& req);
    http::Response job_status(const std::string& id);
    http::Response job_cancel(const std::string& id);
    http::Response job_export(const std::string& id, const http::Request& req);
    http::Response hardware_list();
    http::Response hardware_action(const std::string& id, const std::string& action, const http::Request& req);
    http::Response backprop(const http::Request& req, bool as_csv);
    http::Response log_tail();

    AppContext ctx_;
    JobManager& jobs_;
    hw::DeviceRegistry& devices_;
    EventLog& log_;
};

// packs a json header and raw typed arrays into one response body:
//   "WBF1" | u32 json_len | json | zero padding to 8 bytes | array bytes...
// array offsets in the json are relative to the first byte after the padding.
struct BinaryArray {
    std::string name;
    std::string type;  // "f32" or "f64"
    const void* data;
    std::size_t count;
};
std::string pack_binary(const std::string& meta_json_object_without_arrays, const std::vector<BinaryArray>& arrays);

}  // namespace wb::server
