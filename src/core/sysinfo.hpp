#pragma once
// host description for benchmark reports.

#include <string>

namespace wb {

struct SystemInfo {
    std::string cpu_name;
    int physical_cores = 0;
    int logical_cpus = 0;
    double ram_gib = 0.0;
    std::string os;
    std::string compiler;
    std::string build_flags;
    std::string build_type;
};

SystemInfo query_system_info();

// milliseconds since this process was created, measured from the os process
// creation time (so it includes loader time before main()).
double ms_since_process_start();

// unix epoch milliseconds of the process creation time.
double process_start_epoch_ms();

}  // namespace wb
