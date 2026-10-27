// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v1.5 "Kronos" — Zero-Latency ANSI TrueColor Terminal UI & JSON
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/ui/tui_dashboard.hpp"
#include "kryptos/ui/brand_logo.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <vector>

#if !defined(_WIN32)
    #include <unistd.h>
#endif

namespace kryptos::ui {

namespace {
constexpr const char* C_RESET   = "\033[0m";
constexpr const char* C_BOLD    = "\033[1m";
constexpr const char* C_DIM     = "\033[2m";
constexpr const char* C_CYAN    = "\033[38;5;51m";
constexpr const char* C_GREEN   = "\033[38;5;48m";
constexpr const char* C_YELLOW  = "\033[38;5;220m";
constexpr const char* C_RED     = "\033[38;5;196m";
constexpr const char* C_MAGENTA = "\033[38;5;141m";
constexpr const char* C_BLUE    = "\033[38;5;75m";

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}
} // namespace

std::string TuiDashboard::format_bytes(uint64_t bytes) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        oss << (static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)) << " GB";
    } else if (bytes >= 1024ULL * 1024ULL) {
        oss << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024ULL) {
        oss << (static_cast<double>(bytes) / 1024.0) << " KB";
    } else {
        oss << bytes << " B";
    }
    return oss.str();
}

std::string TuiDashboard::make_bar(double fraction, size_t width) {
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    const size_t filled = static_cast<size_t>(fraction * static_cast<double>(width) + 0.5);
    std::string bar;
    for (size_t i = 0; i < width; ++i) {
        bar += (i < filled) ? "█" : "░";
    }
    return bar;
}

void TuiDashboard::render_full_dashboard(
    const core::WorkspaceIndexSoA& index,
    const engine::QualityScorecard& scorecard,
    const engine::DuplicateReport& dup_report,
    bool use_color
) {
    const char* b   = use_color ? C_BOLD    : "";
    const char* dim = use_color ? C_DIM     : "";
    const char* cy  = use_color ? C_CYAN    : "";
    const char* gr  = use_color ? C_GREEN   : "";
    const char* yl  = use_color ? C_YELLOW  : "";
    const char* rd  = use_color ? C_RED     : "";
    const char* mg  = use_color ? C_MAGENTA : "";
    const char* bl  = use_color ? C_BLUE    : "";
    const char* rst = use_color ? C_RESET   : "";

    std::ostringstream out;

    BrandLogo::render_in_app_banner(out, core::SimdScanner::has_avx2_support(), use_color);

    const double throughput_mbs = (index.scan_duration_ms > 0.0)
        ? ((static_cast<double>(index.total_bytes_indexed) / (1024.0 * 1024.0)) / (index.scan_duration_ms / 1000.0))
        : 0.0;

    out << b << "┌─ [1] KRONOS v1.5 KERNEL TELEMETRY & WORKSPACE SUMMARY ───────────────────────┐" << rst << "\n";
    out << "│  " << b << "Files Indexed   : " << rst << gr << std::setw(12) << std::left << index.size() << rst
        << b << "Total Scan Time : " << rst << cy << std::fixed << std::setprecision(2) << index.scan_duration_ms << " ms" << rst << "\n";
    out << "│  " << b << "Kernel Enum Time: " << rst << cy << std::setw(8) << std::fixed << std::setprecision(2)
        << index.kernel_telemetry.enumeration_ms << " ms " << rst
        << b << "Kernel Syscalls : " << rst << index.kernel_telemetry.raw_syscalls_issued
        << " (" << index.kernel_telemetry.directories_traversed << " dirs)\n";
    out << "│  " << b << "Kernel Engine   : " << rst << gr
        << core::KernelFs::backend_name(index.kernel_telemetry.backend) << rst << "\n";
    out << "│  " << b << "Total Volume    : " << rst << std::setw(12) << std::left << format_bytes(index.total_bytes_indexed)
        << b << "Throughput      : " << rst << gr << std::fixed << std::setprecision(1) << throughput_mbs << " MB/s" << rst << "\n";
    out << "│  " << b << "Total Lines     : " << rst << std::setw(12) << std::left << index.total_lines_indexed
        << b << "LLM Tokens      : " << rst << mg << index.total_tokens_indexed << " tokens (o200k)" << rst << "\n";
    out << "│  " << b << "Bloat Detected  : " << rst << yl << std::setw(12) << std::left << format_bytes(index.bloat_bytes_detected) << rst
        << b << "Duplicate Waste : " << rst << yl << format_bytes(dup_report.total_wasted_bytes)
        << " (" << dup_report.redundant_file_count << " files)" << rst << "\n";
    out << "│  " << b << "StringArena Mem : " << rst << dim << format_bytes(index.string_arena.total_bytes_allocated())
        << " across " << index.string_arena.slab_count() << " contiguous slab(s)" << rst << "\n";
    out << b << "└──────────────────────────────────────────────────────────────────────────────┘" << rst << "\n\n";

    struct LangStat {
        size_t files{0};
        uint64_t bytes{0};
        uint64_t lines{0};
        uint64_t tokens{0};
    };
    std::map<std::string_view, LangStat> lang_map;
    for (size_t i = 0; i < index.size(); ++i) {
        auto& st = lang_map[index.language[i]];
        ++st.files;
        st.bytes += index.size_bytes[i];
        st.lines += index.total_lines[i];
        st.tokens += index.estimated_tokens[i];
    }

    std::vector<std::pair<std::string_view, LangStat>> lang_vec(lang_map.begin(), lang_map.end());
    std::sort(lang_vec.begin(), lang_vec.end(), [](const auto& a, const auto& b) {
        return a.second.bytes > b.second.bytes;
    });

    out << b << "┌─ [2] LANGUAGE & STORAGE TREEMAP (SoA Contiguous Aggregation) ────────────────┐" << rst << "\n";
    out << "│  " << b << std::left << std::setw(15) << "Language"
        << std::right << std::setw(7) << "Files"
        << std::setw(11) << "Lines"
        << std::setw(11) << "Tokens"
        << std::setw(12) << "Size"
        << "   " << std::left << "Share" << rst << "\n";
    out << "├──────────────────────────────────────────────────────────────────────────────┤\n";

    for (const auto& [lang, st] : lang_vec) {
        const double share = (index.total_bytes_indexed > 0)
            ? (static_cast<double>(st.bytes) / static_cast<double>(index.total_bytes_indexed))
            : 0.0;
        out << "│  " << bl << std::left << std::setw(15) << lang << rst
            << std::right << std::setw(7) << st.files
            << std::setw(11) << st.lines
            << std::setw(11) << st.tokens
            << std::setw(12) << format_bytes(st.bytes)
            << "   " << cy << make_bar(share, 14) << rst << " "
            << std::setw(5) << std::fixed << std::setprecision(1) << (share * 100.0) << "%\n";
    }
    out << b << "└──────────────────────────────────────────────────────────────────────────────┘" << rst << "\n\n";

    std::vector<size_t> order(index.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&index](size_t a, size_t b) {
        return index.estimated_tokens[a] > index.estimated_tokens[b];
    });

    out << b << "┌─ [3] TOP AI CONTEXT TOKEN HOTSPOTS (Vibe-Coding Budget Profiler) ────────────┐" << rst << "\n";
    const size_t top_n = std::min<size_t>(5, order.size());
    for (size_t k = 0; k < top_n; ++k) {
        const size_t idx = order[k];
        if (index.estimated_tokens[idx] == 0) break;
        out << "│  " << (k + 1) << ". " << b << std::left << std::setw(42)
            << std::string(index.relative_path[idx]).substr(0, 41) << rst
            << std::right << mg << std::setw(9) << index.estimated_tokens[idx] << " tok" << rst
            << " | " << std::setw(8) << index.total_lines[idx] << " LOC"
            << " | H=" << std::fixed << std::setprecision(2) << index.shannon_entropy[idx] << "\n";
    }
    out << b << "└──────────────────────────────────────────────────────────────────────────────┘" << rst << "\n\n";

    const char* score_col = (scorecard.overall_score >= 85) ? gr : ((scorecard.overall_score >= 65) ? yl : rd);
    out << b << "┌─ [4] SENIOR ENGINEERING & SECURITY SCORECARD ────────────────────────────────┐" << rst << "\n";
    out << "│  " << b << "Overall Grade  : " << score_col << scorecard.overall_score << "/100" << rst
        << "  [Security: " << scorecard.security_score
        << " | Perf: " << scorecard.performance_score
        << " | Maint: " << scorecard.maintainability_score
        << " | Docs: " << scorecard.documentation_score << "]\n";
    for (const auto& f : scorecard.findings) {
        out << "│  • " << f << "\n";
    }
    for (const auto& rec : scorecard.recommendations) {
        out << "│  → " << cy << rec << rst << "\n";
    }
    out << b << "└──────────────────────────────────────────────────────────────────────────────┘" << rst << "\n";

    std::cout << out.str();
}

void TuiDashboard::render_debloat_report(
    const engine::DebloatReport& report,
    bool use_color
) {
    const char* b   = use_color ? C_BOLD  : "";
    const char* cy  = use_color ? C_CYAN  : "";
    const char* gr  = use_color ? C_GREEN : "";
    const char* yl  = use_color ? C_YELLOW: "";
    const char* rst = use_color ? C_RESET : "";

    std::cout << "\n" << cy << b
              << "╔══════════════════════════════════════════════════════════════════════════════╗\n"
              << "║  🧹 KRYPTOS-VX v1.5 DE-BLOATER & SMART TV / OS REPROGRAMMING ENGINE         ║\n"
              << "╚══════════════════════════════════════════════════════════════════════════════╝"
              << rst << "\n\n";

    std::cout << b << "Local Reclaimable Cache/Artifact Bloat : " << rst << yl
              << format_bytes(report.total_reclaimable_bytes) << " ("
              << report.local_bloat_items.size() << " items)" << rst << "\n";
    std::cout << b << "Estimated Idle RAM Saved Across Targets: " << rst << gr
              << "~" << report.total_estimated_ram_saved_mb << " MB" << rst << "\n\n";

    std::cout << b << "Audited Device & OS Telemetry / Ad-Blocker Rules:" << rst << "\n";
    for (const auto& r : report.device_rules) {
        std::cout << "  • " << b << "[" << r.vendor_platform << "] " << cy << r.package_id << rst
                  << " (" << gr << "-" << r.estimated_ram_saved_mb << " MB RAM" << rst << ")\n"
                  << "    Purpose : " << r.description << "\n"
                  << "    Command : `" << r.removal_command << "`\n";
    }
    std::cout << "\n";
}

void TuiDashboard::render_pack_summary(
    const engine::PackResult& pack_res,
    const std::string& output_file,
    bool use_color
) {
    const char* b   = use_color ? C_BOLD  : "";
    const char* cy  = use_color ? C_CYAN  : "";
    const char* gr  = use_color ? C_GREEN : "";
    const char* mg  = use_color ? C_MAGENTA : "";
    const char* rst = use_color ? C_RESET : "";

    std::cout << "\n" << cy << b
              << "╔══════════════════════════════════════════════════════════════════════════════╗\n"
              << "║  📦 KRYPTOS-VX v1.5 VIBE-CODING AI CONTEXT PACKAGER                         ║\n"
              << "╚══════════════════════════════════════════════════════════════════════════════╝"
              << rst << "\n";
    std::cout << "  • " << b << "Files Packed     : " << rst << gr << pack_res.total_files_packed << rst
              << " (Skipped: " << pack_res.skipped_files.size() << ")\n";
    std::cout << "  • " << b << "Total LLM Tokens : " << rst << mg << pack_res.total_tokens << " tokens" << rst << "\n";
    std::cout << "  • " << b << "Secrets Redacted : " << rst << gr << pack_res.total_secrets_redacted << rst << "\n";
    std::cout << "  • " << b << "Pack Latency     : " << rst << cy << std::fixed << std::setprecision(2)
              << pack_res.pack_duration_ms << " ms" << rst << "\n";
    if (!output_file.empty()) {
        std::cout << "  • " << b << "Saved Bundle To  : " << rst << "`" << output_file << "` ("
                  << format_bytes(pack_res.formatted_bundle.size()) << ")\n";
    }
    std::cout << "\n";
}

std::string TuiDashboard::format_json_report(
    const core::WorkspaceIndexSoA& index,
    const engine::QualityScorecard& scorecard,
    const engine::DuplicateReport& dup_report
) {
    std::ostringstream js;
    js << "{\n";
    js << "  \"engine\": \"KRYPTOS-VX v1.5.0 Kronos C++20\",\n";
    js << "  \"active_simd_isa\": \"" << core::SimdScanner::active_isa_name() << "\",\n";
    js << "  \"kernel_enum_backend\": \"" << core::KernelFs::backend_name(index.kernel_telemetry.backend) << "\",\n";
    js << "  \"kernel_enum_ms\": " << std::fixed << std::setprecision(3) << index.kernel_telemetry.enumeration_ms << ",\n";
    js << "  \"kernel_syscalls\": " << index.kernel_telemetry.raw_syscalls_issued << ",\n";
    js << "  \"io_uring_supported\": " << (index.kernel_telemetry.io_uring_supported ? "true" : "false") << ",\n";
    js << "  \"scan_duration_ms\": " << std::fixed << std::setprecision(3) << index.scan_duration_ms << ",\n";
    js << "  \"files_indexed\": " << index.size() << ",\n";
    js << "  \"total_bytes\": " << index.total_bytes_indexed << ",\n";
    js << "  \"total_lines\": " << index.total_lines_indexed << ",\n";
    js << "  \"total_estimated_tokens\": " << index.total_tokens_indexed << ",\n";
    js << "  \"bloat_bytes\": " << index.bloat_bytes_detected << ",\n";
    js << "  \"duplicate_wasted_bytes\": " << dup_report.total_wasted_bytes << ",\n";
    js << "  \"quality_scorecard\": {\n";
    js << "    \"overall\": " << scorecard.overall_score << ",\n";
    js << "    \"security\": " << scorecard.security_score << ",\n";
    js << "    \"performance\": " << scorecard.performance_score << ",\n";
    js << "    \"maintainability\": " << scorecard.maintainability_score << ",\n";
    js << "    \"documentation\": " << scorecard.documentation_score << ",\n";
    js << "    \"leaked_secrets\": " << scorecard.leaked_secrets_count << "\n";
    js << "  },\n";
    js << "  \"findings\": [\n";
    for (size_t i = 0; i < scorecard.findings.size(); ++i) {
        js << "    \"" << json_escape(scorecard.findings[i]) << "\""
           << (i + 1 < scorecard.findings.size() ? "," : "") << "\n";
    }
    js << "  ]\n";
    js << "}\n";
    return js.str();
}

int TuiDashboard::run_interactive_tui(const std::string& target_dir) {
    core::WorkspaceIndexSoA index;
    index.index_directory(target_dir, true);
    const auto scorecard = engine::CodeAuditor::evaluate(index);
    const auto dup_report = engine::DuplicateFinder::find_duplicates(index);

    render_full_dashboard(index, scorecard, dup_report, true);

#if !defined(_WIN32)
    if (!::isatty(STDIN_FILENO)) {
        return 0;
    }
#endif

    std::cout << C_BOLD << "\n[Interactive TUI Commands] " << C_RESET
              << "(d) Debloat & Smart TV View | (p) Vibe-Coding Context Pack Preview | (q) Quit > "
              << std::flush;

    std::string cmd;
    while (std::getline(std::cin, cmd)) {
        if (cmd == "q" || cmd == "quit" || cmd == "exit") {
            break;
        } else if (cmd == "d") {
            auto rep = engine::Debloater::analyze_and_generate(index);
            render_debloat_report(rep, true);
        } else if (cmd == "p") {
            engine::PackConfig cfg{};
            cfg.max_token_budget = 64000;
            auto pack_res = engine::ContextPacker::pack_workspace(index, cfg);
            render_pack_summary(pack_res, "", true);
        } else {
            render_full_dashboard(index, scorecard, dup_report, true);
        }
        std::cout << C_BOLD << "[Interactive TUI Commands] " << C_RESET
                  << "(d) Debloat View | (p) Context Pack Preview | (q) Quit > " << std::flush;
    }
    return 0;
}

} // namespace kryptos::ui
