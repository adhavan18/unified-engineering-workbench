// Launch.exe: starts the local backend on 127.0.0.1 and opens the ui in the
// default browser. closing this console window (or the ui's quit button) stops it.
//
// options:
//   --no-browser     do not open a browser (used by the vs code config and tests)
//   --port N         use a fixed port instead of a free one
//   --ready-check    start, print launch-to-ready time, exit (used for timing)

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include "core/hardware.hpp"
#include "core/sysinfo.hpp"
#include "server/api.hpp"
#include "server/event_log.hpp"
#include "server/http.hpp"
#include "server/jobs.hpp"

namespace fs = std::filesystem;

namespace {

wb::http::Server* g_server = nullptr;

BOOL WINAPI on_console_event(DWORD) {
    if (g_server) g_server->stop();
    return TRUE;
}

fs::path exe_dir() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
}

// packaged layout first (next to Launch.exe), then the source tree for dev builds.
fs::path first_existing(const fs::path& base, std::initializer_list<const char*> candidates) {
    for (const char* c : candidates) {
        fs::path p = (base / c).lexically_normal();
        std::error_code ec;
        if (fs::is_directory(p, ec)) return p;
    }
    return {};
}

void show_fatal(const std::string& message) {
    std::fprintf(stderr, "error: %s\n", message.c_str());
    MessageBoxA(nullptr, message.c_str(), "engineering workbench could not start", MB_ICONERROR | MB_OK);
}

}  // namespace

int main(int argc, char** argv) {
    bool open_browser = true;
    bool ready_check = false;
    int port = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-browser") == 0) open_browser = false;
        else if (std::strcmp(argv[i], "--ready-check") == 0) { ready_check = true; open_browser = false; }
        else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = std::atoi(argv[++i]);
    }

    SetConsoleTitleA("engineering workbench - close this window to stop");
    const fs::path base = exe_dir();
    wb::server::AppContext ctx;
    ctx.www_root = first_existing(base, {"app/www", "../src/frontend", "../../src/frontend"});
    ctx.data_root = first_existing(base, {"data", "../data", "../../data"});
    if (ctx.www_root.empty()) {
        show_fatal("the app/www folder was not found next to Launch.exe.\n"
                   "extract the whole zip (not just Launch.exe) and run it again.");
        return 1;
    }

    wb::server::EventLog log;
    wb::server::JobManager jobs;
    wb::hw::DeviceRegistry devices;
    ctx.system = wb::query_system_info();
    ctx.process_start_epoch_ms = wb::process_start_epoch_ms();

    std::unique_ptr<wb::server::Api> api;
    wb::http::Server server([&api](const wb::http::Request& r) { return api->handle(r); });
    g_server = &server;
    ctx.request_shutdown = [&server] { server.stop(); };

    int bound = 0;
    try {
        bound = server.listen(port);
    } catch (const std::exception& e) {
        show_fatal(std::string(e.what()) + "\ntry again, or pass --port with a free port.");
        return 1;
    }
    ctx.ready_ms = wb::ms_since_process_start();
    api = std::make_unique<wb::server::Api>(ctx, jobs, devices, log);

    const std::string url = "http://127.0.0.1:" + std::to_string(bound) + "/";
    if (ready_check) {
        std::printf("ready_ms=%.1f\n", ctx.ready_ms);
        return 0;
    }

    SetConsoleCtrlHandler(on_console_event, TRUE);
    log.add("info", "unified engineering workbench " + ctx.version + " listening on " + url);
    log.add("info", "launch-to-ready (process start -> listening): " + std::to_string(ctx.ready_ms) + " ms");
    // relative to Launch.exe so logs never contain machine-specific paths
    log.add("info", "frontend: " + ctx.www_root.lexically_relative(base).generic_u8string());
    log.add("info", "close this window or press quit in the ui to stop");

    if (open_browser) {
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    server.serve();
    log.add("info", "server stopped");
    return 0;
}
