// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX — Zero-Overhead Duplicate File & Redundancy Detector Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/engine/duplicate_finder.hpp"

#include <algorithm>
#include <unordered_map>

namespace kryptos::engine {

DuplicateReport DuplicateFinder::find_duplicates(const core::WorkspaceIndexSoA& index) {
    DuplicateReport report{};
    if (index.size() < 2) return report;

    // Stage 1: Group non-empty files by (size_bytes, content_hash)
    struct Key {
        uint64_t size;
        uint64_t hash;
        bool operator==(const Key& o) const noexcept {
            return size == o.size && hash == o.hash;
        }
    };
    struct KeyHasher {
        size_t operator()(const Key& k) const noexcept {
            return static_cast<size_t>(k.hash ^ (k.size * 0x9e3779b97f4a7c15ULL));
        }
    };

    std::unordered_map<Key, std::vector<size_t>, KeyHasher> buckets;
    buckets.reserve(index.size());

    for (size_t i = 0; i < index.size(); ++i) {
        if (index.size_bytes[i] == 0 || index.content_hash[i] == 0) continue;
        buckets[Key{index.size_bytes[i], index.content_hash[i]}].push_back(i);
    }

    for (auto& [key, indices] : buckets) {
        if (indices.size() > 1) {
            DuplicateCluster cluster{};
            cluster.content_hash = key.hash;
            cluster.file_size_bytes = key.size;
            cluster.wasted_bytes = key.size * (indices.size() - 1);
            for (size_t idx : indices) {
                cluster.paths.emplace_back(index.relative_path[idx]);
            }
            report.total_wasted_bytes += cluster.wasted_bytes;
            report.redundant_file_count += (indices.size() - 1);
            report.clusters.push_back(std::move(cluster));
        }
    }

    std::sort(report.clusters.begin(), report.clusters.end(),
              [](const DuplicateCluster& a, const DuplicateCluster& b) {
                  return a.wasted_bytes > b.wasted_bytes;
              });

    return report;
}

} // namespace kryptos::engine
