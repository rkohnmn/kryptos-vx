// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v3.0.0 "Apex" — PE / ELF / Mach-O Binary Section, Symbol Hotspot
// & Visual Diff Profiler Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/engine/binary_profiler.hpp"
#include "kryptos/core/simd_scanner.hpp"
#include "kryptos/memory/raii_handle.hpp"
#include "kryptos/ui/tui_dashboard.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>

namespace kryptos::engine {

namespace {

#pragma pack(push, 1)
struct Elf64EhdrRaw {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64ShdrRaw {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

struct Elf64SymRaw {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

struct PeImageFileHeaderRaw {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct PeSectionHeaderRaw {
    char     Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};

struct MachHeader64Raw {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct MachLoadCommandRaw {
    uint32_t cmd;
    uint32_t cmdsize;
};

struct MachSegmentCommand64Raw {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct MachSection64Raw {
    char     sectname[16];
    char     segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
};
#pragma pack(pop)

bool is_strip_candidate(std::string_view name) noexcept {
    return name.starts_with(".debug") ||
           name == ".symtab" ||
           name == ".strtab" ||
           name == ".comment" ||
           name == ".note.gnu.build-id" ||
           name.find("debug") != std::string_view::npos;
}

void classify_section_bucket(
    std::string_view name,
    uint64_t sz,
    BinaryProfileReport& rep
) noexcept {
    if (name == ".text" || name == "__text" || name == ".init" || name == ".plt") {
        rep.code_section_bytes += sz;
    } else if (name == ".rodata" || name == ".rdata" || name == "__cstring" ||
               name == ".eh_frame" || name == ".eh_frame_hdr") {
        rep.rodata_section_bytes += sz;
    } else if (name == ".data" || name == ".bss" || name == "__data" || name == ".got") {
        rep.data_section_bytes += sz;
    }
}

} // namespace

std::string_view BinaryProfiler::format_name(BinaryFormat fmt) noexcept {
    switch (fmt) {
        case BinaryFormat::Elf64:     return "ELF 64-Bit (Linux/SystemV)";
        case BinaryFormat::WindowsPe: return "Windows PE32/PE64 (COFF)";
        case BinaryFormat::MachO64:   return "macOS Mach-O 64-Bit";
        case BinaryFormat::Unknown:   return "Unknown / Raw Binary";
    }
    return "Unknown / Raw Binary";
}

BinaryProfileReport BinaryProfiler::profile_buffer(
    const uint8_t* data,
    size_t len,
    std::string_view label
) {
    const auto t0 = std::chrono::steady_clock::now();
    BinaryProfileReport rep{};
    rep.file_path = std::string(label);
    rep.total_file_bytes = len;

    if (!data || len < 64) {
        return rep;
    }

    // 1. Check ELF64 (`\x7fELF` and class == 2)
    if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F' && data[4] == 2) {
        rep.format = BinaryFormat::Elf64;
        Elf64EhdrRaw ehdr{};
        std::memcpy(&ehdr, data, sizeof(Elf64EhdrRaw));

        if (ehdr.e_machine == 62) rep.architecture = "x86_64 (AMD64)";
        else if (ehdr.e_machine == 183) rep.architecture = "ARM64 (AArch64)";
        else rep.architecture = "ELF Machine " + std::to_string(ehdr.e_machine);

        if (ehdr.e_shoff > 0 &&
            ehdr.e_shentsize >= sizeof(Elf64ShdrRaw) &&
            ehdr.e_shoff + static_cast<uint64_t>(ehdr.e_shnum) * ehdr.e_shentsize <= len) {

            const char* shstrtab = nullptr;
            size_t shstrtab_len = 0;
            if (ehdr.e_shstrndx < ehdr.e_shnum) {
                Elf64ShdrRaw str_shdr{};
                std::memcpy(&str_shdr, data + ehdr.e_shoff + static_cast<size_t>(ehdr.e_shstrndx) * ehdr.e_shentsize, sizeof(Elf64ShdrRaw));
                if (str_shdr.sh_offset + str_shdr.sh_size <= len) {
                    shstrtab = reinterpret_cast<const char*>(data + str_shdr.sh_offset);
                    shstrtab_len = static_cast<size_t>(str_shdr.sh_size);
                }
            }

            std::vector<BinarySymbolHotspot> all_symbols;

            for (uint16_t i = 0; i < ehdr.e_shnum; ++i) {
                Elf64ShdrRaw shdr{};
                std::memcpy(&shdr, data + ehdr.e_shoff + static_cast<size_t>(i) * ehdr.e_shentsize, sizeof(Elf64ShdrRaw));
                if (shdr.sh_size == 0) continue;

                std::string sname = "<section_" + std::to_string(i) + ">";
                if (shstrtab && shdr.sh_name < shstrtab_len) {
                    size_t max_s = shstrtab_len - shdr.sh_name;
                    size_t slen = ::strnlen(shstrtab + shdr.sh_name, max_s);
                    if (slen > 0) {
                        sname.assign(shstrtab + shdr.sh_name, slen);
                    }
                }

                // Parse ELF64 symbol tables (SHT_SYMTAB == 2, SHT_DYNSYM == 11)
                if ((shdr.sh_type == 2 || shdr.sh_type == 11) &&
                    shdr.sh_link < ehdr.e_shnum &&
                    shdr.sh_offset + shdr.sh_size <= len) {
                    Elf64ShdrRaw sym_str_shdr{};
                    std::memcpy(&sym_str_shdr, data + ehdr.e_shoff + static_cast<size_t>(shdr.sh_link) * ehdr.e_shentsize, sizeof(Elf64ShdrRaw));
                    if (sym_str_shdr.sh_offset + sym_str_shdr.sh_size <= len) {
                        const char* sym_strtab = reinterpret_cast<const char*>(data + sym_str_shdr.sh_offset);
                        const size_t sym_strtab_len = static_cast<size_t>(sym_str_shdr.sh_size);
                        const size_t sym_count = static_cast<size_t>(shdr.sh_size) / sizeof(Elf64SymRaw);

                        for (size_t k = 0; k < sym_count; ++k) {
                            Elf64SymRaw sym{};
                            std::memcpy(&sym, data + shdr.sh_offset + k * sizeof(Elf64SymRaw), sizeof(Elf64SymRaw));
                            ++rep.total_symbols_count;
                            if (sym.st_size == 0 || sym.st_name == 0 || sym.st_name >= sym_strtab_len) continue;

                            const uint8_t st_type = sym.st_info & 0x0F;
                            // STT_OBJECT == 1, STT_FUNC == 2
                            if (st_type != 1 && st_type != 2) continue;

                            size_t max_n = sym_strtab_len - sym.st_name;
                            size_t nlen = ::strnlen(sym_strtab + sym.st_name, max_n);
                            if (nlen == 0) continue;

                            all_symbols.push_back(BinarySymbolHotspot{
                                std::string(sym_strtab + sym.st_name, nlen),
                                (st_type == 2) ? "FUNC" : "OBJECT",
                                sym.st_value,
                                sym.st_size
                            });
                        }
                    }
                }

                // SHT_NOBITS == 8 (.bss occupies virtual size, not disk bytes)
                const uint64_t disk_sz = (shdr.sh_type == 8) ? 0 : shdr.sh_size;
                double ent = 0.0;
                if (disk_sz > 0 && shdr.sh_offset + disk_sz <= len) {
                    ent = core::SimdScanner::compute_entropy(
                        data + shdr.sh_offset,
                        static_cast<size_t>(std::min<uint64_t>(disk_sz, 32768))
                    );
                }

                const bool strip_cand = is_strip_candidate(sname);
                if (strip_cand) {
                    rep.strip_reclaimable_bytes += disk_sz;
                }
                classify_section_bucket(sname, disk_sz, rep);
                rep.total_section_bytes += disk_sz;

                rep.sections.push_back(BinarySectionInfo{
                    std::move(sname),
                    shdr.sh_addr,
                    shdr.sh_size,
                    shdr.sh_offset,
                    disk_sz,
                    0.0,
                    ent,
                    strip_cand
                });
            }

            std::sort(all_symbols.begin(), all_symbols.end(),
                      [](const BinarySymbolHotspot& a, const BinarySymbolHotspot& b) {
                          return a.size_bytes > b.size_bytes;
                      });
            // Deduplicate symbols appearing in both .dynsym and .symtab
            std::vector<BinarySymbolHotspot> unique_top;
            for (auto& sym : all_symbols) {
                bool dup = false;
                for (const auto& existing : unique_top) {
                    if (existing.symbol_name == sym.symbol_name) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    unique_top.push_back(std::move(sym));
                    if (unique_top.size() >= 8) break;
                }
            }
            rep.top_symbols = std::move(unique_top);
        }
    }
    // 2. Check Windows PE32 / PE64 (`MZ` + `PE\0\0`)
    else if (data[0] == 'M' && data[1] == 'Z') {
        uint32_t pe_off = 0;
        std::memcpy(&pe_off, data + 0x3C, sizeof(uint32_t));
        if (pe_off + 4 + sizeof(PeImageFileHeaderRaw) <= len &&
            data[pe_off] == 'P' && data[pe_off + 1] == 'E' &&
            data[pe_off + 2] == 0 && data[pe_off + 3] == 0) {

            rep.format = BinaryFormat::WindowsPe;
            PeImageFileHeaderRaw fhdr{};
            std::memcpy(&fhdr, data + pe_off + 4, sizeof(PeImageFileHeaderRaw));

            if (fhdr.Machine == 0x8664) rep.architecture = "x86_64 (PE32+)";
            else if (fhdr.Machine == 0xAA64) rep.architecture = "ARM64 (PE32+)";
            else if (fhdr.Machine == 0x014C) rep.architecture = "x86 (PE32)";
            else rep.architecture = "PE Machine 0x" + std::to_string(fhdr.Machine);

            rep.total_symbols_count = fhdr.NumberOfSymbols;

            const size_t sec_table_off = pe_off + 4 + sizeof(PeImageFileHeaderRaw) + fhdr.SizeOfOptionalHeader;
            if (sec_table_off + static_cast<size_t>(fhdr.NumberOfSections) * sizeof(PeSectionHeaderRaw) <= len) {
                for (uint16_t i = 0; i < fhdr.NumberOfSections; ++i) {
                    PeSectionHeaderRaw sh{};
                    std::memcpy(&sh, data + sec_table_off + static_cast<size_t>(i) * sizeof(PeSectionHeaderRaw), sizeof(PeSectionHeaderRaw));

                    size_t nlen = ::strnlen(sh.Name, 8);
                    std::string sname(sh.Name, nlen);
                    const uint64_t disk_sz = sh.SizeOfRawData;

                    double ent = 0.0;
                    if (disk_sz > 0 && sh.PointerToRawData + disk_sz <= len) {
                        ent = core::SimdScanner::compute_entropy(
                            data + sh.PointerToRawData,
                            static_cast<size_t>(std::min<uint64_t>(disk_sz, 32768))
                        );
                    }

                    const bool strip_cand = is_strip_candidate(sname);
                    if (strip_cand) rep.strip_reclaimable_bytes += disk_sz;
                    classify_section_bucket(sname, disk_sz, rep);
                    rep.total_section_bytes += disk_sz;

                    rep.sections.push_back(BinarySectionInfo{
                        std::move(sname),
                        sh.VirtualAddress,
                        sh.VirtualSize,
                        sh.PointerToRawData,
                        disk_sz,
                        0.0,
                        ent,
                        strip_cand
                    });
                }
            }
        }
    }
    // 3. Check macOS Mach-O 64-Bit (`0xFEEDFACF`)
    else {
        uint32_t magic = 0;
        std::memcpy(&magic, data, sizeof(uint32_t));
        if (magic == 0xFEEDFACFU && len >= sizeof(MachHeader64Raw)) {
            rep.format = BinaryFormat::MachO64;
            MachHeader64Raw mhdr{};
            std::memcpy(&mhdr, data, sizeof(MachHeader64Raw));
            rep.architecture = (mhdr.cputype == 0x0100000C) ? "ARM64 (Apple Silicon)" : "x86_64 (Mach-O64)";

            size_t cmd_off = sizeof(MachHeader64Raw);
            for (uint32_t c = 0; c < mhdr.ncmds && cmd_off + sizeof(MachLoadCommandRaw) <= len; ++c) {
                MachLoadCommandRaw lcmd{};
                std::memcpy(&lcmd, data + cmd_off, sizeof(MachLoadCommandRaw));
                if (lcmd.cmdsize < sizeof(MachLoadCommandRaw) || cmd_off + lcmd.cmdsize > len) break;

                if (lcmd.cmd == 0x19U && cmd_off + sizeof(MachSegmentCommand64Raw) <= len) {
                    MachSegmentCommand64Raw seg{};
                    std::memcpy(&seg, data + cmd_off, sizeof(MachSegmentCommand64Raw));
                    size_t sec_off = cmd_off + sizeof(MachSegmentCommand64Raw);

                    for (uint32_t s = 0; s < seg.nsects && sec_off + sizeof(MachSection64Raw) <= cmd_off + lcmd.cmdsize; ++s) {
                        MachSection64Raw sec{};
                        std::memcpy(&sec, data + sec_off, sizeof(MachSection64Raw));
                        sec_off += sizeof(MachSection64Raw);

                        std::string sname(sec.sectname, ::strnlen(sec.sectname, 16));
                        const uint64_t disk_sz = sec.size;
                        double ent = 0.0;
                        if (disk_sz > 0 && sec.offset + disk_sz <= len) {
                            ent = core::SimdScanner::compute_entropy(
                                data + sec.offset,
                                static_cast<size_t>(std::min<uint64_t>(disk_sz, 32768))
                            );
                        }
                        const bool strip_cand = is_strip_candidate(sname);
                        if (strip_cand) rep.strip_reclaimable_bytes += disk_sz;
                        classify_section_bucket(sname, disk_sz, rep);
                        rep.total_section_bytes += disk_sz;

                        rep.sections.push_back(BinarySectionInfo{
                            std::move(sname),
                            sec.addr,
                            sec.size,
                            sec.offset,
                            disk_sz,
                            0.0,
                            ent,
                            strip_cand
                        });
                    }
                }
                cmd_off += lcmd.cmdsize;
            }
        }
    }

    std::sort(rep.sections.begin(), rep.sections.end(),
              [](const BinarySectionInfo& a, const BinarySectionInfo& b) {
                  return a.raw_size > b.raw_size;
              });

    const uint64_t denom = (rep.total_file_bytes > 0) ? rep.total_file_bytes : 1;
    for (auto& sec : rep.sections) {
        sec.share_pct = (static_cast<double>(sec.raw_size) * 100.0) / static_cast<double>(denom);
    }

    if (rep.strip_reclaimable_bytes > 4096) {
        rep.optimization_hints.push_back(
            "Symbol/debug tables account for " +
            ui::TuiDashboard::format_bytes(rep.strip_reclaimable_bytes) +
            " (" + std::to_string(static_cast<int>((rep.strip_reclaimable_bytes * 100) / denom)) +
            "% of binary). Run `strip --strip-unneeded` or compile with `-s` for release distribution."
        );
    }
    if (rep.rodata_section_bytes > rep.code_section_bytes) {
        rep.optimization_hints.push_back(
            "Read-only data + unwind tables (`.rodata` / `.eh_frame`) exceed executable `.text` size. Consider `-fno-unwind-tables` or `-flto` if exceptions are unused."
        );
    }
    if (rep.optimization_hints.empty()) {
        rep.optimization_hints.push_back(
            "Binary section distribution is lean and production-optimized."
        );
    }

    const auto t1 = std::chrono::steady_clock::now();
    rep.parse_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return rep;
}

BinaryProfileReport BinaryProfiler::profile_file(const std::string& path) {
    std::string actual_path = path;
    if (!std::filesystem::exists(actual_path)) {
        if (std::filesystem::exists("../" + path)) {
            actual_path = "../" + path;
        } else if (std::filesystem::exists("../../" + path)) {
            actual_path = "../../" + path;
        }
    }
    memory::MappedFile mf(actual_path);
    if (!mf.data() || mf.empty()) {
        BinaryProfileReport empty{};
        empty.file_path = path;
        return empty;
    }
    return profile_buffer(mf.data(), mf.size(), path);
}

BinaryDiffReport BinaryProfiler::diff_binaries(
    const BinaryProfileReport& before,
    const BinaryProfileReport& after
) {
    BinaryDiffReport diff{};
    diff.path_before = before.file_path;
    diff.path_after = after.file_path;
    diff.total_before = before.total_file_bytes;
    diff.total_after = after.total_file_bytes;
    diff.total_delta_bytes = static_cast<int64_t>(after.total_file_bytes) -
                             static_cast<int64_t>(before.total_file_bytes);

    std::map<std::string, std::pair<uint64_t, uint64_t>> sec_map;
    for (const auto& s : before.sections) {
        sec_map[s.name].first += s.raw_size;
    }
    for (const auto& s : after.sections) {
        sec_map[s.name].second += s.raw_size;
    }

    for (const auto& [name, pair_sz] : sec_map) {
        const uint64_t sz_b = pair_sz.first;
        const uint64_t sz_a = pair_sz.second;
        const int64_t delta = static_cast<int64_t>(sz_a) - static_cast<int64_t>(sz_b);
        if (sz_b == 0 && sz_a == 0) continue;

        double pct = 0.0;
        if (sz_b > 0) {
            pct = (static_cast<double>(delta) * 100.0) / static_cast<double>(sz_b);
        } else if (sz_a > 0) {
            pct = 100.0;
        }

        diff.section_deltas.push_back(SectionDelta{
            name,
            sz_b,
            sz_a,
            delta,
            pct
        });
    }

    std::sort(diff.section_deltas.begin(), diff.section_deltas.end(),
              [](const SectionDelta& a, const SectionDelta& b) {
                  return std::abs(a.delta_bytes) > std::abs(b.delta_bytes);
              });

    return diff;
}

void BinaryProfiler::render_profile_report(const BinaryProfileReport& rep, bool use_color) {
    const char* b   = use_color ? "\033[1m"       : "";
    const char* cy  = use_color ? "\033[38;5;51m" : "";
    const char* gr  = use_color ? "\033[38;5;48m" : "";
    const char* yl  = use_color ? "\033[38;5;220m": "";
    const char* bl  = use_color ? "\033[38;5;75m" : "";
    const char* mg  = use_color ? "\033[38;5;141m": "";
    const char* rst = use_color ? "\033[0m"       : "";

    std::cout << "\n" << cy << b
              << "╔══════════════════════════════════════════════════════════════════════════════╗\n"
              << "║  🔬 KRYPTOS-VX v3.0 \"Apex\" — PE / ELF / Mach-O Binary Section Bloat Profiler ║\n"
              << "╚══════════════════════════════════════════════════════════════════════════════╝"
              << rst << "\n"
              << "  • Target Binary      : `" << rep.file_path << "` ("
              << ui::TuiDashboard::format_bytes(rep.total_file_bytes) << ")\n"
              << "  • Format / Arch      : " << gr << format_name(rep.format) << " [" << rep.architecture << "]" << rst << "\n"
              << "  • Executable (`.text`): " << ui::TuiDashboard::format_bytes(rep.code_section_bytes)
              << " | Read-Only/Unwind: " << ui::TuiDashboard::format_bytes(rep.rodata_section_bytes)
              << " | Strip Reclaimable: " << yl << ui::TuiDashboard::format_bytes(rep.strip_reclaimable_bytes) << rst << "\n\n";

    std::cout << b << "  Section Name          Raw Size     Share    Entropy   Visual Distribution" << rst << "\n";
    std::cout << "  ─────────────────────────────────────────────────────────────────────────────\n";

    const size_t top_n = std::min<size_t>(10, rep.sections.size());
    for (size_t i = 0; i < top_n; ++i) {
        const auto& s = rep.sections[i];
        std::cout << "  " << bl << std::left << std::setw(20) << s.name.substr(0, 19) << rst
                  << std::right << std::setw(10) << ui::TuiDashboard::format_bytes(s.raw_size)
                  << std::setw(8) << std::fixed << std::setprecision(1) << s.share_pct << "%"
                  << std::setw(9) << std::setprecision(2) << s.shannon_entropy
                  << "   " << cy << ui::TuiDashboard::make_bar(s.share_pct / 100.0, 16) << rst
                  << (s.is_debug_or_strip_candidate ? (std::string(" ") + yl + "[STRIP]" + rst) : "")
                  << "\n";
    }

    if (!rep.top_symbols.empty()) {
        std::cout << "\n" << b << "  Top Symbol Table Hotspots (`.symtab` / `.dynsym` — "
                  << rep.total_symbols_count << " symbols scanned):" << rst << "\n";
        const size_t top_sym = std::min<size_t>(5, rep.top_symbols.size());
        for (size_t i = 0; i < top_sym; ++i) {
            const auto& sym = rep.top_symbols[i];
            std::cout << "    " << (i + 1) << ". [" << mg << std::left << std::setw(6) << sym.symbol_type << rst << "] "
                      << std::left << std::setw(46) << sym.symbol_name.substr(0, 45)
                      << std::right << std::setw(10) << ui::TuiDashboard::format_bytes(sym.size_bytes) << "\n";
        }
    }

    std::cout << "\n" << b << "  Senior Systems Optimization Insights:" << rst << "\n";
    for (const auto& h : rep.optimization_hints) {
        std::cout << "  → " << cy << h << rst << "\n";
    }
    std::cout << "\n";
}

void BinaryProfiler::render_diff_report(const BinaryDiffReport& diff, bool use_color) {
    const char* b   = use_color ? "\033[1m"       : "";
    const char* cy  = use_color ? "\033[38;5;51m" : "";
    const char* gr  = use_color ? "\033[38;5;48m" : "";
    const char* yl  = use_color ? "\033[38;5;220m": "";
    const char* rst = use_color ? "\033[0m"       : "";

    std::cout << "\n" << cy << b
              << "╔══════════════════════════════════════════════════════════════════════════════╗\n"
              << "║  ⚖️  KRYPTOS-VX v3.0 \"Apex\" — Side-by-Side Visual Binary Section Diff        ║\n"
              << "╚══════════════════════════════════════════════════════════════════════════════╝"
              << rst << "\n"
              << "  • Baseline Binary : `" << diff.path_before << "` (" << ui::TuiDashboard::format_bytes(diff.total_before) << ")\n"
              << "  • Target Binary   : `" << diff.path_after  << "` (" << ui::TuiDashboard::format_bytes(diff.total_after)  << ")\n"
              << "  • Net Size Delta  : " << (diff.total_delta_bytes <= 0 ? gr : yl)
              << (diff.total_delta_bytes > 0 ? "+" : "") << diff.total_delta_bytes << " bytes" << rst << "\n\n";

    std::cout << b << "  Section Name          Before       After        Delta (Bytes)     Delta (%)" << rst << "\n";
    std::cout << "  ─────────────────────────────────────────────────────────────────────────────\n";

    const size_t top_n = std::min<size_t>(10, diff.section_deltas.size());
    for (size_t i = 0; i < top_n; ++i) {
        const auto& d = diff.section_deltas[i];
        const char* col = (d.delta_bytes < 0) ? gr : ((d.delta_bytes > 0) ? yl : rst);
        std::string delta_str = std::to_string(d.delta_bytes);
        if (d.delta_bytes > 0) {
            delta_str.insert(delta_str.begin(), '+');
        }
        std::cout << "  " << std::left << std::setw(20) << d.section_name.substr(0, 19)
                  << std::right << std::setw(10) << ui::TuiDashboard::format_bytes(d.size_before)
                  << std::setw(12) << ui::TuiDashboard::format_bytes(d.size_after)
                  << col << std::setw(15) << delta_str
                  << std::setw(12) << std::fixed << std::setprecision(1) << d.delta_pct << "%" << rst << "\n";
    }
    std::cout << "\n";
}

} // namespace kryptos::engine
