#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX — AI "Vibe-Coding" Context Packager & Token Budget Engine
// Packs a repository into an LLM-ready prompt bundle with AST/signature
// compression, token budget sorting, and automatic secret redaction.
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/soa_index.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kryptos::engine {

struct PackConfig {
    size_t max_token_budget{128000};
    bool redact_secrets{true};
    bool compress_signatures_only{false};
    bool exclude_tests{false};
    std::string output_path{};
};

struct PackedFileSummary {
    std::string relative_path;
    std::string language;
    uint64_t size_bytes{0};
    uint32_t tokens{0};
    bool compressed{false};
    size_t secrets_redacted{0};
};

struct PackResult {
    std::string formatted_bundle;
    std::vector<PackedFileSummary> included_files;
    std::vector<std::string> skipped_files;
    size_t total_tokens{0};
    size_t total_files_packed{0};
    size_t total_secrets_redacted{0};
    double pack_duration_ms{0.0};
};

class ContextPacker {
public:
    [[nodiscard]] static PackResult pack_workspace(
        const core::WorkspaceIndexSoA& index,
        const PackConfig& config = {}
    );

    // Extracts structural signatures (headers, class/struct/fn declarations)
    // to reduce token footprint by 60-80% when fitting huge repos into context
    [[nodiscard]] static std::string extract_structural_skeleton(std::string_view source_code);
};

} // namespace kryptos::engine
