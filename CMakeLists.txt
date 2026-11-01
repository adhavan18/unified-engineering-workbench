cmake_minimum_required(VERSION 3.20)
project(unified_engineering_workbench VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "build type" FORCE)
endif()

if(NOT WIN32)
  message(FATAL_ERROR "the workbench targets windows 11 x64 (winsock + win32 launcher)")
endif()

# warnings on, optimisation from the build type (-O2 for release)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
  # fully static: Launch.exe needs no mingw runtime dlls on a clean machine
  add_link_options(-static -static-libgcc -static-libstdc++)
elseif(MSVC)
  add_compile_options(/W4 /utf-8)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" WB_BT)
set(WB_FLAGS_STRING "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${WB_BT}} -std=c++17")

# ---- core: engineering logic, no transport or ui code ----------------------
add_library(wb_core STATIC
  src/core/fea1d.cpp
  src/core/batch.cpp
  src/core/worker_pool.cpp
  src/core/stl.cpp
  src/core/nodal_csv.cpp
  src/core/hardware.cpp
  src/core/backprop.cpp
  src/core/sysinfo.cpp
)
target_include_directories(wb_core PUBLIC src)
target_compile_definitions(wb_core PRIVATE
  WB_BUILD_FLAGS="${WB_FLAGS_STRING}"
  WB_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
find_package(Threads REQUIRED)
target_link_libraries(wb_core PUBLIC Threads::Threads)

# ---- server: http transport + json api over the core -----------------------
add_library(wb_server STATIC
  src/server/http.cpp
  src/server/jobs.cpp
  src/server/api.cpp
)
target_link_libraries(wb_server PUBLIC wb_core ws2_32)

# ---- Launch.exe -------------------------------------------------------------
add_executable(Launch src/app/main.cpp)
target_link_libraries(Launch PRIVATE wb_server shell32)

# ---- report tool: regenerates results/ (csv + svg plots) --------------------
add_executable(wb_report tools/report_main.cpp src/report/svg_plot.cpp)
target_link_libraries(wb_report PRIVATE wb_core)

# ---- example data generator --------------------------------------------------
add_executable(wb_datagen tools/datagen_main.cpp)
target_link_libraries(wb_datagen PRIVATE wb_core)

# ---- tests --------------------------------------------------------------------
enable_testing()
add_executable(wb_tests
  tests/test_main.cpp
  tests/test_fea.cpp
  tests/test_batch.cpp
  tests/test_stl.cpp
  tests/test_csv.cpp
  tests/test_hardware.cpp
  tests/test_backprop.cpp
  tests/test_http_api.cpp
)
target_link_libraries(wb_tests PRIVATE wb_server)
# tests read tests/fixtures relative to the source root
add_test(NAME unit_and_numerical_tests COMMAND wb_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ---- timing experiments kept as evidence for the performance write-up ------
add_executable(wb_order_probe tools/experiments/order_probe.cpp)
target_link_libraries(wb_order_probe PRIVATE wb_core)
add_executable(wb_thread_probe tools/experiments/thread_probe.cpp)
target_link_libraries(wb_thread_probe PRIVATE wb_core)
