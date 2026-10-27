#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v1.5 "Kronos" — Kernel-Level Accelerated Filesystem Enumerator
//   • Linux 6.x+: Raw `SYS_getdents64` + `statx(STATX_SIZE | STATX_MTIME)` +
//     `io_uring` capability probe
//   • Windows NTFS: Raw Volume `$MFT` / `FSCTL_ENUM_USN_DATA` (`DeviceIoControl`) +
//     `FindFirstFileExW(FindExInfoBasic, FIND_FIRST_EX_LARGE_FETCH)`
//   • Portable Fallback: `std::filesystem` directory iterator
// ═══════════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kryptos::core {

enum class KernelEnumBackend : uint8_t {
    LinuxGetdents64Statx = 0,
    WindowsNtfsUsnOrLargeFetch,
    PortableStdFilesystem
};

struct KernelFileEntry {
    std::string full_path;
    std::string relative_path;
    std::string extension;
    uint64_t size_bytes{0};
    uint64_t mtime_ns{0};
    uint64_t inode{0};
};

struct KernelEnumTelemetry {
    KernelEnumBackend backend{KernelEnumBackend::PortableStdFilesystem};
    bool io_uring_supported{false};
    bool ntfs_usn_journal_accessible{false};
    size_t directories_traversed{0};
    size_t files_discovered{0};
    size_t raw_syscalls_issued{0};
    double enumeration_ms{0.0};
};

class KernelFs {
public:
    // Enumerate directory tree using the fastest kernel-level path available
    [[nodiscard]] static std::vector<KernelFileEntry> enumerate_fast(
        const std::string& root_dir,
        bool include_bloat_dirs,
        KernelEnumTelemetry* telemetry_out = nullptr
    );

    // Baseline std::filesystem enumerator for A/B micro-benchmark comparison
    [[nodiscard]] static std::vector<KernelFileEntry> enumerate_std_filesystem(
        const std::string& root_dir,
        bool include_bloat_dirs,
        KernelEnumTelemetry* telemetry_out = nullptr
    );

    [[nodiscard]] static std::string_view backend_name(KernelEnumBackend b) noexcept;
    [[nodiscard]] static bool probe_io_uring_support() noexcept;
};

} // namespace kryptos::core
