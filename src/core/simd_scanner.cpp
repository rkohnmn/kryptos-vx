// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v1.5 "Kronos" — Multi-ISA SIMD Pattern Scanner Implementation
// Supports: x86_64 AVX-512BW (64B), x86_64 AVX2 (32B), ARM64 NEON (16B), Scalar
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/simd_scanner.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #include <immintrin.h>
#endif

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON)
    #include <arm_neon.h>
#endif

namespace kryptos::core {

bool SimdScanner::has_avx512_support() noexcept {
#if defined(__AVX512BW__) && defined(__AVX512F__)
    return true;
#elif (defined(__x86_64__) || defined(__i386__)) && defined(__GNUC__)
    return __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512f");
#else
    return false;
#endif
}

bool SimdScanner::has_avx2_support() noexcept {
#if defined(__AVX2__)
    return true;
#elif (defined(__x86_64__) || defined(__i386__)) && defined(__GNUC__)
    return __builtin_cpu_supports("avx2");
#else
    return false;
#endif
}

bool SimdScanner::has_neon_support() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON)
    return true;
#else
    return false;
#endif
}

SimdIsa SimdScanner::detect_best_isa() noexcept {
#if defined(__AVX512BW__) && defined(__AVX512F__)
    return SimdIsa::X86Avx512_512;
#elif defined(__AVX2__)
    return SimdIsa::X86Avx2_256;
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON)
    return SimdIsa::ArmNeon128;
#else
    return SimdIsa::Scalar;
#endif
}

std::string_view SimdScanner::active_isa_name() noexcept {
    switch (detect_best_isa()) {
        case SimdIsa::X86Avx512_512: return "AVX-512BW 512-Bit (64B Lanes)";
        case SimdIsa::X86Avx2_256:   return "AVX2 256-Bit (32B Lanes + AVX-512/NEON Ready)";
        case SimdIsa::ArmNeon128:    return "ARM64 NEON 128-Bit (16B Lanes)";
        case SimdIsa::Scalar:        return "Scalar 64-Bit Unrolled";
    }
    return "Scalar 64-Bit Unrolled";
}

size_t SimdScanner::count_byte_scalar(const uint8_t* data, size_t len, uint8_t target) noexcept {
    size_t count = 0;
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == target) {
            ++count;
        }
    }
    return count;
}

size_t SimdScanner::count_byte_neon(const uint8_t* data, size_t len, uint8_t target) noexcept {
    if (!data || len == 0) return 0;
    size_t count = 0;
    size_t i = 0;

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON)
    const uint8x16_t v_target = vdupq_n_u8(target);
    for (; i + 16 <= len; i += 16) {
        const uint8x16_t chunk = vld1q_u8(data + i);
        const uint8x16_t cmp = vceqq_u8(chunk, v_target);
        // Each matching lane is 0xFF; shift right by 7 to get 1 per match and horizontal add
        const uint8x16_t bits = vshrq_n_u8(cmp, 7);
        count += static_cast<size_t>(vaddvq_u8(bits));
    }
#endif

    for (; i < len; ++i) {
        if (data[i] == target) {
            ++count;
        }
    }
    return count;
}

size_t SimdScanner::count_byte_avx2(const uint8_t* data, size_t len, uint8_t target) noexcept {
    if (!data || len == 0) return 0;
    size_t count = 0;
    size_t i = 0;

#if defined(__AVX2__)
    const __m256i v_target = _mm256_set1_epi8(static_cast<char>(target));
    for (; i + 32 <= len; i += 32) {
        const __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
        const __m256i cmp = _mm256_cmpeq_epi8(chunk, v_target);
        const uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(cmp));
        count += static_cast<size_t>(std::popcount(mask));
    }
#endif

    for (; i < len; ++i) {
        if (data[i] == target) {
            ++count;
        }
    }
    return count;
}

size_t SimdScanner::count_byte_avx512(const uint8_t* data, size_t len, uint8_t target) noexcept {
    if (!data || len == 0) return 0;
    size_t count = 0;
    size_t i = 0;

#if defined(__AVX512BW__) && defined(__AVX512F__)
    const __m512i v_target = _mm512_set1_epi8(static_cast<char>(target));
    for (; i + 64 <= len; i += 64) {
        const __m512i chunk = _mm512_loadu_si512(reinterpret_cast<const void*>(data + i));
        const __mmask64 mask = _mm512_cmpeq_epi8_mask(chunk, v_target);
        count += static_cast<size_t>(std::popcount(static_cast<uint64_t>(mask)));
    }

    for (; i < len; ++i) {
        if (data[i] == target) {
            ++count;
        }
    }
    return count;
#elif defined(__AVX2__)
    return count_byte_avx2(data, len, target);
#else
    return count_byte_scalar(data, len, target);
#endif
}

size_t SimdScanner::count_byte_simd(const uint8_t* data, size_t len, uint8_t target) noexcept {
    switch (detect_best_isa()) {
        case SimdIsa::X86Avx512_512: return count_byte_avx512(data, len, target);
        case SimdIsa::X86Avx2_256:   return count_byte_avx2(data, len, target);
        case SimdIsa::ArmNeon128:    return count_byte_neon(data, len, target);
        case SimdIsa::Scalar:        return count_byte_scalar(data, len, target);
    }
    return count_byte_scalar(data, len, target);
}

std::vector<size_t> SimdScanner::find_all_scalar(
    std::string_view haystack,
    std::string_view needle
) {
    std::vector<size_t> matches;
    if (needle.empty() || haystack.size() < needle.size()) {
        return matches;
    }
    const size_t limit = haystack.size() - needle.size();
    const char first = needle[0];
    const char last = needle[needle.size() - 1];
    const size_t nlen = needle.size();

    for (size_t i = 0; i <= limit; ++i) {
        if (haystack[i] == first && haystack[i + nlen - 1] == last) {
            if (nlen <= 2 || std::memcmp(haystack.data() + i + 1, needle.data() + 1, nlen - 2) == 0) {
                matches.push_back(i);
            }
        }
    }
    return matches;
}

std::vector<size_t> SimdScanner::find_all_neon(
    std::string_view haystack,
    std::string_view needle
) {
    std::vector<size_t> matches;
    if (needle.empty() || haystack.size() < needle.size()) {
        return matches;
    }
    const size_t nlen = needle.size();
    const size_t limit = haystack.size() - nlen;
    const char* hdata = haystack.data();
    const char* ndata = needle.data();
    size_t i = 0;

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON)
    const uint8x16_t v_first = vdupq_n_u8(static_cast<uint8_t>(ndata[0]));
    const uint8x16_t v_last  = vdupq_n_u8(static_cast<uint8_t>(ndata[nlen - 1]));

    for (; i + 16 <= limit; i += 16) {
        const uint8x16_t b_first = vld1q_u8(reinterpret_cast<const uint8_t*>(hdata + i));
        const uint8x16_t b_last  = vld1q_u8(reinterpret_cast<const uint8_t*>(hdata + i + nlen - 1));
        const uint8x16_t cmp = vandq_u8(vceqq_u8(b_first, v_first), vceqq_u8(b_last, v_last));

        if (vmaxvq_u8(cmp) != 0) {
            alignas(16) uint8_t lanes[16];
            vst1q_u8(lanes, cmp);
            for (size_t lane = 0; lane < 16; ++lane) {
                if (lanes[lane] != 0) {
                    const size_t cand = i + lane;
                    if (nlen <= 2 || std::memcmp(hdata + cand + 1, ndata + 1, nlen - 2) == 0) {
                        matches.push_back(cand);
                    }
                }
            }
        }
    }
#endif

    for (; i <= limit; ++i) {
        if (hdata[i] == ndata[0] && hdata[i + nlen - 1] == ndata[nlen - 1]) {
            if (nlen <= 2 || std::memcmp(hdata + i + 1, ndata + 1, nlen - 2) == 0) {
                matches.push_back(i);
            }
        }
    }
    return matches;
}

std::vector<size_t> SimdScanner::find_all_avx2(
    std::string_view haystack,
    std::string_view needle
) {
    std::vector<size_t> matches;
    if (needle.empty() || haystack.size() < needle.size()) {
        return matches;
    }

    const size_t nlen = needle.size();
    const size_t limit = haystack.size() - nlen;
    const char* hdata = haystack.data();
    const char* ndata = needle.data();
    size_t i = 0;

#if defined(__AVX2__)
    const __m256i v_first = _mm256_set1_epi8(ndata[0]);
    const __m256i v_last  = _mm256_set1_epi8(ndata[nlen - 1]);

    for (; i + 32 <= limit; i += 32) {
        const __m256i block_first = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(hdata + i));
        const __m256i block_last  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(hdata + i + nlen - 1));

        const __m256i eq_first = _mm256_cmpeq_epi8(block_first, v_first);
        const __m256i eq_last  = _mm256_cmpeq_epi8(block_last, v_last);
        uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last)));

        while (mask != 0) {
            const int bit_pos = std::countr_zero(mask);
            const size_t candidate = i + static_cast<size_t>(bit_pos);
            if (nlen <= 2 || std::memcmp(hdata + candidate + 1, ndata + 1, nlen - 2) == 0) {
                matches.push_back(candidate);
            }
            mask &= (mask - 1);
        }
    }
#endif

    for (; i <= limit; ++i) {
        if (hdata[i] == ndata[0] && hdata[i + nlen - 1] == ndata[nlen - 1]) {
            if (nlen <= 2 || std::memcmp(hdata + i + 1, ndata + 1, nlen - 2) == 0) {
                matches.push_back(i);
            }
        }
    }

    return matches;
}

std::vector<size_t> SimdScanner::find_all_avx512(
    std::string_view haystack,
    std::string_view needle
) {
    std::vector<size_t> matches;
    if (needle.empty() || haystack.size() < needle.size()) {
        return matches;
    }

#if defined(__AVX512BW__) && defined(__AVX512F__)
    const size_t nlen = needle.size();
    const size_t limit = haystack.size() - nlen;
    const char* hdata = haystack.data();
    const char* ndata = needle.data();
    size_t i = 0;

    const __m512i v_first = _mm512_set1_epi8(ndata[0]);
    const __m512i v_last  = _mm512_set1_epi8(ndata[nlen - 1]);

    for (; i + 64 <= limit; i += 64) {
        const __m512i block_first = _mm512_loadu_si512(reinterpret_cast<const void*>(hdata + i));
        const __m512i block_last  = _mm512_loadu_si512(reinterpret_cast<const void*>(hdata + i + nlen - 1));

        const __mmask64 eq_first = _mm512_cmpeq_epi8_mask(block_first, v_first);
        const __mmask64 eq_last  = _mm512_cmpeq_epi8_mask(block_last, v_last);
        uint64_t mask = static_cast<uint64_t>(eq_first & eq_last);

        while (mask != 0) {
            const int bit_pos = static_cast<int>(std::countr_zero(mask));
            const size_t candidate = i + static_cast<size_t>(bit_pos);
            if (nlen <= 2 || std::memcmp(hdata + candidate + 1, ndata + 1, nlen - 2) == 0) {
                matches.push_back(candidate);
            }
            mask &= (mask - 1);
        }
    }

    for (; i <= limit; ++i) {
        if (hdata[i] == ndata[0] && hdata[i + nlen - 1] == ndata[nlen - 1]) {
            if (nlen <= 2 || std::memcmp(hdata + i + 1, ndata + 1, nlen - 2) == 0) {
                matches.push_back(i);
            }
        }
    }
    return matches;
#else
    return find_all_avx2(haystack, needle);
#endif
}

std::vector<size_t> SimdScanner::find_all_simd(
    std::string_view haystack,
    std::string_view needle
) {
    switch (detect_best_isa()) {
        case SimdIsa::X86Avx512_512: return find_all_avx512(haystack, needle);
        case SimdIsa::X86Avx2_256:   return find_all_avx2(haystack, needle);
        case SimdIsa::ArmNeon128:    return find_all_neon(haystack, needle);
        case SimdIsa::Scalar:        return find_all_scalar(haystack, needle);
    }
    return find_all_scalar(haystack, needle);
}

uint64_t SimdScanner::fast_hash64(const uint8_t* data, size_t len) noexcept {
    constexpr uint64_t PRIME1 = 11400714785074694791ULL;
    constexpr uint64_t PRIME2 = 14029467366897019727ULL;
    constexpr uint64_t PRIME3 = 1609587929392839161ULL;
    constexpr uint64_t PRIME5 = 2870177450012600261ULL;

    uint64_t h64 = PRIME5 + len;
    size_t i = 0;

    for (; i + 8 <= len; i += 8) {
        uint64_t k1 = 0;
        std::memcpy(&k1, data + i, sizeof(uint64_t));
        k1 *= PRIME2;
        k1 = (k1 << 31) | (k1 >> 33);
        k1 *= PRIME1;
        h64 ^= k1;
        h64 = ((h64 << 27) | (h64 >> 37)) * PRIME1 + PRIME3;
    }

    for (; i < len; ++i) {
        h64 ^= static_cast<uint64_t>(data[i]) * PRIME5;
        h64 = ((h64 << 11) | (h64 >> 53)) * PRIME1;
    }

    h64 ^= h64 >> 33;
    h64 *= PRIME2;
    h64 ^= h64 >> 29;
    h64 *= PRIME3;
    h64 ^= h64 >> 32;
    return h64;
}

double SimdScanner::compute_entropy(const uint8_t* data, size_t len) noexcept {
    if (!data || len == 0) return 0.0;
    std::array<uint64_t, 256> freq{};
    for (size_t i = 0; i < len; ++i) {
        ++freq[data[i]];
    }
    double entropy = 0.0;
    const double inv_len = 1.0 / static_cast<double>(len);
    for (uint64_t count : freq) {
        if (count > 0) {
            const double p = static_cast<double>(count) * inv_len;
            entropy -= p * std::log2(p);
        }
    }
    return entropy;
}

ScanMetrics SimdScanner::analyze_buffer(const uint8_t* data, size_t len) noexcept {
    ScanMetrics m{};
    if (!data || len == 0) return m;

    const size_t check_len = std::min<size_t>(len, 512);
    for (size_t i = 0; i < check_len; ++i) {
        if (data[i] == 0) {
            m.is_binary = true;
            break;
        }
    }

    m.content_hash = fast_hash64(data, len);
    m.shannon_entropy = compute_entropy(data, std::min<size_t>(len, 65536));

    if (m.is_binary) {
        return m;
    }

    size_t line_start = 0;
    size_t word_transitions = 0;
    size_t punctuation_tokens = 0;
    bool in_word = false;

    for (size_t i = 0; i <= len; ++i) {
        const char c = (i < len) ? static_cast<char>(data[i]) : '\n';
        if (i < len) {
            const bool is_alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') || c == '_';
            if (is_alnum) {
                if (!in_word) {
                    ++word_transitions;
                    in_word = true;
                }
            } else {
                in_word = false;
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                    ++punctuation_tokens;
                }
            }
        }

        if (c == '\n') {
            ++m.total_lines;
            size_t p = line_start;
            while (p < i && (data[p] == ' ' || data[p] == '\t' || data[p] == '\r')) {
                ++p;
            }
            if (p >= i) {
                ++m.blank_lines;
            } else {
                const char c1 = static_cast<char>(data[p]);
                const char c2 = (p + 1 < i) ? static_cast<char>(data[p + 1]) : '\0';
                if (c1 == '#' || (c1 == '/' && (c2 == '/' || c2 == '*')) || (c1 == '*' && c2 != '=')) {
                    ++m.comment_lines;
                } else {
                    ++m.code_lines;
                }
            }
            line_start = i + 1;
        }
    }

    m.estimated_tokens = static_cast<size_t>(
        static_cast<double>(word_transitions) * 1.32 +
        static_cast<double>(punctuation_tokens) * 0.55
    );
    if (m.estimated_tokens == 0 && len > 0) {
        m.estimated_tokens = (len + 3) / 4;
    }

    return m;
}

namespace {
struct SecretSignature {
    const char* name;
    const char* prefix_part1;
    const char* prefix_part2;
    size_t min_token_len;
};

constexpr std::array<SecretSignature, 8> SECRET_SIGNATURES{{
    {"OpenAI API Key",        "sk-",         "proj-",              24},
    {"Anthropic API Key",     "sk-ant-",     "api",                28},
    {"AWS Access Key ID",     "AK",          "IA",                 20},
    {"GitHub Personal Token", "gh",          "p_",                 36},
    {"GitHub Fine-Grained",   "github_",     "pat_",               36},
    {"Google AI / GCP Key",   "AIza",        "Sy",                 35},
    {"Slack Bot/User Token",  "xox",         "b-",                 24},
    {"Private Key Header",    "-----BEGIN ", "PRIVATE KEY-----",   27}
}};

bool is_token_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}
} // namespace

std::vector<SecretMatch> SimdScanner::scan_secrets(std::string_view content) {
    std::vector<SecretMatch> results;
    if (content.empty()) return results;

    for (const auto& sig : SECRET_SIGNATURES) {
        const std::string prefix = std::string(sig.prefix_part1) + sig.prefix_part2;
        auto positions = find_all_simd(content, prefix);
        for (size_t pos : positions) {
            size_t end_pos = pos + prefix.size();
            if (std::string_view(sig.name) != "Private Key Header") {
                while (end_pos < content.size() && is_token_char(content[end_pos])) {
                    ++end_pos;
                }
            }
            const size_t token_len = end_pos - pos;
            if (token_len >= sig.min_token_len) {
                const size_t line_no = 1 + count_byte_simd(
                    reinterpret_cast<const uint8_t*>(content.data()), pos, '\n'
                );
                std::string snippet(content.substr(pos, std::min<size_t>(token_len, 10)));
                snippet += "...[REDACTED]";
                results.push_back(SecretMatch{
                    sig.name,
                    line_no,
                    pos,
                    std::move(snippet)
                });
            }
        }
    }
    return results;
}

std::string SimdScanner::redact_secrets(std::string_view content, size_t* redaction_count) {
    std::string out(content);
    size_t count = 0;

    for (const auto& sig : SECRET_SIGNATURES) {
        const std::string prefix = std::string(sig.prefix_part1) + sig.prefix_part2;
        auto positions = find_all_simd(out, prefix);
        for (size_t pos : positions) {
            size_t end_pos = pos + prefix.size();
            if (std::string_view(sig.name) != "Private Key Header") {
                while (end_pos < out.size() && is_token_char(out[end_pos])) {
                    ++end_pos;
                }
            }
            const size_t token_len = end_pos - pos;
            if (token_len >= sig.min_token_len) {
                const size_t keep = std::min<size_t>(prefix.size(), 6);
                for (size_t k = pos + keep; k < end_pos; ++k) {
                    out[k] = '*';
                }
                ++count;
            }
        }
    }
    if (redaction_count) {
        *redaction_count = count;
    }
    return out;
}

} // namespace kryptos::core
