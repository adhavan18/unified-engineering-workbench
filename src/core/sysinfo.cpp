#include "core/sysinfo.hpp"

#include <chrono>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef WB_BUILD_FLAGS
#define WB_BUILD_FLAGS "unknown"
#endif
#ifndef WB_BUILD_TYPE
#define WB_BUILD_TYPE "unknown"
#endif

namespace wb {

namespace {

std::string compiler_name() {
#if defined(__clang__)
    return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("gcc ") + __VERSION__;
#elif defined(_MSC_VER)
    return "msvc " + std::to_string(_MSC_FULL_VER);
#else
    return "unknown compiler";
#endif
}

#ifdef _WIN32
std::string read_cpu_name() {
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ,
                      &key) != ERROR_SUCCESS) {
        return "unknown cpu";
    }
    char buf[256] = {};
    DWORD size = sizeof buf - 1;
    DWORD type = 0;
    std::string name = "unknown cpu";
    if (RegQueryValueExA(key, "ProcessorNameString", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) ==
            ERROR_SUCCESS &&
        type == REG_SZ) {
        name = buf;
        while (!name.empty() && name.back() == ' ') name.pop_back();
    }
    RegCloseKey(key);
    return name;
}

int count_physical_cores() {
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (len == 0) return 0;
    std::vector<char> buf(len);
    auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data());
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &len)) return 0;
    int cores = 0;
    for (DWORD off = 0; off < len;) {
        auto* item = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + off);
        if (item->Relationship == RelationProcessorCore) ++cores;
        off += item->Size;
    }
    return cores;
}

double filetime_to_epoch_ms(const FILETIME& ft) {
    ULARGE_INTEGER t;
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    // filetime counts 100 ns ticks since 1601-01-01
    return (static_cast<double>(t.QuadPart) - 116444736000000000.0) / 10000.0;
}
#endif

double now_epoch_ms() {
    using namespace std::chrono;
    return static_cast<double>(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count()) / 1000.0;
}

}  // namespace

SystemInfo query_system_info() {
    SystemInfo s;
    s.compiler = compiler_name();
    s.build_flags = WB_BUILD_FLAGS;
    s.build_type = WB_BUILD_TYPE;
#ifdef _WIN32
    s.cpu_name = read_cpu_name();
    s.physical_cores = count_physical_cores();
    s.logical_cpus = static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof mem;
    if (GlobalMemoryStatusEx(&mem)) s.ram_gib = static_cast<double>(mem.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
    s.os = "windows";
#else
    s.cpu_name = "unknown cpu";
    s.logical_cpus = static_cast<int>(std::thread::hardware_concurrency());
    s.os = "non-windows";
#endif
    if (s.logical_cpus <= 0) s.logical_cpus = 1;
    return s;
}

double process_start_epoch_ms() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return filetime_to_epoch_ms(created);
    }
#endif
    static const double fallback = now_epoch_ms();
    return fallback;
}

double ms_since_process_start() { return now_epoch_ms() - process_start_epoch_ms(); }

}  // namespace wb
