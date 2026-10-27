#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v3.0.0 "Apex" — FastCDC Content-Defined Chunking, Binary Delta Patch
// Serialization (`KVXD`), and Cross-Device TCP/LAN Zero-Copy Sync Engine
//   • 64-Bit Rolling Gear Hash with Normalized Chunking (`MASK_S` / `MASK_L`)
//   • Sub-File Deduplication & Bit-Exact Binary Patch Reconstruction
//   • Native TCP/LAN Zero-Copy Sync Protocol (Manifest Exchange + Missing Chunk Stream)
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/soa_index.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kryptos::engine {

struct CdcChunk {
    size_t offset{0};
    size_t length{0};
    uint64_t gear_fingerprint{0};
    uint64_t content_hash64{0};
};

struct DeltaSyncSummary {
    std::string source_label;
    std::string target_label;
    size_t total_target_bytes{0};
    size_t reusable_unchanged_bytes{0};
    size_t delta_transfer_bytes{0};
    size_t serialized_patch_bytes{0};
    size_t total_chunks{0};
    size_t matched_chunks{0};
    double bandwidth_saved_pct{0.0};
    double throughput_mbs{0.0};
    double compute_ms{0.0};
    bool patch_verified_bit_exact{false};
};

struct LanSyncSessionReport {
    std::string endpoint;
    size_t target_file_bytes{0};
    size_t wire_bytes_transmitted{0};
    size_t chunks_total{0};
    size_t chunks_reused_locally{0};
    size_t chunks_fetched_over_lan{0};
    double bandwidth_saved_pct{0.0};
    double session_latency_ms{0.0};
    bool bit_exact_verified{false};
};

struct WorkspaceCdcReport {
    size_t files_processed{0};
    size_t total_chunks{0};
    size_t unique_chunks{0};
    uint64_t total_bytes_processed{0};
    uint64_t unique_bytes_stored{0};
    uint64_t subfile_dedup_saved_bytes{0};
    double throughput_mbs{0.0};
    double duration_ms{0.0};
};

class FastCdcSync {
public:
    static constexpr size_t DEFAULT_MIN_CHUNK = 2048;  // 2 KB
    static constexpr size_t DEFAULT_AVG_CHUNK = 8192;  // 8 KB
    static constexpr size_t DEFAULT_MAX_CHUNK = 65536; // 64 KB

    // Partition an arbitrary byte buffer into variable-size Content-Defined Chunks
    // using Normalized FastCDC 64-bit Rolling Gear Hash.
    [[nodiscard]] static std::vector<CdcChunk> chunk_buffer(
        const uint8_t* data,
        size_t len,
        size_t min_chunk = DEFAULT_MIN_CHUNK,
        size_t avg_chunk = DEFAULT_AVG_CHUNK,
        size_t max_chunk = DEFAULT_MAX_CHUNK
    );

    // Fixed-size block chunking baseline (for A/B deduplication shift resilience comparison)
    [[nodiscard]] static std::vector<CdcChunk> chunk_fixed_blocks(
        const uint8_t* data,
        size_t len,
        size_t block_size = DEFAULT_AVG_CHUNK
    );

    // Serialize a compact binary delta patch (`KVXD` wire format) between `base_data`
    // and `updated_data`. Matched chunks are encoded as 9-byte hash references;
    // modified chunks are encoded with inline literal bytes.
    [[nodiscard]] static std::string build_delta_patch(
        std::string_view base_data,
        std::string_view updated_data
    );

    // Reconstruct the exact `updated_data` byte-for-byte from `base_data` and a
    // `KVXD` binary delta patch.
    [[nodiscard]] static std::string apply_delta_patch(
        std::string_view base_data,
        std::string_view patch_blob
    );

    // Compute zero-copy delta sync between an existing base buffer and a modified target buffer,
    // including live `KVXD` patch serialization and bit-exact reconstruction verification.
    [[nodiscard]] static DeltaSyncSummary compute_delta_sync(
        std::string_view base_data,
        std::string_view updated_data,
        std::string_view src_label = "base_v1",
        std::string_view dst_label = "updated_v2"
    );

    // Execute a real TCP/LAN Cross-Device Zero-Copy Sync session over loopback/LAN sockets:
    // Receiver advertises its existing chunk hash table -> Sender transmits only missing
    // FastCDC chunks (`KVXD` wire stream) -> Receiver reconstructs and verifies the file.
    [[nodiscard]] static LanSyncSessionReport run_lan_sync_session(
        std::string_view receiver_base_data,
        std::string_view sender_updated_data
    );

    // Run Content-Defined Chunking across an entire indexed workspace to measure sub-file deduplication
    [[nodiscard]] static WorkspaceCdcReport deduplicate_workspace(
        const core::WorkspaceIndexSoA& index
    );

    static void render_cdc_report(const WorkspaceCdcReport& rep, bool use_color = true);
    static void render_delta_report(const DeltaSyncSummary& delta, bool use_color = true);
    static void render_lan_sync_report(const LanSyncSessionReport& rep, bool use_color = true);
};

} // namespace kryptos::engine
