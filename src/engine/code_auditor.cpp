// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX — Adversarial Engineering & Code Quality Auditor Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/engine/code_auditor.hpp"

#include <algorithm>
#include <string>

namespace kryptos::engine {

QualityScorecard CodeAuditor::evaluate(const core::WorkspaceIndexSoA& index) {
    QualityScorecard card{};
    if (index.empty()) return card;

    uint64_t total_code = 0;
    uint64_t total_comments = 0;
    bool has_readme = false;
    bool has_tests = false;
    bool has_ci = false;

    for (size_t i = 0; i < index.size(); ++i) {
        total_code += index.code_lines[i];
        total_comments += index.comment_lines[i];

        const auto rel = index.relative_path[i];
        if (rel.find("README") != std::string_view::npos) has_readme = true;
        if (rel.find("test") != std::string_view::npos) has_tests = true;
        if (rel.find(".github/workflows") != std::string_view::npos ||
            rel.find(".gitlab-ci") != std::string_view::npos) {
            has_ci = true;
        }

        if (index.category[i] == core::FileCategory::SourceCode && index.total_lines[i] > 1200) {
            ++card.oversized_source_files_count;
        }
        if (index.is_binary[i] == 0 && index.shannon_entropy[i] > 6.2 && index.size_bytes[i] > 1024) {
            ++card.high_entropy_files_count;
        }
    }

    card.leaked_secrets_count = index.detected_secrets.size();
    card.comment_to_code_ratio = (total_code > 0)
        ? (static_cast<double>(total_comments) / static_cast<double>(total_code))
        : 0.0;
    card.bloat_ratio = (index.total_bytes_indexed > 0)
        ? (static_cast<double>(index.bloat_bytes_detected) / static_cast<double>(index.total_bytes_indexed))
        : 0.0;

    // 1. Security Score
    if (card.leaked_secrets_count > 0) {
        card.security_score = std::max(0, 100 - static_cast<int>(card.leaked_secrets_count * 25));
        card.findings.push_back(
            "CRITICAL [Security Auditor]: Detected " + std::to_string(card.leaked_secrets_count) +
            " unredacted credential/API key signature(s) in workspace."
        );
        card.recommendations.push_back(
            "Run `kryptos_vx pack --redact` before sharing with LLMs and rotate any exposed keys immediately."
        );
    } else {
        card.findings.push_back(
            "PASS [Security Auditor]: Zero plaintext API keys or private key headers found across indexed files."
        );
    }

    // 2. Performance & Bloat Score
    if (card.bloat_ratio > 0.35) {
        card.performance_score -= 25;
        card.findings.push_back(
            "WARN [Performance Optimizer]: Build/cache bloat accounts for " +
            std::to_string(static_cast<int>(card.bloat_ratio * 100.0)) + "% of workspace bytes."
        );
        card.recommendations.push_back(
            "Run `kryptos_vx debloat` to purge redundant caches and reclaim disk & indexing bandwidth."
        );
    } else {
        card.findings.push_back(
            "PASS [Performance Optimizer]: Low artifact bloat ratio (" +
            std::to_string(static_cast<int>(card.bloat_ratio * 100.0)) + "%)."
        );
    }

    // 3. Maintainability Score
    if (!has_tests) {
        card.maintainability_score -= 25;
        card.findings.push_back("WARN [Saboteur Persona]: No unit test suite detected in repository tree.");
        card.recommendations.push_back("Add automated unit and sanitizer tests under `tests/`.");
    }
    if (!has_ci) {
        card.maintainability_score -= 10;
        card.recommendations.push_back("Add `.github/workflows/ci.yml` for continuous multi-OS verification.");
    }
    if (card.oversized_source_files_count > 0) {
        card.maintainability_score -= std::min(20, static_cast<int>(card.oversized_source_files_count * 5));
        card.findings.push_back(
            "INFO [Code Reviewer]: " + std::to_string(card.oversized_source_files_count) +
            " source file(s) exceed 1,200 lines."
        );
    }

    // 4. Documentation Score
    if (!has_readme) {
        card.documentation_score -= 35;
        card.recommendations.push_back("Create a comprehensive `README.md` with architecture and build steps.");
    }
    if (card.comment_to_code_ratio < 0.05) {
        card.documentation_score -= 15;
        card.recommendations.push_back("Increase inline architectural commentary (current ratio < 5%).");
    }

    card.overall_score = (card.security_score * 30 +
                          card.performance_score * 25 +
                          card.maintainability_score * 25 +
                          card.documentation_score * 20) / 100;

    if (card.recommendations.empty()) {
        card.recommendations.push_back(
            "Workspace meets Senior Engineering production gates (Security, Locality, Testing, CI, Documentation)."
        );
    }

    return card;
}

} // namespace kryptos::engine
