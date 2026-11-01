#include "server/api.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "core/backprop.hpp"
#include "core/batch.hpp"
#include "core/fea1d.hpp"
#include "core/json.hpp"
#include "core/nodal_csv.hpp"
#include "core/stl.hpp"
#include "core/text_scan.hpp"
#include "core/thermal_study.hpp"

namespace wb::server {

namespace fs = std::filesystem;

namespace {

// thrown by route helpers; turned into a json error response in handle().
struct ApiError {
    int status;
    std::string message;
    std::string hint;
    std::size_t line = 0;
};

http::Response json_response(const std::string& body, int status = 200) {
    http::Response r;
    r.status = status;
    r.body = body;
    return r;
}

http::Response error_response(const ApiError& e) {
    JsonWriter w;
    w.begin_object().field("error", e.message);
    if (!e.hint.empty()) w.field("hint", e.hint);
    if (e.line > 0) w.field("line", e.line);
    w.end_object();
    return json_response(w.str(), e.status);
}

http::Response csv_download(const std::string& csv, const std::string& filename) {
    http::Response r;
    r.content_type = "text/csv; charset=utf-8";
    r.body = csv;
    r.headers.push_back({"Content-Disposition", "attachment; filename=\"" + filename + "\""});
    return r;
}

std::string fmt(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.12g", v);
    return buf;
}

double number_param(const http::Request& req, const std::string& key, double fallback, const std::string& unit) {
    auto it = req.query.find(key);
    if (it == req.query.end() || text::trim(it->second).empty()) return fallback;
    double v = 0.0;
    if (text::parse_double(text::trim(it->second), v) != text::NumberStatus::ok) {
        throw ApiError{400, "parameter '" + key + "' must be a finite number, got '" + text::preview(it->second) + "'",
                       unit.empty() ? "" : "enter a plain number in " + unit};
    }
    return v;
}

long long integer_param(const http::Request& req, const std::string& key, long long fallback) {
    const double v = number_param(req, key, static_cast<double>(fallback), "");
    if (v != std::floor(v) || std::abs(v) > 9.0e15) {
        throw ApiError{400, "parameter '" + key + "' must be a whole number", ""};
    }
    return static_cast<long long>(v);
}

std::vector<int> int_list_param(const http::Request& req, const std::string& key, const std::string& fallback) {
    auto it = req.query.find(key);
    const std::string raw = (it == req.query.end() || text::trim(it->second).empty()) ? fallback : it->second;
    std::vector<int> out;
    std::stringstream ss(raw);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const auto t = text::trim(item);
        if (t.empty()) continue;
        double v = 0.0;
        if (text::parse_double(t, v) != text::NumberStatus::ok || v != std::floor(v) || v < 1 || v > 1e7) {
            throw ApiError{400, "'" + key + "' must be a comma-separated list of positive whole numbers",
                           "example: 10,20,40,80"};
        }
        out.push_back(static_cast<int>(v));
    }
    if (out.empty()) throw ApiError{400, "'" + key + "' is empty", "example: 10,20,40,80"};
    if (out.size() > 16) throw ApiError{400, "'" + key + "' can list at most 16 values", ""};
    return out;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string mime_for(const fs::path& p) {
    const std::string ext = p.extension().string();
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".js") return "text/javascript; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".json") return "application/json";
    return "application/octet-stream";
}

void write_system(JsonWriter& w, const SystemInfo& s) {
    w.begin_object();
    w.field("cpu", s.cpu_name).field("physical_cores", s.physical_cores).field("logical_cpus", s.logical_cpus);
    w.field("ram_gib", s.ram_gib).field("os", s.os).field("compiler", s.compiler);
    w.field("build_flags", s.build_flags).field("build_type", s.build_type);
    w.end_object();
}

fea::RodProblem rod_from(const http::Request& req) {
    fea::RodProblem p;
    p.length_m = number_param(req, "L", p.length_m, "m");
    p.area_m2 = number_param(req, "A", p.area_m2, "m^2");
    p.conductivity_W_mK = number_param(req, "k", p.conductivity_W_mK, "W/(m K)");
    p.heat_gen_W_m3 = number_param(req, "q", p.heat_gen_W_m3, "W/m^3");
    p.t_left_C = number_param(req, "T0", p.t_left_C, "C");
    p.t_right_C = number_param(req, "TL", p.t_right_C, "C");
    return p;
}

nn::Params params_from(const http::Request& req) {
    nn::Params p;
    p.w = number_param(req, "w", p.w, "");
    p.b = number_param(req, "b", p.b, "");
    p.v = number_param(req, "v", p.v, "");
    p.c = number_param(req, "c", p.c, "");
    return p;
}

void write_device(JsonWriter& w, hw::DeviceAdapter& d) {
    const auto info = d.info();
    w.begin_object();
    w.field("id", info.id).field("kind", hw::to_string(info.kind)).field("model", info.model);
    w.field("host_link", info.host_link).field("io_voltage", info.io_voltage);
    w.field("can_run_workloads", info.can_run_workloads).field("simulated", true);
    w.field("state", hw::to_string(d.state())).field("fault", hw::to_string(d.fault()));
    w.key("capabilities").begin_array();
    for (const auto& c : d.capabilities()) w.begin_object().field("name", c.name).field("detail", c.detail).end_object();
    w.end_array();
    w.end_object();
}

}  // namespace

std::string pack_binary(const std::string& meta, const std::vector<BinaryArray>& arrays) {
    JsonWriter aw;
    aw.begin_object();
    std::size_t offset = 0;
    for (const auto& a : arrays) {
        const std::size_t elem = a.type == "f64" ? 8 : 4;
        aw.key(a.name).begin_object().field("type", a.type).field("offset", offset).field("count", a.count).end_object();
        offset += a.count * elem;
        offset = (offset + 7) / 8 * 8;  // keep every array 8-byte aligned for typed-array views
    }
    aw.end_object();

    std::string json = meta;
    json.pop_back();  // drop the closing brace and append "arrays"
    if (json.size() > 1) json += ',';
    json += "\"arrays\":" + aw.str() + "}";

    std::string out = "WBF1";
    const auto len = static_cast<std::uint32_t>(json.size());
    out.append(reinterpret_cast<const char*>(&len), 4);
    out += json;
    out.append((8 - out.size() % 8) % 8, '\0');
    const std::size_t data_start = out.size();
    out.resize(data_start + offset, '\0');
    std::size_t pos = 0;
    for (const auto& a : arrays) {
        const std::size_t bytes = a.count * (a.type == "f64" ? 8 : 4);
        if (bytes) std::memcpy(&out[data_start + pos], a.data, bytes);
        pos = (pos + bytes + 7) / 8 * 8;
    }
    return out;
}

Api::Api(AppContext ctx, JobManager& jobs, hw::DeviceRegistry& devices, EventLog& log)
    : ctx_(std::move(ctx)), jobs_(jobs), devices_(devices), log_(log) {}

http::Response Api::handle(const http::Request& req) {
    try {
        return route(req);
    } catch (const ApiError& e) {
        if (e.status >= 400) log_.add("warn", req.method + " " + req.path + " -> " + std::to_string(e.status) + ": " + e.message);
        return error_response(e);
    } catch (const std::exception& e) {
        log_.add("error", req.method + " " + req.path + " -> " + e.what());
        return error_response({500, std::string("internal error: ") + e.what(), "see the activity log"});
    }
}

http::Response Api::route(const http::Request& req) {
    const std::string& p = req.path;
    const bool get = req.method == "GET";
    const bool post = req.method == "POST";

    if (p.rfind("/api/", 0) != 0) {
        if (!get) throw ApiError{405, "only GET is allowed for static files", ""};
        return serve_static(p);
    }
    if (p == "/api/status" && get) return status();
    if (p == "/api/log" && get) return log_tail();
    if (p == "/api/geometry/stl" && post) return parse_stl(req);
    if (p == "/api/results/nodal-csv" && post) return parse_csv(req);
    if (p == "/api/examples/stl" && get) return example(req, "stl");
    if (p == "/api/examples/csv" && get) return example(req, "csv");
    if (p == "/api/thermal/solve" && post) return start_thermal(req);
    if (p == "/api/thermal/live" && get) return thermal_live(req);
    if (p == "/api/thermal/export" && get) return thermal_export(req);
    if (p == "/api/perf/benchmark" && post) return start_benchmark(req);
    if (p == "/api/backprop" && get) return backprop(req, false);
    if (p == "/api/backprop/export" && get) return backprop(req, true);
    if (p == "/api/hardware/devices" && get) return hardware_list();
    if (p == "/api/shutdown" && post) {
        log_.add("info", "shutdown requested from the ui");
        if (ctx_.request_shutdown) ctx_.request_shutdown();
        return json_response("{\"ok\":true}");
    }

    // /api/jobs/{id}[/cancel|/export]
    if (p.rfind("/api/jobs/", 0) == 0) {
        std::string rest = p.substr(10);
        const std::size_t slash = rest.find('/');
        const std::string id = rest.substr(0, slash);
        const std::string action = slash == std::string::npos ? "" : rest.substr(slash + 1);
        if (action.empty() && get) return job_status(id);
        if (action == "cancel" && post) return job_cancel(id);
        if (action == "export" && get) return job_export(id, req);
    }
    // /api/hardware/{id}/{action}
    if (p.rfind("/api/hardware/", 0) == 0 && post) {
        std::string rest = p.substr(14);
        const std::size_t slash = rest.find('/');
        if (slash != std::string::npos) return hardware_action(rest.substr(0, slash), rest.substr(slash + 1), req);
    }
    throw ApiError{404, "unknown api route " + req.method + " " + p, ""};
}

http::Response Api::serve_static(const std::string& path) {
    std::string rel = path == "/" ? "index.html" : path.substr(1);
    if (rel.find("..") != std::string::npos || rel.find('\\') != std::string::npos ||
        rel.find(':') != std::string::npos) {
        throw ApiError{403, "path not allowed", ""};
    }
    const fs::path file = ctx_.www_root / fs::u8path(rel);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) throw ApiError{404, "not found: " + path, ""};
    http::Response r;
    r.content_type = mime_for(file);
    r.body = read_file(file);
    return r;
}

http::Response Api::status() {
    JsonWriter w;
    w.begin_object();
    w.field("app", "unified engineering workbench").field("version", ctx_.version);
    w.field("ready_ms", ctx_.ready_ms).field("process_start_epoch_ms", ctx_.process_start_epoch_ms);
    w.key("system");
    write_system(w, ctx_.system);
    w.end_object();
    return json_response(w.str());
}

http::Response Api::log_tail() {
    JsonWriter w;
    w.begin_object().key("lines").begin_array();
    for (const auto& line : log_.recent(200)) w.value(line);
    w.end_array().end_object();
    return json_response(w.str());
}

http::Response Api::parse_stl(const http::Request& req) {
    const std::string name = req.query.count("name") ? req.query.at("name") : "upload.stl";
    stl::Mesh mesh;
    try {
        mesh = stl::parse_ascii(req.body);
    } catch (const stl::ParseError& e) {
        throw ApiError{422, name + ": " + e.what(), e.hint(), e.line()};
    }
    const std::string unit = req.query.count("unit") ? req.query.at("unit") : "mm";
    double to_m = 1.0;
    if (!stl::metres_per_unit(unit, to_m)) throw ApiError{400, "unknown stl unit '" + unit + "'", "use mm, cm, m or in"};
    stl::scale(mesh, to_m);  // everything downstream is in metres
    log_.add("info", "stl '" + name + "' parsed: " + std::to_string(mesh.triangles) + " triangles, " +
                         std::to_string(mesh.degenerate) + " degenerate");

    JsonWriter w;
    w.begin_object();
    w.field("file", name).field("solid_name", mesh.name).field("triangles", mesh.triangles);
    w.field("degenerate_triangles", mesh.degenerate).field("solids", mesh.solids).field("bytes", req.body.size());
    w.field("unit", unit).field("metres_per_unit", to_m);
    w.key("bbox_min").array({mesh.bbox_min[0], mesh.bbox_min[1], mesh.bbox_min[2]});
    w.key("bbox_max").array({mesh.bbox_max[0], mesh.bbox_max[1], mesh.bbox_max[2]});
    w.end_object();

    http::Response r;
    r.content_type = "application/octet-stream";
    r.body = pack_binary(w.str(), {{"positions", "f32", mesh.vertices.data(), mesh.vertices.size()},
                                   {"normals", "f32", mesh.normals.data(), mesh.normals.size()}});
    return r;
}

http::Response Api::parse_csv(const http::Request& req) {
    const std::string name = req.query.count("name") ? req.query.at("name") : "upload.csv";
    csv::NodalField f;
    try {
        f = csv::parse_nodal(req.body);
    } catch (const csv::ParseError& e) {
        throw ApiError{422, name + ": " + e.what(), e.hint(), e.line()};
    }
    log_.add("info", "nodal csv '" + name + "' parsed: " + std::to_string(f.size()) + " nodes, T " + fmt(f.t_min_C) +
                         " .. " + fmt(f.t_max_C) + " C");

    std::vector<float> xyz(f.size() * 3);
    std::vector<double> ids(f.size());
    for (std::size_t i = 0; i < f.size(); ++i) {
        xyz[3 * i] = static_cast<float>(f.x_m[i]);
        xyz[3 * i + 1] = static_cast<float>(f.y_m[i]);
        xyz[3 * i + 2] = static_cast<float>(f.z_m[i]);
        ids[i] = static_cast<double>(f.node_id[i]);
    }
    JsonWriter w;
    w.begin_object();
    w.field("file", name).field("nodes", f.size()).field("blank_lines", f.blank_lines).field("bytes", req.body.size());
    w.field("t_min_C", f.t_min_C).field("t_max_C", f.t_max_C);
    w.key("bbox_min_m").array({f.bbox_min[0], f.bbox_min[1], f.bbox_min[2]});
    w.key("bbox_max_m").array({f.bbox_max[0], f.bbox_max[1], f.bbox_max[2]});
    w.end_object();

    http::Response r;
    r.content_type = "application/octet-stream";
    r.body = pack_binary(w.str(), {{"xyz_m", "f32", xyz.data(), xyz.size()},
                                   {"temperature_C", "f64", f.temp_C.data(), f.temp_C.size()},
                                   {"node_id", "f64", ids.data(), ids.size()}});
    return r;
}

namespace {

// what the ui needs from a study; arrays are already decimated by the core
std::string study_json(const fea::Study& s) {
    JsonWriter w;
    w.begin_object();
    w.key("problem").begin_object();
    w.field("L_m", s.rod.length_m).field("A_m2", s.rod.area_m2).field("k_W_mK", s.rod.conductivity_W_mK);
    w.field("q_W_m3", s.rod.heat_gen_W_m3).field("T0_C", s.rod.t_left_C).field("TL_C", s.rod.t_right_C);
    w.end_object();
    w.field("peak_x_m", s.peak_x_m).field("peak_T_C", s.peak_T_C);
    // heat leaving through each end, from the exact solution: q = -kA dT/dx (outward)
    const fea::RodProblem& p = s.rod;
    const double kA = p.conductivity_W_mK * p.area_m2;
    const double drift = (p.t_right_C - p.t_left_C) / p.length_m;
    const double bulge = p.heat_gen_W_m3 * p.length_m / (2 * p.conductivity_W_mK);
    w.field("heat_out_left_W", kA * (drift + bulge));    // = +kA T'(0)
    w.field("heat_out_right_W", -kA * (drift - bulge));  // = -kA T'(L)
    w.field("heat_generated_W", p.heat_gen_W_m3 * p.area_m2 * p.length_m);
    w.key("exact").begin_object().key("x_m").array(s.exact_x_m).key("T_C").array(s.exact_T_C).end_object();
    w.key("meshes").begin_array();
    for (std::size_t i = 0; i < s.meshes.size(); ++i) {
        const auto& m = s.meshes[i];
        w.begin_object();
        w.field("elements", m.elements).field("h_m", m.error.h_m);
        w.field("max_nodal_error_C", m.error.max_nodal_error_C);
        w.field("max_midpoint_error_C", m.error.max_midpoint_error_C);
        w.field("worst_midpoint_x_m", m.error.worst_midpoint_x_m);
        w.field("theory_midpoint_error_C", m.theory_midpoint_error_C);
        w.key("observed_order");
        if (i == 0) w.null();
        else w.value(m.observed_order);
        w.key("x_m").array(m.plot_x_m).key("T_C").array(m.plot_T_C);
        w.key("err_x_m").array(m.err_x_m).key("err_abs_C").array(m.err_abs_C);
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return w.str();
}

constexpr long long kLiveNodeLimit = 200'000;
constexpr long long kExportNodeLimit = 2'000'000;

}  // namespace

http::Response Api::example(const http::Request& req, const std::string& kind) {
    std::string name;
    if (kind == "stl") {
        name = "heatsink_ascii.stl";
    } else {
        const std::string which = req.query.count("which") ? req.query.at("which") : "heatsink";
        if (which == "heatsink") name = "heatsink_nodal_temperatures_synthetic.csv";
        else if (which == "rod") name = "rod_nodal_temperatures_synthetic.csv";
        else throw ApiError{400, "unknown example '" + which + "'", "use heatsink or rod"};
    }
    const fs::path file = ctx_.data_root / "examples" / name;
    std::string body = read_file(file);
    if (body.empty()) {
        throw ApiError{404, "example file missing: " + name, "the data/examples folder must sit next to Launch.exe"};
    }
    http::Response r;
    r.content_type = "text/plain; charset=utf-8";
    r.body = std::move(body);
    r.headers.push_back({"X-File-Name", name});
    return r;
}

http::Response Api::thermal_live(const http::Request& req) {
    const fea::RodProblem rod = rod_from(req);
    const std::vector<int> meshes = int_list_param(req, "meshes", "10,20,40,80");
    if (auto why = fea::validate_study(rod, meshes, kLiveNodeLimit); !why.empty()) {
        throw ApiError{400, why, "for very fine meshes use 'run as job', which has progress and cancel"};
    }
    return json_response(study_json(fea::run_study(rod, meshes, false)));
}

http::Response Api::thermal_export(const http::Request& req) {
    const fea::RodProblem rod = rod_from(req);
    const std::vector<int> meshes = int_list_param(req, "meshes", "10,20,40,80");
    if (auto why = fea::validate_study(rod, meshes, kExportNodeLimit); !why.empty()) throw ApiError{400, why, ""};
    const std::string what = req.query.count("what") ? req.query.at("what") : "convergence";
    if (what != "nodes" && what != "midpoints" && what != "convergence") {
        throw ApiError{400, "export must be nodes, midpoints or convergence", ""};
    }
    const fea::Study s = fea::run_study(rod, meshes, what != "convergence");
    const std::string csv = what == "nodes"       ? fea::nodes_csv(s)
                            : what == "midpoints" ? fea::midpoints_csv(s)
                                                  : fea::convergence_csv(s);
    log_.add("info", "exported fea_" + what + ".csv");
    return csv_download(csv, "fea_" + what + ".csv");
}

http::Response Api::start_thermal(const http::Request& req) {
    const fea::RodProblem rod = rod_from(req);
    const std::vector<int> meshes = int_list_param(req, "meshes", "10,20,40,80");
    if (auto why = fea::validate_study(rod, meshes, 20'000'000); !why.empty()) {
        throw ApiError{400, why, "fix the highlighted input"};
    }
    long long total_nodes = 0;
    for (int n : meshes) total_nodes += n + 1;
    const bool full_csv = total_nodes <= kExportNodeLimit;

    const std::string id = jobs_.start("thermal", [rod, meshes, full_csv](JobContext& job) {
        JobOutput out;
        const fea::Study s = fea::run_study(rod, meshes, full_csv, [&job](double f, const std::string& msg) {
            job.progress(f, msg);
            return !job.cancelled();
        });
        if (s.cancelled) return out;
        out.result_json = study_json(s);
        out.exports["convergence"] = fea::convergence_csv(s);
        if (full_csv) {
            out.exports["nodes"] = fea::nodes_csv(s);
            out.exports["midpoints"] = fea::midpoints_csv(s);
        }
        return out;
    });
    std::string mesh_list;
    for (int n : meshes) mesh_list += (mesh_list.empty() ? "" : ",") + std::to_string(n);
    log_.add("info", "thermal job " + id + " started, N = " + mesh_list);
    return json_response("{\"job\":\"" + id + "\"}", 202);
}

http::Response Api::start_benchmark(const http::Request& req) {
    batch::BenchConfig cfg;
    cfg.batch.cases = static_cast<int>(integer_param(req, "cases", 200));
    cfg.batch.elements = static_cast<int>(integer_param(req, "elements", 20000));
    cfg.batch.seed = static_cast<std::uint64_t>(integer_param(req, "seed", 20261101));
    cfg.workers = int_list_param(req, "workers", "1,2,4");
    cfg.repetitions = static_cast<int>(integer_param(req, "reps", 5));
    cfg.warmups = static_cast<int>(integer_param(req, "warmups", 1));
    if (auto why = batch::validate(cfg); !why.empty()) throw ApiError{400, why, "adjust the benchmark settings"};
    if (jobs_.running("benchmark")) {
        throw ApiError{409, "a benchmark is already running", "wait for it to finish or cancel it first"};
    }

    const SystemInfo sys = ctx_.system;
    const double ready_ms = ctx_.ready_ms;
    const std::string id = jobs_.start("benchmark", [cfg, sys, ready_ms](JobContext& job) {
        JobOutput out;
        // every finished run is streamed to the ui as provisional data
        std::string runs_json;
        auto on_run = [&](std::size_t row, int workers, int round, bool measured, double seconds) {
            JsonWriter r;
            r.begin_object().field("row", row).field("mode", row == 0 ? "serial" : "parallel");
            r.field("workers", workers).field("round", round).field("measured", measured).field("time_s", seconds);
            r.end_object();
            runs_json += (runs_json.empty() ? "" : ",") + r.str();
            job.partial("{\"runs\":[" + runs_json + "]}");
        };
        const auto report = batch::run_benchmark(
            cfg, sys.logical_cpus, job.cancel_flag(),
            [&job](std::size_t done, std::size_t total) {
                job.progress(static_cast<double>(done) / total,
                             "run " + std::to_string(done) + " of " + std::to_string(total));
            },
            on_run);
        if (report.cancelled) return out;

        JsonWriter w;
        std::ostringstream summary, raw;
        summary << "mode,workers,median_s,speedup,efficiency,max_abs_diff_C\n";
        raw << "mode,workers,repetition,time_s\n";
        w.begin_object();
        w.key("system");
        write_system(w, sys);
        w.field("launch_to_ready_ms", ready_ms);
        w.key("config").begin_object();
        w.field("cases", report.config.batch.cases).field("elements", report.config.batch.elements);
        w.field("seed", static_cast<long long>(report.config.batch.seed));
        w.field("k_range_W_mK", fmt(report.config.batch.k_min_W_mK) + " .. " + fmt(report.config.batch.k_max_W_mK));
        w.field("q_range_W_m3", fmt(report.config.batch.q_min_W_m3) + " .. " + fmt(report.config.batch.q_max_W_m3));
        w.field("warmups", report.config.warmups).field("repetitions", report.config.repetitions);
        w.end_object();
        w.key("skipped_workers").begin_array();
        for (int s : report.skipped_workers) w.value(s);
        w.end_array();
        w.field("single_cpu", report.single_cpu).field("agreement_ok", report.agreement_ok);
        w.field("agreement_limit_C", report.agreement_limit_C);
        w.key("rows").begin_array();
        for (const auto& row : report.rows) {
            w.begin_object().field("mode", row.label).field("workers", row.workers);
            w.key("times_s").array(row.times_s);
            w.field("median_s", row.median_s).field("speedup", row.speedup).field("efficiency", row.efficiency);
            w.field("max_abs_diff_C", row.max_abs_diff_C);
            w.end_object();
            summary << row.label << ',' << row.workers << ',' << fmt(row.median_s) << ',' << fmt(row.speedup) << ','
                    << fmt(row.efficiency) << ',' << fmt(row.max_abs_diff_C) << '\n';
            for (std::size_t r = 0; r < row.times_s.size(); ++r) {
                raw << row.label << ',' << row.workers << ',' << r + 1 << ',' << fmt(row.times_s[r]) << '\n';
            }
        }
        w.end_array();
        w.end_object();
        out.result_json = w.str();
        out.exports["benchmark_summary"] = summary.str();
        out.exports["benchmark_raw"] = raw.str();
        return out;
    });
    log_.add("info", "benchmark job " + id + " started: " + std::to_string(cfg.batch.cases) + " cases x " +
                         std::to_string(cfg.batch.elements) + " elements");
    return json_response("{\"job\":\"" + id + "\"}", 202);
}

http::Response Api::job_status(const std::string& id) {
    auto s = jobs_.status_json(id);
    if (!s) throw ApiError{404, "no job with id '" + id + "'", "the app may have been restarted; run it again"};
    return json_response(*s);
}

http::Response Api::job_cancel(const std::string& id) {
    if (!jobs_.cancel(id)) throw ApiError{404, "no job with id '" + id + "'", ""};
    log_.add("info", "cancel requested for " + id);
    return json_response("{\"ok\":true}");
}

http::Response Api::job_export(const std::string& id, const http::Request& req) {
    const std::string name = req.query.count("name") ? req.query.at("name") : "";
    auto csv = jobs_.export_csv(id, name);
    if (!csv) {
        throw ApiError{404, "export '" + name + "' is not available for job '" + id + "'",
                       "exports exist only for finished jobs; run the analysis again"};
    }
    log_.add("info", "exported " + name + " from " + id);
    return csv_download(*csv, name + ".csv");
}

http::Response Api::hardware_list() {
    auto devices = devices_.discover();
    JsonWriter w;
    w.begin_object();
    w.field("simulated", true);
    w.field("note", "all devices are software mocks; no usb, serial or jtag hardware is accessed");
    w.key("devices").begin_array();
    for (auto* d : devices) write_device(w, *d);
    w.end_array().end_object();
    log_.add("info", "device discovery (simulated): " + std::to_string(devices.size()) + " devices");
    return json_response(w.str());
}

http::Response Api::hardware_action(const std::string& id, const std::string& action, const http::Request& req) {
    devices_.discover();
    hw::DeviceAdapter* d = devices_.find(id);
    if (!d) throw ApiError{404, "no device '" + id + "'", "run discovery first"};

    hw::OpResult r;
    hw::Telemetry t;
    bool has_telemetry = false;
    if (action == "connect") {
        r = d->connect();
    } else if (action == "disconnect") {
        r = d->disconnect();
    } else if (action == "telemetry") {
        r = d->read_telemetry(t);
        has_telemetry = r.ok;
    } else if (action == "fault") {
        hw::FaultMode mode;
        const std::string m = req.query.count("mode") ? req.query.at("mode") : "";
        if (!hw::parse_fault_mode(m, mode)) {
            throw ApiError{400, "fault mode must be none, timeout_once or timeout_always", ""};
        }
        d->set_fault(mode);
        r.message = std::string("fault injection set to ") + hw::to_string(mode);
    } else {
        throw ApiError{404, "unknown device action '" + action + "'", "use connect, disconnect, telemetry or fault"};
    }
    log_.add(r.ok ? "info" : "warn", id + " " + action + ": " + r.code + " - " + r.message);

    JsonWriter w;
    w.begin_object();
    w.key("result").begin_object().field("ok", r.ok).field("code", r.code).field("message", r.message);
    w.field("recoverable", r.recoverable).field("elapsed_ms", r.elapsed_ms).end_object();
    if (has_telemetry) {
        w.key("telemetry").begin_object().field("device_id", t.device_id).field("sequence", t.sequence);
        w.key("fields").begin_array();
        for (const auto& f : t.fields) {
            w.begin_object().field("name", f.name).field("value", f.value).field("unit", f.unit).end_object();
        }
        w.end_array().end_object();
    }
    w.key("device");
    write_device(w, *d);
    w.end_object();
    return json_response(w.str());
}

http::Response Api::backprop(const http::Request& req, bool as_csv) {
    const double x = number_param(req, "x", 1.0, "");
    const double y = number_param(req, "y", 2.0, "");
    const double lr = number_param(req, "lr", 0.1, "");
    const double eps = number_param(req, "eps", 1e-6, "");
    const long long steps = integer_param(req, "steps", 100);
    const nn::Params p0 = params_from(req);
    if (!(lr > 0) || lr > 10) throw ApiError{400, "learning rate must be in (0, 10]", ""};
    if (!(eps > 0) || eps > 1e-2) throw ApiError{400, "epsilon must be in (0, 0.01]", ""};
    if (steps < 1 || steps > 100000) throw ApiError{400, "steps must be between 1 and 100000", ""};

    const nn::Forward f = nn::forward(p0, x, y);
    const nn::Gradients ga = nn::analytic_gradients(p0, x, y);
    const nn::Gradients gn = nn::finite_difference_gradients(p0, x, y, eps);
    const nn::Params p1 = nn::sgd_step(p0, ga, lr);
    const nn::Forward f1 = nn::forward(p1, x, y);
    // the handwritten update is step 1; the brief then asks for 100 further steps
    const auto losses = nn::train_losses(p0, x, y, lr, static_cast<int>(steps) + 1);

    const char* names[] = {"w", "b", "v", "c"};
    const double analytic[] = {ga.dw, ga.db, ga.dv, ga.dc};
    const double numeric[] = {gn.dw, gn.db, gn.dv, gn.dc};
    const double before[] = {p0.w, p0.b, p0.v, p0.c};
    const double after[] = {p1.w, p1.b, p1.v, p1.c};

    if (as_csv) {
        const std::string what = req.query.count("what") ? req.query.at("what") : "gradcheck";
        std::ostringstream ss;
        if (what == "loss") {
            ss << "step,loss\n";
            for (std::size_t i = 0; i < losses.size(); ++i) ss << i << ',' << fmt(losses[i]) << '\n';
        } else {
            ss << "parameter,value_before,analytic_grad,central_diff_grad,abs_error,value_after\n";
            for (int i = 0; i < 4; ++i) {
                ss << names[i] << ',' << fmt(before[i]) << ',' << fmt(analytic[i]) << ',' << fmt(numeric[i]) << ','
                   << fmt(std::abs(analytic[i] - numeric[i])) << ',' << fmt(after[i]) << '\n';
            }
        }
        return csv_download(ss.str(), "backprop_" + what + ".csv");
    }

    JsonWriter w;
    w.begin_object();
    w.key("forward").begin_object().field("z", f.z).field("h", f.h).field("y_hat", f.y_hat).field("loss", f.loss);
    w.end_object();
    w.key("gradients").begin_array();
    bool all_ok = true;
    for (int i = 0; i < 4; ++i) {
        const double err = std::abs(analytic[i] - numeric[i]);
        all_ok = all_ok && err <= 1e-5;
        w.begin_object().field("parameter", names[i]).field("before", before[i]).field("analytic", analytic[i]);
        w.field("central_difference", numeric[i]).field("abs_error", err).field("pass", err <= 1e-5);
        w.field("after", after[i]).end_object();
    }
    w.end_array();
    w.field("gradient_check_ok", all_ok).field("tolerance", 1e-5).field("epsilon", eps);
    w.field("loss_after_first_step", f1.loss);
    w.key("losses").array(losses);

    // every step's state, column-wise, for the step scrubber (capped to keep replies small)
    if (steps <= 2000) {
        const auto traj = nn::train_trajectory(p0, x, y, lr, static_cast<int>(steps) + 1);
        std::vector<double> cols[11];
        for (const auto& s : traj) {
            const double row[] = {s.params.w, s.params.b, s.params.v, s.params.c, s.forward.z, s.forward.h,
                                  s.forward.y_hat, s.grads.dw, s.grads.db, s.grads.dv, s.grads.dc};
            for (int c = 0; c < 11; ++c) cols[c].push_back(row[c]);
        }
        const char* col_names[] = {"w", "b", "v", "c", "z", "h", "y_hat", "dw", "db", "dv", "dc"};
        w.key("trajectory").begin_object();
        for (int c = 0; c < 11; ++c) w.key(col_names[c]).array(cols[c]);
        w.end_object();
    }
    w.end_object();
    return json_response(w.str());
}

}  // namespace wb::server
