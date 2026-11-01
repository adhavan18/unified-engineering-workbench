// transport-level tests: request parsing and api routing without sockets.

#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#include "server/api.hpp"
#include "server/http.hpp"
#include "test_framework.hpp"

using namespace wb;

namespace {

struct Fixture {
    server::EventLog log;
    server::JobManager jobs;
    hw::DeviceRegistry devices{false};
    server::Api api;
    Fixture() : api(make_ctx(), jobs, devices, log) {}

    static server::AppContext make_ctx() {
        server::AppContext c;
        c.www_root = "src/frontend";
        c.data_root = "data";
        c.system.logical_cpus = 2;
        return c;
    }

    http::Response call(const std::string& method, const std::string& target, const std::string& body = "") {
        http::Request r;
        std::string err;
        CHECK(http::parse_head(method + " " + target + " HTTP/1.1\r\nHost: x", r, err));
        r.body = body;
        return api.handle(r);
    }

    // polls a job until it leaves the running state
    std::string wait_job(const std::string& id) {
        for (int i = 0; i < 600; ++i) {
            auto res = call("GET", "/api/jobs/" + id);
            if (res.body.find("\"state\":\"running\"") == std::string::npos) return res.body;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return "timeout";
    }
};

std::string job_id(const std::string& body) {
    const auto start = body.find("\"job\":\"") + 7;
    return body.substr(start, body.find('"', start) - start);
}

}  // namespace

TEST(http, url_decode_and_query_parsing) {
    CHECK(http::url_decode("a%20b+c%2C") == "a b c,");
    CHECK(http::url_decode("bad%zz") == "bad%zz");
    auto q = http::parse_query("meshes=10%2C20&k=10&flag");
    CHECK(q["meshes"] == "10,20");
    CHECK(q["k"] == "10");
    CHECK(q.count("flag") == 1);
}

TEST(http, parse_head_rejects_garbage) {
    http::Request r;
    std::string err;
    CHECK(!http::parse_head("NONSENSE", r, err));
    CHECK(!http::parse_head("GET relative HTTP/1.1", r, err));
    CHECK(http::parse_head("POST /api/x?a=1 HTTP/1.1\r\nContent-Length: 5\r\nX-Y:  z ", r, err));
    CHECK(r.method == "POST" && r.path == "/api/x" && r.query["a"] == "1");
    CHECK(r.headers["content-length"] == "5" && r.headers["x-y"] == "z");
}

TEST(api, serves_frontend_and_blocks_path_traversal) {
    Fixture f;
    auto r = f.call("GET", "/");
    CHECK(r.status == 200);
    CHECK(r.content_type.find("text/html") == 0);
    CHECK(f.call("GET", "/../CMakeLists.txt").status == 403);
    CHECK(f.call("GET", "/%2e%2e/CMakeLists.txt").status == 403);
    CHECK(f.call("GET", "/nope.js").status == 404);
}

TEST(api, unknown_routes_and_bad_parameters_give_actionable_errors) {
    Fixture f;
    CHECK(f.call("GET", "/api/nope").status == 404);
    auto r = f.call("POST", "/api/thermal/solve?k=abc");
    CHECK(r.status == 400);
    CHECK(r.body.find("'k' must be a finite number") != std::string::npos);
    CHECK(r.body.find("W/(m K)") != std::string::npos);
    r = f.call("POST", "/api/thermal/solve?k=-5");
    CHECK(r.status == 400 && r.body.find("conductivity") != std::string::npos);
    r = f.call("POST", "/api/thermal/solve?meshes=10,0");
    CHECK(r.status == 400);
}

TEST(api, stl_upload_errors_are_422_with_line_and_hint) {
    Fixture f;
    auto r = f.call("POST", "/api/geometry/stl?name=x.stl", "");
    CHECK(r.status == 422);
    CHECK(r.body.find("empty") != std::string::npos);
    r = f.call("POST", "/api/geometry/stl?name=x.stl",
               "solid a\nfacet normal 0 0 1\nouter loop\nvertex 0 0 nan\n");
    CHECK(r.status == 422);
    CHECK(r.body.find("\"line\":4") != std::string::npos);
    CHECK(r.body.find("\"hint\"") != std::string::npos);
}

TEST(api, stl_upload_returns_binary_payload) {
    Fixture f;
    auto r = f.call("POST", "/api/geometry/stl?name=t.stl",
                    "solid t\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\n"
                    "endloop\nendfacet\nendsolid t\n");
    CHECK(r.status == 200);
    CHECK(r.body.compare(0, 4, "WBF1") == 0);
    std::uint32_t len = 0;
    std::memcpy(&len, r.body.data() + 4, 4);
    const std::string meta = r.body.substr(8, len);
    CHECK(meta.find("\"triangles\":1") != std::string::npos);
    CHECK(meta.find("\"positions\"") != std::string::npos);
    const std::size_t data_start = (8 + len + 7) / 8 * 8;
    CHECK(r.body.size() == data_start + 9 * 4 + 4 /*pad*/ + 3 * 4 + 4 /*pad*/);
}

TEST(api, thermal_job_runs_and_exports_csv) {
    Fixture f;
    auto r = f.call("POST", "/api/thermal/solve?meshes=10,20");
    CHECK(r.status == 202);
    const std::string id = job_id(r.body);
    const std::string done = f.wait_job(id);
    CHECK(done.find("\"state\":\"done\"") != std::string::npos);
    CHECK(done.find("\"max_midpoint_error_C\":1.2") != std::string::npos);  // q h^2 / 8k = 1.25 C
    CHECK(f.call("GET", "/api/jobs/" + id + "/export?name=nodes").status == 200);
    auto csv = f.call("GET", "/api/jobs/" + id + "/export?name=convergence");
    CHECK(csv.status == 200);
    CHECK(csv.body.rfind("elements,h_m,", 0) == 0);
    CHECK(f.call("GET", "/api/jobs/" + id + "/export?name=bogus").status == 404);
}

TEST(api, cancelled_benchmark_publishes_no_results) {
    Fixture f;
    auto r = f.call("POST", "/api/perf/benchmark?cases=2000&elements=20000&reps=5");
    CHECK(r.status == 202);
    const std::string id = job_id(r.body);
    CHECK(f.call("POST", "/api/perf/benchmark").status == 409);  // one benchmark at a time
    CHECK(f.call("POST", "/api/jobs/" + id + "/cancel").status == 200);
    const std::string done = f.wait_job(id);
    CHECK(done.find("\"state\":\"cancelled\"") != std::string::npos);
    CHECK(done.find("\"result\"") == std::string::npos);
    CHECK(f.call("GET", "/api/jobs/" + id + "/export?name=benchmark_summary").status == 404);
}

TEST(api, hardware_routes_demonstrate_timeout_and_recovery) {
    Fixture f;
    CHECK(f.call("GET", "/api/hardware/devices").body.find("jtag_adapter") != std::string::npos);
    CHECK(f.call("POST", "/api/hardware/mcu0/fault?mode=timeout_once").status == 200);
    auto r = f.call("POST", "/api/hardware/mcu0/connect");
    CHECK(r.body.find("\"code\":\"timeout\"") != std::string::npos);
    CHECK(r.body.find("\"recoverable\":true") != std::string::npos);
    r = f.call("POST", "/api/hardware/mcu0/connect");
    CHECK(r.body.find("\"ok\":true") != std::string::npos);
    r = f.call("POST", "/api/hardware/mcu0/telemetry");
    CHECK(r.body.find("\"telemetry\"") != std::string::npos);
    CHECK(f.call("POST", "/api/hardware/mcu0/fault?mode=bad").status == 400);
    CHECK(f.call("POST", "/api/hardware/zzz/connect").status == 404);
}

TEST(api, live_thermal_returns_peak_balance_and_error_curves) {
    Fixture f;
    auto r = f.call("GET", "/api/thermal/live?meshes=10,20");
    CHECK(r.status == 200);
    CHECK(r.body.find("\"peak_x_m\":0.42") != std::string::npos);
    CHECK(r.body.find("\"heat_generated_W\":1") != std::string::npos);
    CHECK(r.body.find("\"err_abs_C\":[") != std::string::npos);
    r = f.call("GET", "/api/thermal/live?meshes=150000,150000");
    CHECK(r.status == 400);
    CHECK(r.body.find("run as job") != std::string::npos);
}

TEST(api, stateless_thermal_export) {
    Fixture f;
    auto r = f.call("GET", "/api/thermal/export?what=midpoints&meshes=10");
    CHECK(r.status == 200);
    CHECK(r.body.rfind("elements,element,x_mid_m", 0) == 0);
    CHECK(f.call("GET", "/api/thermal/export?what=bogus").status == 400);
}

TEST(api, stl_units_are_converted_to_metres) {
    Fixture f;
    const std::string tri =
        "solid t\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 60 0 0\nvertex 0 40 0\nendloop\nendfacet\nendsolid t\n";
    auto mm = f.call("POST", "/api/geometry/stl?unit=mm", tri);
    CHECK(mm.body.find("\"bbox_max\":[0.06,0.04,0]") != std::string::npos);
    auto in = f.call("POST", "/api/geometry/stl?unit=in", tri);
    CHECK(in.body.find("\"metres_per_unit\":0.0254") != std::string::npos);
    CHECK(f.call("POST", "/api/geometry/stl?unit=furlong", tri).status == 400);
}

TEST(api, both_csv_examples_are_served) {
    Fixture f;
    auto h = f.call("GET", "/api/examples/csv?which=heatsink");
    CHECK(h.status == 200 && h.body.rfind("node_id,x_m,y_m,z_m,temperature_C", 0) == 0);
    CHECK(f.call("GET", "/api/examples/csv?which=rod").status == 200);
    CHECK(f.call("GET", "/api/examples/csv?which=moon").status == 400);
}

TEST(api, backprop_endpoint_passes_gradient_check) {
    Fixture f;
    auto r = f.call("GET", "/api/backprop");
    CHECK(r.status == 200);
    CHECK(r.body.find("\"gradient_check_ok\":true") != std::string::npos);
    CHECK(r.body.find("\"trajectory\":{\"w\":[0.5,0.62094644752") != std::string::npos);
    CHECK(f.call("GET", "/api/backprop?lr=0").status == 400);
    auto csv = f.call("GET", "/api/backprop/export?what=loss");
    CHECK(csv.body.rfind("step,loss\n0,", 0) == 0);
}
