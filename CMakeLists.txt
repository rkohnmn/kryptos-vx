cmake_minimum_required(VERSION 3.20)
project(kryptos_vx
    VERSION 3.0.0
    DESCRIPTION "KRYPTOS-VX v3.0 Apex: Zero-Bloat C++20 Full-Stack Systems Engine (Kernel I/O, Multi-ISA SIMD, Trigram, MCP, ADB Wire, FastCDC, PE/ELF/Mach-O Profiler)"
    LANGUAGES CXX
)

# ─────────────────────────────────────────────────────────────────────────────
# Global C++20 Configuration
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─────────────────────────────────────────────────────────────────────────────
# Build Options
# ─────────────────────────────────────────────────────────────────────────────
option(BUILD_TESTING "Build unit tests" ON)
option(BUILD_BENCHMARKS "Build performance micro-benchmarks" ON)
option(ENABLE_SANITIZERS "Enable ASan and UBSan in Debug builds" OFF)
option(ENABLE_AVX2 "Enable AVX2 SIMD intrinsics on x86_64" ON)
option(ENABLE_AVX512 "Enable AVX-512BW/F SIMD intrinsics on supported x86_64 CPUs" OFF)
option(ENABLE_LTO "Enable Link-Time Optimization in Release builds" ON)

# ─────────────────────────────────────────────────────────────────────────────
# Compiler Warnings Interface
# ─────────────────────────────────────────────────────────────────────────────
add_library(kryptos_vx_warnings INTERFACE)
target_compile_options(kryptos_vx_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor -Wcast-align
        -Wunused -Woverloaded-virtual -Wconversion
    >
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /permissive- /wd4996 /Zc:__cplusplus
    >
)

if(WIN32)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Performance & Multi-ISA SIMD Interface (AVX-512 / AVX2 / ARM64 NEON)
# ─────────────────────────────────────────────────────────────────────────────
add_library(kryptos_vx_optimization INTERFACE)
if(ENABLE_AVX512)
    target_compile_options(kryptos_vx_optimization INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-mavx512f -mavx512bw -mavx2 -mfma>
        $<$<CXX_COMPILER_ID:MSVC>:/arch:AVX512>
    )
elseif(ENABLE_AVX2)
    target_compile_options(kryptos_vx_optimization INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-mavx2 -mfma>
        $<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Sanitizers Interface (Debug Builds)
# ─────────────────────────────────────────────────────────────────────────────
add_library(kryptos_vx_sanitizers INTERFACE)
if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(kryptos_vx_sanitizers INTERFACE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(kryptos_vx_sanitizers INTERFACE
        -fsanitize=address,undefined
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Platform Detection & Core Static Library
# ─────────────────────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

add_library(kryptos_vx_core STATIC
    src/core/kernel_fs.cpp
    src/core/simd_scanner.cpp
    src/core/soa_index.cpp
    src/engine/adb_bridge.cpp
    src/engine/binary_profiler.cpp
    src/engine/context_packer.cpp
    src/engine/debloater.cpp
    src/engine/duplicate_finder.cpp
    src/engine/code_auditor.cpp
    src/engine/fastcdc_sync.cpp
    src/engine/mcp_server.cpp
    src/engine/trigram_watcher.cpp
    src/ui/tui_dashboard.cpp
)

target_include_directories(kryptos_vx_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(WIN32)
    target_compile_definitions(kryptos_vx_core PUBLIC PLATFORM_WINDOWS)
    target_link_libraries(kryptos_vx_core PUBLIC ws2_32)
elseif(APPLE)
    target_compile_definitions(kryptos_vx_core PUBLIC PLATFORM_MACOS)
else()
    target_compile_definitions(kryptos_vx_core PUBLIC PLATFORM_LINUX)
endif()

target_link_libraries(kryptos_vx_core
    PUBLIC Threads::Threads
    PRIVATE kryptos_vx_warnings kryptos_vx_optimization kryptos_vx_sanitizers
)

# ─────────────────────────────────────────────────────────────────────────────
# Main Executable Target (Embeds Win32 .rc Icon on Windows)
# ─────────────────────────────────────────────────────────────────────────────
if(WIN32)
    add_executable(kryptos_vx src/main.cpp assets/kryptos_vx.rc)
else()
    add_executable(kryptos_vx src/main.cpp)
endif()
target_link_libraries(kryptos_vx PRIVATE kryptos_vx_core kryptos_vx_warnings kryptos_vx_optimization kryptos_vx_sanitizers)

# ─────────────────────────────────────────────────────────────────────────────
# Unit Test & Benchmark Targets
# ─────────────────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
    enable_testing()
    add_executable(kryptos_vx_tests tests/test_main.cpp)
    target_link_libraries(kryptos_vx_tests PRIVATE kryptos_vx_core kryptos_vx_warnings kryptos_vx_optimization kryptos_vx_sanitizers)
    add_test(NAME KryptosVXUnitTests COMMAND kryptos_vx_tests)
endif()

if(BUILD_BENCHMARKS)
    add_executable(kryptos_vx_bench benchmarks/bench_main.cpp)
    target_link_libraries(kryptos_vx_bench PRIVATE kryptos_vx_core kryptos_vx_warnings kryptos_vx_optimization)
endif()
