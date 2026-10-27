#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX — System, Electron & Smart TV De-Bloater / Reprogramming Engine
// Identifies bloated Electron/Chromium caches, orphaned build artifacts, and
// generates audited Smart TV ADB & OS telemetry removal scripts.
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/soa_index.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kryptos::engine {

enum class DeviceProfile : uint8_t {
    All = 0,
    SamsungTizenTV,
    LGWebOSTV,
    SonyBraviaAndroidTV,
    DesktopElectronAndOS
};

struct BloatPackageRule {
    std::string package_id;
    std::string vendor_platform;
    std::string description;
    std::string removal_command;
    uint32_t estimated_ram_saved_mb{0};
    bool safe_to_remove{true};
};

struct WorkspaceBloatItem {
    std::string relative_path;
    std::string reason;
    uint64_t size_bytes{0};
};

struct DebloatReport {
    std::vector<WorkspaceBloatItem> local_bloat_items;
    std::vector<BloatPackageRule> device_rules;
    uint64_t total_reclaimable_bytes{0};
    uint32_t total_estimated_ram_saved_mb{0};
    std::string generated_reprogram_script;
};

class Debloater {
public:
    [[nodiscard]] static DebloatReport analyze_and_generate(
        const core::WorkspaceIndexSoA& index,
        DeviceProfile profile = DeviceProfile::All
    );

    [[nodiscard]] static std::vector<BloatPackageRule> get_curated_device_rules(
        DeviceProfile profile = DeviceProfile::All
    );
};

} // namespace kryptos::engine
