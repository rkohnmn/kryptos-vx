// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX (`kryptos_vx` / `kryptos_vx.exe`) — v3.0.0 "Apex" Main CLI Router
// Zero-Bloat C++20 Full-Stack Systems Engine:
//   • Phase 1 "Hyperion" : SoA Indexer, AVX2 SIMD, Context Packer & Smart TV De-Bloater
//   • Phase 2 "Kronos"   : Kernel `getdents64`/`statx`, Multi-ISA SIMD & Trigram Watcher
//   • Phase 3 "Aegis"    : Native JSON-RPC 2.0 MCP Server & 24-Byte Binary ADB Wire Bridge
//   • Phase 4 "Apex"     : FastCDC Rolling Gear Hash Deduplication & PE/ELF/Mach-O Profiler
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/kernel_fs.hpp"
#include "kryptos/core/simd_scanner.hpp"
#include "kryptos/core/soa_index.hpp"
#include "kryptos/engine/adb_bridge.hpp"
#include "kryptos/engine/binary_profiler.hpp"
#include "kryptos/engine/code_auditor.hpp"
#include "kryptos/engine/context_packer.hpp"
#include "kryptos/engine/debloater.hpp"
#include "kryptos/engine/duplicate_finder.hpp"
#include "kryptos/engine/fastcdc_sync.hpp"
#include "kryptos/engine/mcp_server.hpp"
#include "kryptos/engine/trigram_watcher.hpp"
#include "kryptos/memory/raii_handle.hpp"
#include "kryptos/ui/brand_logo.hpp"
#include "kryptos/ui/tui_dashboard.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_help() {
    std::cout << R"(╔══════════════════════════════════════════════════════════════════════════════╗
║  ⚡ KRYPTOS-VX v3.0.0 "Apex" — Full-Stack C++20 Systems & AI Engine          ║
╚══════════════════════════════════════════════════════════════════════════════╝

USAGE:
  kryptos_vx <command> [target_path] [options]

COMMANDS:
  scan [path]                Parallel Kernel-Enumerated SoA scan, token count & security audit
  pack [path]                Pack repository into an LLM-ready Vibe-Coding bundle with secret scrubbing
  debloat [path]             Detect Electron/OS cache bloat & generate Smart TV ADB de-bloat manifests
  adb [host:port]            Execute Smart TV de-bloat over Native C++20 24-Byte Binary ADB Wire Protocol
  mcp [path]                 Launch Native C++20 JSON-RPC 2.0 Model Context Protocol (MCP) Server (<0.5ms)
  cdc [path]                 Run FastCDC 64-Bit Rolling Gear Hash sub-file deduplication & delta sync
  sync [fileA] [fileB]       Execute Cross-Device TCP/LAN Zero-Copy Sync (`KVXD` wire protocol + bit-exact verify)
  binprof <bin1> [bin2]      Profile PE / ELF64 / Mach-O64 binary sections or diff two binaries side-by-side
  search <pattern> [path]    Multi-ISA SIMD (AVX-512 / AVX2 / NEON) ultra-fast substring search
  trigram <pattern> [path]   Sub-millisecond (< 1 ms) 24-bit inverted trigram index query
  watch [path]               Launch real-time OS filesystem watcher daemon (`inotify` / Win32) with < 1 ms updates
  dups [path]                Find duplicate files using 2-stage size + 64-bit SIMD rolling hash
  audit [path]               Run Senior Engineering & Adversarial Security Scorecard
  tui [path]                 Launch interactive ANSI TrueColor Terminal UI dashboard
  logo [--out file.svg]      Render in-app KRYPTOS-VX vector emblem & export embedded SVG icon

OPTIONS:
  --json                     Emit deterministic machine-readable JSON output
  --no-color                 Disable ANSI color escape sequences
  --out <file>               Write output bundle, script, or SVG logo to file
  --max-tokens <N>           Maximum LLM token budget for `pack` (default: 128000)
  --skeleton                 Compress source files to structural signatures only in `pack`
  --profile <tv|samsung|lg|android|desktop|all>
                             Target profile for `debloat` / `adb` (default: all)
  --simulate-tv              Spawn loopback Smart TV ADB daemon to test/verify wire protocol offline
  --tcp-port <port>          Run `mcp` server on `127.0.0.1:<port>` instead of `stdio`
  --oneshot <json>           Execute a single JSON-RPC 2.0 request against `mcp` and print response
  --timeout-ms <N>           Timeout in milliseconds for `watch` or `adb` (default: 2000)
  --threads <N>              Worker thread count (default: hardware_concurrency)
  -h, --help                 Show this help message
  -v, --version              Show version, kernel backend, and hardware SIMD capabilities
)";
}

} // namespace

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);

    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    if (args.empty()) {
        args.emplace_back("scan");
        args.emplace_back(".");
    }

    bool json_output = false;
    bool use_color = true;
    bool skeleton_only = false;
    bool simulate_tv = false;
    uint16_t mcp_tcp_port = 0;
    size_t max_tokens = 128000;
    size_t num_threads = 0;
    uint32_t watch_timeout_ms = 2000;
    std::string out_file;
    std::string profile_str = "all";
    std::string mcp_oneshot_json;
    std::vector<std::string> positional;

    for (size_t i = 0; i < args.size(); ++i) {
        const auto arg = args[i];
        if (arg == "-h" || arg == "--help" || arg == "help") {
            print_help();
            return 0;
        }
        if (arg == "-v" || arg == "--version" || arg == "version") {
            std::cout << "kryptos_vx 3.0.0 \"Apex\" (C++20 | SIMD: "
                      << kryptos::core::SimdScanner::active_isa_name()
                      << " | MCP: 2024-11-05 (7 Tools) | ADB Wire: v1.0 | FastCDC: 64-Bit Gear | BinProf: ELF64/PE/Mach-O | Threads: "
                      << std::thread::hardware_concurrency() << ")\n";
            return 0;
        }
        if (arg == "--json") {
            json_output = true;
        } else if (arg == "--no-color") {
            use_color = false;
        } else if (arg == "--skeleton") {
            skeleton_only = true;
        } else if (arg == "--simulate-tv") {
            simulate_tv = true;
        } else if (arg == "--out" && i + 1 < args.size()) {
            out_file = std::string(args[++i]);
        } else if (arg == "--max-tokens" && i + 1 < args.size()) {
            max_tokens = static_cast<size_t>(std::stoull(std::string(args[++i])));
        } else if (arg == "--tcp-port" && i + 1 < args.size()) {
            mcp_tcp_port = static_cast<uint16_t>(std::stoul(std::string(args[++i])));
        } else if (arg == "--oneshot" && i + 1 < args.size()) {
            mcp_oneshot_json = std::string(args[++i]);
        } else if (arg == "--timeout-ms" && i + 1 < args.size()) {
            watch_timeout_ms = static_cast<uint32_t>(std::stoul(std::string(args[++i])));
        } else if (arg == "--threads" && i + 1 < args.size()) {
            num_threads = static_cast<size_t>(std::stoull(std::string(args[++i])));
        } else if (arg == "--profile" && i + 1 < args.size()) {
            profile_str = std::string(args[++i]);
        } else if (!arg.starts_with("-")) {
            positional.emplace_back(arg);
        }
    }

    const std::string command = positional.empty() ? "scan" : positional[0];

    if (command == "logo" || command == "--logo") {
        kryptos::ui::BrandLogo::render_in_app_banner(
            std::cout, kryptos::core::SimdScanner::has_avx2_support(), use_color
        );
        if (!out_file.empty()) {
            std::ofstream ofs(out_file, std::ios::binary);
            ofs << kryptos::ui::BrandLogo::EMBEDDED_SVG;
            std::cout << "Exported embedded KRYPTOS-VX SVG vector logo to `" << out_file << "`.\n";
        }
        return 0;
    }

    if (command == "mcp") {
        const std::string ws_root = (positional.size() >= 2) ? positional[1] : ".";
        kryptos::engine::McpServer server(ws_root);
        if (!mcp_oneshot_json.empty()) {
            kryptos::engine::McpDispatchTelemetry tel{};
            std::string reply = server.handle_jsonrpc_message(mcp_oneshot_json, &tel);
            std::cout << reply << "\n";
            return 0;
        }
        if (mcp_tcp_port > 0) {
            return server.run_tcp_server(mcp_tcp_port, 0);
        }
        return server.run_stdio_loop(std::cin, std::cout);
    }

    if (command == "adb") {
        kryptos::engine::DeviceProfile prof = kryptos::engine::DeviceProfile::SonyBraviaAndroidTV;
        if (profile_str == "samsung") prof = kryptos::engine::DeviceProfile::SamsungTizenTV;
        else if (profile_str == "lg") prof = kryptos::engine::DeviceProfile::LGWebOSTV;
        else if (profile_str == "all") prof = kryptos::engine::DeviceProfile::All;

        if (simulate_tv || positional.size() < 2) {
            const auto rep = kryptos::engine::AdbBridge::run_simulated_smart_tv_debloat(prof);
            kryptos::engine::AdbBridge::render_session_report(rep, use_color);
            return rep.connected ? 0 : 1;
        }

        const std::string endpoint = positional[1];
        std::string host = endpoint;
        uint16_t port = 5555;
        const auto colon = endpoint.find(':');
        if (colon != std::string::npos) {
            host = endpoint.substr(0, colon);
            port = static_cast<uint16_t>(std::stoul(endpoint.substr(colon + 1)));
        }

        const auto rules = kryptos::engine::Debloater::get_curated_device_rules(prof);
        std::vector<std::string> cmds;
        for (const auto& r : rules) cmds.push_back(r.removal_command);

        const auto rep = kryptos::engine::AdbBridge::execute_over_tcp(host, port, cmds, watch_timeout_ms);
        kryptos::engine::AdbBridge::render_session_report(rep, use_color);
        return rep.connected ? 0 : 1;
    }

    if (command == "sync") {
        if (positional.size() >= 3) {
            kryptos::memory::MappedFile mf1(positional[1]);
            kryptos::memory::MappedFile mf2(positional[2]);
            const auto rep = kryptos::engine::FastCdcSync::run_lan_sync_session(mf1.view(), mf2.view());
            kryptos::engine::FastCdcSync::render_lan_sync_report(rep, use_color);
            return rep.bit_exact_verified ? 0 : 1;
        }
        // Self-contained live TCP/LAN sync demonstration on a shifted binary update
        kryptos::memory::MappedFile mf1("./bin/kryptos_vx");
        std::string base_v1(mf1.view());
        std::string updated_v2 = "APEX_HOTFIX_COMMIT_HEADER_64B_SHIFT_0123456789ABCDEF" + base_v1;
        const auto rep = kryptos::engine::FastCdcSync::run_lan_sync_session(base_v1, updated_v2);
        kryptos::engine::FastCdcSync::render_lan_sync_report(rep, use_color);
        return rep.bit_exact_verified ? 0 : 1;
    }

    if (command == "cdc") {
        if (positional.size() >= 3) {
            // Two-file zero-copy FastCDC delta sync comparison (`kryptos_vx cdc <fileA> <fileB>`)
            kryptos::memory::MappedFile mf1(positional[1]);
            kryptos::memory::MappedFile mf2(positional[2]);
            const auto delta = kryptos::engine::FastCdcSync::compute_delta_sync(
                mf1.view(), mf2.view(), positional[1], positional[2]
            );
            kryptos::engine::FastCdcSync::render_delta_report(delta, use_color);
            return 0;
        }
        const std::string ws_root = (positional.size() >= 2) ? positional[1] : ".";
        kryptos::core::WorkspaceIndexSoA index;
        index.index_directory(ws_root, true, num_threads);
        const auto cdc_rep = kryptos::engine::FastCdcSync::deduplicate_workspace(index);
        kryptos::engine::FastCdcSync::render_cdc_report(cdc_rep, use_color);
        return 0;
    }

    if (command == "binprof") {
        const std::string bin_a = (positional.size() >= 2) ? positional[1] : "./bin/kryptos_vx";
        const auto rep_a = kryptos::engine::BinaryProfiler::profile_file(bin_a);
        kryptos::engine::BinaryProfiler::render_profile_report(rep_a, use_color);

        if (positional.size() >= 3) {
            const std::string bin_b = positional[2];
            const auto rep_b = kryptos::engine::BinaryProfiler::profile_file(bin_b);
            const auto diff = kryptos::engine::BinaryProfiler::diff_binaries(rep_a, rep_b);
            kryptos::engine::BinaryProfiler::render_diff_report(diff, use_color);
        }
        return 0;
    }

    if (command == "search") {
        if (positional.size() < 2) {
            std::cerr << "Error: `kryptos_vx search <pattern> [path]` requires a search pattern.\n";
            return 2;
        }
        const std::string pattern = positional[1];
        const std::string target_path = (positional.size() >= 3) ? positional[2] : ".";

        const auto t0 = std::chrono::steady_clock::now();
        kryptos::core::WorkspaceIndexSoA index;
        index.index_directory(target_path, false, num_threads);

        size_t total_matches = 0;
        size_t matched_files = 0;

        for (size_t i = 0; i < index.size(); ++i) {
            if (index.is_binary[i] != 0) continue;
            kryptos::memory::MappedFile mf(std::string(index.full_path[i]));
            if (!mf.data() || mf.empty()) continue;

            auto offsets = kryptos::core::SimdScanner::find_all_simd(mf.view(), pattern);
            if (!offsets.empty()) {
                ++matched_files;
                total_matches += offsets.size();
                for (size_t off : offsets) {
                    const size_t line_no = 1 + kryptos::core::SimdScanner::count_byte_simd(
                        mf.data(), off, '\n'
                    );
                    std::cout << index.relative_path[i] << ":" << line_no
                              << " (byte " << off << ")\n";
                }
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "\n[" << kryptos::core::SimdScanner::active_isa_name() << " Search] Found "
                  << total_matches << " match(es) across " << matched_files << " file(s) in "
                  << std::fixed << std::setprecision(2) << ms << " ms.\n";
        return 0;
    }

    if (command == "trigram") {
        if (positional.size() < 2) {
            std::cerr << "Error: `kryptos_vx trigram <pattern> [path]` requires a search pattern.\n";
            return 2;
        }
        const std::string pattern = positional[1];
        const std::string target_path = (positional.size() >= 3) ? positional[2] : ".";

        kryptos::core::WorkspaceIndexSoA index;
        index.index_directory(target_path, false, num_threads);

        kryptos::engine::TrigramWatcherEngine trigram_engine;
        trigram_engine.build_from_workspace(index);

        const auto qres = trigram_engine.query(pattern);
        for (const auto& hit : qres.hits) {
            for (size_t j = 0; j < hit.byte_offsets.size(); ++j) {
                std::cout << hit.relative_path << ":" << hit.line_numbers[j]
                          << " (byte " << hit.byte_offsets[j] << ")\n";
            }
        }
        std::cout << "\n[KRONOS/APEX Trigram Index Query]\n"
                  << "  • Trigram Index Build : " << std::fixed << std::setprecision(2)
                  << trigram_engine.initial_build_ms() << " ms ("
                  << trigram_engine.unique_trigram_count() << " unique 24-bit trigrams)\n"
                  << "  • Candidate Pruning   : Evaluated " << qres.candidate_files_evaluated
                  << " / " << qres.total_files_in_index << " files\n"
                  << "  • Query Execution Time: " << std::fixed << std::setprecision(3)
                  << qres.query_latency_ms << " ms (" << qres.total_matches << " matches)\n";
        return 0;
    }

    const std::string target_path = (positional.size() >= 2) ? positional[1] : ".";

    if (command == "watch") {
        kryptos::core::WorkspaceIndexSoA index;
        index.index_directory(target_path, false, num_threads);

        kryptos::engine::TrigramWatcherEngine watcher;
        watcher.build_from_workspace(index);
        return watcher.run_watcher_daemon(target_path, 5, watch_timeout_ms, use_color);
    }

    if (command == "tui") {
        return kryptos::ui::TuiDashboard::run_interactive_tui(target_path);
    }

    kryptos::core::WorkspaceIndexSoA index;
    index.index_directory(target_path, true, num_threads);

    if (command == "pack") {
        kryptos::engine::PackConfig cfg{};
        cfg.max_token_budget = max_tokens;
        cfg.redact_secrets = true;
        cfg.compress_signatures_only = skeleton_only;
        cfg.output_path = out_file.empty() ? "kryptos_vx_context_pack.md" : out_file;

        const auto pack_res = kryptos::engine::ContextPacker::pack_workspace(index, cfg);
        kryptos::ui::TuiDashboard::render_pack_summary(pack_res, cfg.output_path, use_color);
        return 0;
    }

    if (command == "debloat") {
        kryptos::engine::DeviceProfile prof = kryptos::engine::DeviceProfile::All;
        if (profile_str == "samsung") prof = kryptos::engine::DeviceProfile::SamsungTizenTV;
        else if (profile_str == "lg") prof = kryptos::engine::DeviceProfile::LGWebOSTV;
        else if (profile_str == "android" || profile_str == "tv") prof = kryptos::engine::DeviceProfile::SonyBraviaAndroidTV;
        else if (profile_str == "desktop") prof = kryptos::engine::DeviceProfile::DesktopElectronAndOS;

        const auto rep = kryptos::engine::Debloater::analyze_and_generate(index, prof);
        kryptos::ui::TuiDashboard::render_debloat_report(rep, use_color);

        if (!out_file.empty()) {
            std::ofstream ofs(out_file, std::ios::binary);
            ofs << rep.generated_reprogram_script;
            std::cout << "Saved executable reprogramming manifest to `" << out_file << "`.\n";
        }
        return 0;
    }

    if (command == "dups") {
        const auto dup_rep = kryptos::engine::DuplicateFinder::find_duplicates(index);
        std::cout << "\n[KRYPTOS-VX Duplicate & Redundancy Report]\n";
        std::cout << "Redundant Files: " << dup_rep.redundant_file_count
                  << " | Reclaimable Space: "
                  << kryptos::ui::TuiDashboard::format_bytes(dup_rep.total_wasted_bytes) << "\n";
        for (const auto& c : dup_rep.clusters) {
            std::cout << "  • Hash 0x" << std::hex << c.content_hash << std::dec
                      << " (" << kryptos::ui::TuiDashboard::format_bytes(c.file_size_bytes) << " each, "
                      << c.paths.size() << " copies):\n";
            for (const auto& p : c.paths) {
                std::cout << "      - " << p << "\n";
            }
        }
        return 0;
    }

    // Default (`scan` or `audit`)
    const auto scorecard = kryptos::engine::CodeAuditor::evaluate(index);
    const auto dup_report = kryptos::engine::DuplicateFinder::find_duplicates(index);

    if (json_output) {
        std::cout << kryptos::ui::TuiDashboard::format_json_report(index, scorecard, dup_report);
    } else {
        kryptos::ui::TuiDashboard::render_full_dashboard(index, scorecard, dup_report, use_color);
    }

    return 0;
}
