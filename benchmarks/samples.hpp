#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace perf {
struct Directory {
    std::filesystem::path path;
    Directory() {
        std::random_device random;
        do { path = std::filesystem::temp_directory_path() / ("native-perf-" + std::to_string(random())); }
        while (!std::filesystem::create_directory(path));
    }
    ~Directory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
inline void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Metric { std::string name; size_t iterations; std::vector<double> samples; };
template<class Function> Metric measure(std::string name, size_t iterations, Function function) {
    Metric result{std::move(name), iterations, {}};
    for (size_t pass = 0; pass < 24; ++pass) {
        const auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < iterations; ++i) function();
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count()
                          / static_cast<double>(iterations);
        if (pass >= 3) result.samples.push_back(us);
    }
    return result;
}
inline void report(const std::vector<Metric>& metrics) {
    std::cout << std::setprecision(12) << "{\"build_type\":\"" << PERF_BUILD_TYPE
              << "\",\"compiler\":\"" << PERF_COMPILER << "\",\"clock\":\"steady_clock\",\"warmup_passes\":3,\"measurements\":[";
    bool first = true;
    for (const auto& metric : metrics) {
        auto sorted = metric.samples;
        std::sort(sorted.begin(), sorted.end());
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"name\":\"" << metric.name << "\",\"unit\":\"us_per_operation\",\"iterations_per_sample\":"
                  << metric.iterations << ",\"median\":" << sorted[sorted.size()/2]
                  << ",\"p95\":" << sorted[static_cast<size_t>(std::ceil(0.95*static_cast<double>(sorted.size())))-1]
                  << ",\"samples\":[";
        for (size_t i = 0; i < metric.samples.size(); ++i) { if (i) std::cout << ','; std::cout << metric.samples[i]; }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}
}
