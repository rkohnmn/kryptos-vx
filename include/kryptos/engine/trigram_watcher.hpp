#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v1.5 "Kronos" — In-Memory Trigram Index & OS Filesystem Watcher
//   • Sub-Millisecond (< 1 ms) Incremental Index Updates
//   • Inverted 24-Bit Trigram Posting Lists for Instant Candidate Pruning
//   • Native Linux `inotify` (`IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_TO`)
//   • Native Windows `ReadDirectoryChangesW` Event Loop
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/soa_index.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kryptos::engine {

struct TrigramQueryResult {
    struct FileHit {
        std::string relative_path;
        std::vector<size_t> byte_offsets;
        std::vector<size_t> line_numbers;
    };

    std::vector<FileHit> hits;
    size_t candidate_files_evaluated{0};
    size_t total_files_in_index{0};
    size_t total_matches{0};
    double query_latency_ms{0.0};
};

struct IncrementalUpdateMetrics {
    std::string relative_path;
    std::string event_type;
    size_t trigrams_indexed{0};
    double update_latency_ms{0.0};
};

class TrigramWatcherEngine {
    struct IndexedDoc {
        std::string full_path;
        std::string relative_path;
        std::string content;
        uint64_t content_hash{0};
        std::vector<uint32_t> unique_trigrams;
        bool active{true};
    };

    std::vector<IndexedDoc> docs_{};
    std::unordered_map<std::string, uint32_t> path_to_doc_id_{};
    // Maps 24-bit trigram key -> sorted vector of doc_ids
    std::unordered_map<uint32_t, std::vector<uint32_t>> posting_lists_{};
    size_t total_trigrams_stored_{0};
    double initial_build_ms_{0.0};

public:
    TrigramWatcherEngine() = default;

    // Build initial inverted trigram index from a populated WorkspaceIndexSoA
    void build_from_workspace(const core::WorkspaceIndexSoA& index);

    // Incrementally insert or update a single file in < 1 ms
    IncrementalUpdateMetrics upsert_file(
        const std::string& full_path,
        const std::string& relative_path,
        std::string_view event_type = "MODIFIED"
    );

    // Remove a deleted file from the active trigram posting lists in < 1 ms
    IncrementalUpdateMetrics remove_file(
        const std::string& relative_path
    );

    // Sub-millisecond trigram-accelerated substring query across the indexed workspace
    [[nodiscard]] TrigramQueryResult query(std::string_view needle) const;

    // Run native OS filesystem watcher daemon (`inotify` on Linux / `ReadDirectoryChangesW` on Windows)
    // for a bounded duration or event count, logging < 1 ms incremental updates in real time.
    int run_watcher_daemon(
        const std::string& root_dir,
        uint32_t max_events = 5,
        uint32_t timeout_ms = 2500,
        bool use_color = true
    );

    [[nodiscard]] size_t indexed_doc_count() const noexcept { return docs_.size(); }
    [[nodiscard]] size_t unique_trigram_count() const noexcept { return posting_lists_.size(); }
    [[nodiscard]] size_t total_postings_count() const noexcept { return total_trigrams_stored_; }
    [[nodiscard]] double initial_build_ms() const noexcept { return initial_build_ms_; }

    // Pack 3 ASCII/UTF-8 bytes into a 24-bit integer trigram key
    [[nodiscard]] static constexpr uint32_t pack_trigram(uint8_t a, uint8_t b, uint8_t c) noexcept {
        return (static_cast<uint32_t>(a) << 16) |
               (static_cast<uint32_t>(b) << 8)  |
                static_cast<uint32_t>(c);
    }

    // Extract deduplicated sorted 24-bit trigrams from a text buffer
    [[nodiscard]] static std::vector<uint32_t> extract_unique_trigrams(std::string_view text);
};

} // namespace kryptos::engine
