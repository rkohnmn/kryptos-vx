#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v1.5 "Kronos" — Structure of Arrays (SoA) High-Locality File Index
// Compliant with SASMP v1.3.0 / performance v3.0.0 (AoS vs SoA Cache Optimization)
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/kernel_fs.hpp"
#include "kryptos/core/simd_scanner.hpp"
#include "kryptos/core/thread_pool.hpp"
#include "kryptos/memory/pool_allocator.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kryptos::core {

enum class FileCategory : uint8_t {
    SourceCode = 0,
    ConfigOrDoc,
    BuildOrCacheBloat,
    MediaOrBinary,
    Other
};

// Traditional Array of Structures (AoS) used as a baseline in unit tests
// and micro-benchmarks to prove SoA cache-line superiority.
struct FileRecordAoS {
    std::string path;
    std::string extension;
    uint64_t size_bytes{0};
    uint64_t content_hash{0};
    uint32_t total_lines{0};
    uint32_t code_lines{0};
    uint32_t comment_lines{0};
    uint32_t blank_lines{0};
    uint32_t estimated_tokens{0};
    double shannon_entropy{0.0};
    FileCategory category{FileCategory::Other};
    bool is_binary{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// Structure of Arrays (SoA) Workspace Index
// Contiguous parallel vectors guarantee 100% cache-line utilization when
// summing sizes, filtering by category, sorting by token weight, or scanning hashes.
// ─────────────────────────────────────────────────────────────────────────────
struct WorkspaceIndexSoA {
    memory::StringArena string_arena{};

    // Hot numeric arrays (contiguous in L1/L2 cache)
    std::vector<uint64_t> size_bytes{};
    std::vector<uint64_t> content_hash{};
    std::vector<uint32_t> total_lines{};
    std::vector<uint32_t> code_lines{};
    std::vector<uint32_t> comment_lines{};
    std::vector<uint32_t> blank_lines{};
    std::vector<uint32_t> estimated_tokens{};
    std::vector<double>   shannon_entropy{};
    std::vector<FileCategory> category{};
    std::vector<uint8_t>  is_binary{};

    // String views pointing into contiguous StringArena slabs
    std::vector<std::string_view> relative_path{};
    std::vector<std::string_view> full_path{};
    std::vector<std::string_view> extension{};
    std::vector<std::string_view> language{};

    // Aggregate & Kernel Telemetry (v1.5 "Kronos")
    uint64_t total_bytes_indexed{0};
    uint64_t total_lines_indexed{0};
    uint64_t total_tokens_indexed{0};
    uint64_t bloat_bytes_detected{0};
    double   scan_duration_ms{0.0};
    KernelEnumTelemetry kernel_telemetry{};
    std::vector<SecretMatch> detected_secrets{};

    void reserve(size_t capacity);
    void clear() noexcept;
    [[nodiscard]] size_t size() const noexcept { return size_bytes.size(); }
    [[nodiscard]] bool empty() const noexcept { return size_bytes.empty(); }

    // Contiguous SoA reduction (SIMD auto-vectorizable)
    [[nodiscard]] uint64_t sum_sizes_soa() const noexcept;
    [[nodiscard]] uint64_t sum_tokens_soa() const noexcept;

    // Build index in parallel across a directory tree using KernelFs fast enumeration
    void index_directory(
        const std::string& root_dir,
        bool include_bloat_dirs = true,
        size_t num_threads = 0
    );

    [[nodiscard]] static std::string_view classify_language(std::string_view ext) noexcept;
    [[nodiscard]] static FileCategory classify_category(
        std::string_view rel_path,
        std::string_view ext,
        bool is_bin
    ) noexcept;
};

} // namespace kryptos::core
