#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v3.0.0 "Apex" — PE / ELF / Mach-O Binary Section Bloat, Symbol
// Table Hotspot & Visual Binary Diff Profiler
//   • Zero-Dependency Native Header, Section & Symbol Table Parsers for:
//       1. Linux / BSD ELF64 (`\x7fELF`, `Elf64_Ehdr`, `Elf64_Shdr`, `Elf64_Sym`)
//       2. Windows PE32 / PE32+ (`MZ` + `PE\0\0`, `IMAGE_SECTION_HEADER`)
//       3. macOS Mach-O 64-Bit (`0xFEEDFACF`, `LC_SEGMENT_64`, `section_64`)
//   • Visual Section Heatmap, Top Symbol Hotspots, Entropy & Binary Diffing
// ═══════════════════════════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kryptos::engine {

enum class BinaryFormat : uint8_t {
    Unknown = 0,
    Elf64,
    WindowsPe,
    MachO64
};

struct BinarySectionInfo {
    std::string name;
    uint64_t virtual_address{0};
    uint64_t virtual_size{0};
    uint64_t file_offset{0};
    uint64_t raw_size{0};
    double share_pct{0.0};
    double shannon_entropy{0.0};
    bool is_debug_or_strip_candidate{false};
};

struct BinarySymbolHotspot {
    std::string symbol_name;
    std::string symbol_type; // "FUNC" or "OBJECT"
    uint64_t virtual_address{0};
    uint64_t size_bytes{0};
};

struct BinaryProfileReport {
    std::string file_path;
    BinaryFormat format{BinaryFormat::Unknown};
    std::string architecture;
    uint64_t total_file_bytes{0};
    uint64_t total_section_bytes{0};
    uint64_t code_section_bytes{0};
    uint64_t rodata_section_bytes{0};
    uint64_t data_section_bytes{0};
    uint64_t strip_reclaimable_bytes{0};
    size_t total_symbols_count{0};
    std::vector<BinarySectionInfo> sections;
    std::vector<BinarySymbolHotspot> top_symbols;
    std::vector<std::string> optimization_hints;
    double parse_ms{0.0};
};

struct SectionDelta {
    std::string section_name;
    uint64_t size_before{0};
    uint64_t size_after{0};
    int64_t delta_bytes{0};
    double delta_pct{0.0};
};

struct BinaryDiffReport {
    std::string path_before;
    std::string path_after;
    uint64_t total_before{0};
    uint64_t total_after{0};
    int64_t total_delta_bytes{0};
    std::vector<SectionDelta> section_deltas;
};

class BinaryProfiler {
public:
    // Profile an ELF64, Windows PE, or Mach-O64 binary buffer in memory
    [[nodiscard]] static BinaryProfileReport profile_buffer(
        const uint8_t* data,
        size_t len,
        std::string_view label = "<memory>"
    );

    // Profile a binary executable or shared library on disk
    [[nodiscard]] static BinaryProfileReport profile_file(const std::string& path);

    // Compute a section-by-section visual binary diff between two binaries
    [[nodiscard]] static BinaryDiffReport diff_binaries(
        const BinaryProfileReport& before,
        const BinaryProfileReport& after
    );

    [[nodiscard]] static std::string_view format_name(BinaryFormat fmt) noexcept;

    static void render_profile_report(const BinaryProfileReport& rep, bool use_color = true);
    static void render_diff_report(const BinaryDiffReport& diff, bool use_color = true);
};

} // namespace kryptos::engine
