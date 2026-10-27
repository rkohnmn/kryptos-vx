// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX — System, Electron & Smart TV De-Bloater Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/engine/debloater.hpp"

#include <algorithm>
#include <sstream>

namespace kryptos::engine {

std::vector<BloatPackageRule> Debloater::get_curated_device_rules(DeviceProfile profile) {
    std::vector<BloatPackageRule> all_rules = {
        // Samsung Tizen / Smart TV Telemetry & Ad Injectors
        {
            "com.samsung.tv.adservice",
            "Samsung Smart TV (Tizen / SDB)",
            "Home screen banner ad injector & ACR (Automatic Content Recognition) beacon",
            "sdb shell 0 debug_pm uninstall com.samsung.tv.adservice",
            145,
            true
        },
        {
            "com.samsung.tv.bixby.agent",
            "Samsung Smart TV (Tizen / SDB)",
            "Always-on voice telemetry daemon & background wake-word listener",
            "sdb shell 0 debug_pm disable-user com.samsung.tv.bixby.agent",
            210,
            true
        },
        {
            "com.samsung.tv.universalguide",
            "Samsung Smart TV (Tizen / SDB)",
            "Sponsored content carousel & background recommendation poller",
            "sdb shell 0 debug_pm disable-user com.samsung.tv.universalguide",
            180,
            true
        },
        // LG webOS Smart TV Ad & Voice Telemetry
        {
            "com.webos.service.admanager",
            "LG Smart TV (webOS / Ares-CLI)",
            "LG webOS home screen video ad overlay & LivePlus ACR tracker",
            "ares-novacom --run \"luna-send -n 1 palm://com.webos.applicationManager/remove '{\\\"id\\\":\\\"com.webos.service.admanager\\\"}'\"",
            160,
            true
        },
        {
            "com.webos.app.discovery",
            "LG Smart TV (webOS / Ares-CLI)",
            "Heavy background store promotion daemon & auto-play promo banner",
            "ares-novacom --run \"luna-send -n 1 palm://com.webos.applicationManager/disable '{\\\"id\\\":\\\"com.webos.app.discovery\\\"}'\"",
            195,
            true
        },
        // Android TV / Google TV / Sony Bravia / Shield TV (ADB)
        {
            "com.google.android.tvrecommendations",
            "Android TV / Sony Bravia (ADB)",
            "Google TV home screen sponsored launcher & auto-playing video ads",
            "adb shell pm disable-user --user 0 com.google.android.tvrecommendations",
            280,
            true
        },
        {
            "com.sony.dtv.adservice",
            "Android TV / Sony Bravia (ADB)",
            "Bravia Core background telemetry & Samba Interactive TV ACR daemon",
            "adb shell pm uninstall -k --user 0 com.sony.dtv.adservice",
            175,
            true
        },
        {
            "com.interactive.sambatv",
            "Android TV / Smart TVs (ADB)",
            "Samba TV frame-by-frame screen capture ACR tracking service",
            "adb shell pm disable-user --user 0 com.interactive.sambatv",
            220,
            true
        },
        // Desktop Windows 11 / Electron Bloatware & Telemetry
        {
            "Microsoft.WidgetsPlatformRuntime",
            "Windows 11 Desktop (PowerShell)",
            "Background WebView2 MSN news/ads widget host (~350 MB idle RAM)",
            "Get-AppxPackage *WebExperience* | Remove-AppxPackage",
            350,
            true
        },
        {
            "Electron.Chromium.GPUCache",
            "Desktop Electron Apps (Cross-Platform)",
            "Orphaned Chromium Shader/GPUCache & Code Cache folders across AppData/.config",
            "find ~/.config -type d \\( -name 'GPUCache' -o -name 'Code Cache' \\) -exec rm -rf {} +",
            420,
            true
        }
    };

    if (profile == DeviceProfile::All) {
        return all_rules;
    }

    std::vector<BloatPackageRule> filtered;
    for (const auto& r : all_rules) {
        if (profile == DeviceProfile::SamsungTizenTV && r.vendor_platform.find("Samsung") != std::string::npos) {
            filtered.push_back(r);
        } else if (profile == DeviceProfile::LGWebOSTV && r.vendor_platform.find("LG") != std::string::npos) {
            filtered.push_back(r);
        } else if (profile == DeviceProfile::SonyBraviaAndroidTV && r.vendor_platform.find("Android TV") != std::string::npos) {
            filtered.push_back(r);
        } else if (profile == DeviceProfile::DesktopElectronAndOS &&
                   (r.vendor_platform.find("Windows") != std::string::npos ||
                    r.vendor_platform.find("Desktop") != std::string::npos)) {
            filtered.push_back(r);
        }
    }
    return filtered;
}

DebloatReport Debloater::analyze_and_generate(
    const core::WorkspaceIndexSoA& index,
    DeviceProfile profile
) {
    DebloatReport report{};
    report.device_rules = get_curated_device_rules(profile);

    for (const auto& rule : report.device_rules) {
        report.total_estimated_ram_saved_mb += rule.estimated_ram_saved_mb;
    }

    for (size_t i = 0; i < index.size(); ++i) {
        if (index.category[i] == core::FileCategory::BuildOrCacheBloat) {
            std::string reason = "Build artifact / temporary cache";
            const auto rel = index.relative_path[i];
            if (rel.find("node_modules") != std::string_view::npos) {
                reason = "Node.js dependency tree bloat";
            } else if (rel.find("GPUCache") != std::string_view::npos ||
                       rel.find("Code Cache") != std::string_view::npos) {
                reason = "Electron/Chromium redundant cache";
            } else if (rel.find("__pycache__") != std::string_view::npos) {
                reason = "Python bytecode cache";
            }

            report.local_bloat_items.push_back(WorkspaceBloatItem{
                std::string(rel),
                std::move(reason),
                index.size_bytes[i]
            });
            report.total_reclaimable_bytes += index.size_bytes[i];
        }
    }

    std::sort(report.local_bloat_items.begin(), report.local_bloat_items.end(),
              [](const WorkspaceBloatItem& a, const WorkspaceBloatItem& b) {
                  return a.size_bytes > b.size_bytes;
              });

    std::ostringstream script;
    script << "#!/usr/bin/env bash\n";
    script << "# ═══════════════════════════════════════════════════════════════════════════\n";
    script << "# KRYPTOS-VX Audited De-Bloat & Smart TV Reprogramming Manifest\n";
    script << "# Safe-by-default commands (inspect before executing on target device)\n";
    script << "# ═══════════════════════════════════════════════════════════════════════════\n\n";
    for (const auto& r : report.device_rules) {
        script << "# [" << r.vendor_platform << "] " << r.description
               << " (~" << r.estimated_ram_saved_mb << " MB RAM saved)\n";
        script << "# " << r.removal_command << "\n\n";
    }
    report.generated_reprogram_script = script.str();

    return report;
}

} // namespace kryptos::engine
