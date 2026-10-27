#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX — Zero-Overhead Duplicate File & Redundancy Detector
// Uses 2-stage size bucket filtering + 64-bit SIMD rolling hash verification
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/soa_index.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kryptos::engine {

struct DuplicateCluster {
    uint64_t content_hash{0};
    uint64_t file_size_bytes{0};
    uint64_t wasted_bytes{0};
    std::vector<std::string> paths;
};

struct DuplicateReport {
    std::vector<DuplicateCluster> clusters;
    uint64_t total_wasted_bytes{0};
    size_t redundant_file_count{0};
};

class DuplicateFinder {
public:
    [[nodiscard]] static DuplicateReport find_duplicates(const core::WorkspaceIndexSoA& index);
};

} // namespace kryptos::engine
