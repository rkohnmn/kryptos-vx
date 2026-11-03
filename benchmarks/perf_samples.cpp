#include "samples.hpp"
#include "kryptos/core/simd_scanner.hpp"
#include "kryptos/engine/trigram_watcher.hpp"
#include "kryptos/engine/fastcdc_sync.hpp"
#include <fstream>
int main() {
    try {
        perf::Directory directory;
        std::string bytes(1024*1024, 'x');
        for (size_t i = 0; i < bytes.size(); i += 64) bytes[i] = '\n';
        const auto* data = reinterpret_cast<const uint8_t*>(bytes.data());
        perf::require(kryptos::core::SimdScanner::count_byte_simd(data, bytes.size(), '\n') == 16384, "SIMD count differs");
        kryptos::engine::TrigramWatcherEngine index;
        for (int i = 0; i < 100; ++i) {
            const auto name = std::to_string(i) + ".txt";
            const auto path = directory.path / name;
            { std::ofstream file(path); file << "perfneedle " << std::string(1000, 'a'); }
            index.upsert_file(path.string(), name);
        }
        perf::require(index.query("perfneedle").total_matches == 100, "Trigram corpus differs");
        const auto updated = "inserted prefix" + bytes;
        const auto patch = kryptos::engine::FastCdcSync::build_delta_patch(bytes, updated);
        perf::require(kryptos::engine::FastCdcSync::apply_delta_patch(bytes, patch) == updated, "Delta reconstruction differs");
        volatile size_t sink = 0;
        std::vector<perf::Metric> metrics;
        metrics.push_back(perf::measure("simd_count_1MiB", 100, [&] { sink = kryptos::core::SimdScanner::count_byte_simd(data, bytes.size(), '\n'); }));
        metrics.push_back(perf::measure("trigram_query_100_documents", 100, [&] { sink = index.query("perfneedle").total_matches; }));
        metrics.push_back(perf::measure("fastcdc_1MiB", 5, [&] { sink = kryptos::engine::FastCdcSync::chunk_buffer(data, bytes.size()).size(); }));
        (void)sink;
        perf::report(metrics);
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
