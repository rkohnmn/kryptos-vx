#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX — Adversarial Engineering & Code Quality Auditor
// Inspired by alirezarezvani/claude-skills/engineering-team/skills
// (code-reviewer, adversarial-reviewer, senior-security)
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/soa_index.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kryptos::engine {

struct QualityScorecard {
    int overall_score{100};
    int security_score{100};
    int performance_score{100};
    int maintainability_score{100};
    int documentation_score{100};

    size_t leaked_secrets_count{0};
    size_t high_entropy_files_count{0};
    size_t oversized_source_files_count{0};
    double comment_to_code_ratio{0.0};
    double bloat_ratio{0.0};

    std::vector<std::string> findings;
    std::vector<std::string> recommendations;
};

class CodeAuditor {
public:
    [[nodiscard]] static QualityScorecard evaluate(const core::WorkspaceIndexSoA& index);
};

} // namespace kryptos::engine
