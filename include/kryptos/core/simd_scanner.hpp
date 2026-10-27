#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v1.5 "Kronos" — Multi-ISA SIMD Pattern Scanner (AVX-512 / AVX2 / NEON)
// Compliant with SASMP v1.3.0 / performance v3.0.0
// ═══════════════════════════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kryptos::core {

enum class SimdIsa : uint8_t {
    Scalar = 0,
    ArmNeon128,
    X86Avx2_256,
    X86Avx512_512
};

struct SecretMatch {
    std::string rule_name;
    size_t line_number{1};
    size_t byte_offset{0};
    std::string redacted_snippet;
};

struct ScanMetrics {
    size_t total_lines{0};
    size_t code_lines{0};
    size_t comment_lines{0};
    size_t blank_lines{0};
    size_t estimated_tokens{0};
    double shannon_entropy{0.0};
    uint64_t content_hash{0};
    bool is_binary{false};
};

class SimdScanner {
public:
    // Runtime + compile-time active SIMD ISA detection (AVX-512BW / AVX2 / ARM64 NEON / Scalar)
    [[nodiscard]] static SimdIsa detect_best_isa() noexcept;
    [[nodiscard]] static std::string_view active_isa_name() noexcept;
    [[nodiscard]] static bool has_avx2_support() noexcept;
    [[nodiscard]] static bool has_avx512_support() noexcept;
    [[nodiscard]] static bool has_neon_support() noexcept;

    // Multi-ISA hardware-dispatched substring search (automatically selects AVX-512 -> AVX2 -> NEON -> Scalar)
    [[nodiscard]] static std::vector<size_t> find_all_simd(
        std::string_view haystack,
        std::string_view needle
    );

    // Explicit AVX-512 512-bit (`_mm512_cmpeq_epi8_mask`) implementation
    [[nodiscard]] static std::vector<size_t> find_all_avx512(
        std::string_view haystack,
        std::string_view needle
    );

    // Explicit AVX2 256-bit (`_mm256_cmpeq_epi8`) implementation
    [[nodiscard]] static std::vector<size_t> find_all_avx2(
        std::string_view haystack,
        std::string_view needle
    );

    // Explicit ARM64 NEON 128-bit (`vceqq_u8`) implementation
    [[nodiscard]] static std::vector<size_t> find_all_neon(
        std::string_view haystack,
        std::string_view needle
    );

    // Explicit scalar implementation for verification & A/B benchmarking
    [[nodiscard]] static std::vector<size_t> find_all_scalar(
        std::string_view haystack,
        std::string_view needle
    );

    // Multi-ISA byte counter (dispatches to AVX-512, AVX2, NEON, or Scalar)
    [[nodiscard]] static size_t count_byte_simd(const uint8_t* data, size_t len, uint8_t target) noexcept;
    [[nodiscard]] static size_t count_byte_avx512(const uint8_t* data, size_t len, uint8_t target) noexcept;
    [[nodiscard]] static size_t count_byte_avx2(const uint8_t* data, size_t len, uint8_t target) noexcept;
    [[nodiscard]] static size_t count_byte_neon(const uint8_t* data, size_t len, uint8_t target) noexcept;
    [[nodiscard]] static size_t count_byte_scalar(const uint8_t* data, size_t len, uint8_t target) noexcept;

    // Ultra-fast 64-bit XXH/FNV-inspired unrolled rolling hash
    [[nodiscard]] static uint64_t fast_hash64(const uint8_t* data, size_t len) noexcept;

    // Compute byte-level Shannon Entropy (0.0 to 8.0 bits/byte)
    [[nodiscard]] static double compute_entropy(const uint8_t* data, size_t len) noexcept;

    // Single-pass file analysis: lines, blank/comment/code breakdown, LLM token
    // count (cl100k / o200k heuristic), hash, binary check, and entropy
    [[nodiscard]] static ScanMetrics analyze_buffer(const uint8_t* data, size_t len) noexcept;

    // Scan text buffer for leaked credentials, API keys, private keys, and tokens
    [[nodiscard]] static std::vector<SecretMatch> scan_secrets(std::string_view content);

    // Redact detected secrets in-place for safe AI context packing
    [[nodiscard]] static std::string redact_secrets(std::string_view content, size_t* redaction_count = nullptr);
};

} // namespace kryptos::core
