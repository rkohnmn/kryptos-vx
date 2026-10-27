#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX — Zero-Latency ANSI TrueColor Terminal UI (TUI) & JSON Presenter
// Designed by Senior Frontend UI/UX Developer (WCAG AA High-Contrast Compliant)
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/soa_index.hpp"
#include "kryptos/engine/code_auditor.hpp"
#include "kryptos/engine/context_packer.hpp"
#include "kryptos/engine/debloater.hpp"
#include "kryptos/engine/duplicate_finder.hpp"

#include <cstdint>
#include <string>

namespace kryptos::ui {

class TuiDashboard {
public:
    // Render full executive dashboard (Scan Telemetry + Language Breakdown +
    // Top Token Consumers + Security & Quality Scorecard)
    static void render_full_dashboard(
        const core::WorkspaceIndexSoA& index,
        const engine::QualityScorecard& scorecard,
        const engine::DuplicateReport& dup_report,
        bool use_color = true
    );

    // Render De-Bloater & Smart TV Reprogramming report
    static void render_debloat_report(
        const engine::DebloatReport& report,
        bool use_color = true
    );

    // Render AI Context Packager summary
    static void render_pack_summary(
        const engine::PackResult& pack_res,
        const std::string& output_file,
        bool use_color = true
    );

    // Emit machine-readable JSON output for CI/CD and agent pipelines
    [[nodiscard]] static std::string format_json_report(
        const core::WorkspaceIndexSoA& index,
        const engine::QualityScorecard& scorecard,
        const engine::DuplicateReport& dup_report
    );

    // Interactive keyboard-driven TUI loop (falls back cleanly if stdin is not a TTY)
    static int run_interactive_tui(const std::string& target_dir);

    [[nodiscard]] static std::string format_bytes(uint64_t bytes);
    [[nodiscard]] static std::string make_bar(double fraction, size_t width = 22);
};

} // namespace kryptos::ui
