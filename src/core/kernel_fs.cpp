// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v1.5 "Kronos" — Kernel-Level Accelerated Filesystem Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/kernel_fs.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <winioctl.h>
#elif defined(__linux__)
    #include <dirent.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <sys/syscall.h>
    #include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace kryptos::core {

std::string_view KernelFs::backend_name(KernelEnumBackend b) noexcept {
    switch (b) {
        case KernelEnumBackend::LinuxGetdents64Statx:
            return "Linux Kernel SYS_getdents64 + statx(STATX_SIZE)";
        case KernelEnumBackend::WindowsNtfsUsnOrLargeFetch:
            return "Windows NTFS USN Journal + FindFirstFileEx(LARGE_FETCH)";
        case KernelEnumBackend::PortableStdFilesystem:
            return "Portable C++20 std::filesystem";
    }
    return "Portable C++20 std::filesystem";
}

bool KernelFs::probe_io_uring_support() noexcept {
#if defined(__linux__) && defined(__NR_io_uring_setup)
    // Probe whether kernel permits io_uring_setup (returns ENOMEM/EINVAL if enabled, ENOSYS/EPERM if blocked)
    long ret = ::syscall(__NR_io_uring_setup, 0, nullptr);
    if (ret < 0) {
        return (errno == EINVAL || errno == EFAULT || errno == ENOMEM);
    }
    return true;
#else
    return false;
#endif
}

namespace {

bool should_skip_dir(std::string_view name, bool include_bloat_dirs) noexcept {
    if (name == "." || name == ".." || name == ".git" ||
        name == "proc" || name == "sys" || name == "dev") {
        return true;
    }
    if (!include_bloat_dirs &&
        (name == "node_modules" || name == "__pycache__" || name == "build" || name == "target")) {
        return true;
    }
    return false;
}

std::string extract_ext(std::string_view filename) {
    if (filename == "CMakeLists.txt" || filename == "Makefile") {
        return ".cmake";
    }
    const auto dot = filename.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
        return "";
    }
    return std::string(filename.substr(dot));
}

#if defined(__linux__)
struct linux_dirent64_raw {
    uint64_t d_ino;
    int64_t  d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[1];
};
#endif

} // namespace

std::vector<KernelFileEntry> KernelFs::enumerate_std_filesystem(
    const std::string& root_dir,
    bool include_bloat_dirs,
    KernelEnumTelemetry* telemetry_out
) {
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<KernelFileEntry> results;
    results.reserve(2048);

    KernelEnumTelemetry tel{};
    tel.backend = KernelEnumBackend::PortableStdFilesystem;
    tel.io_uring_supported = probe_io_uring_support();

    std::error_code ec;
    const fs::path root_path = fs::canonical(root_dir, ec);
    const fs::path base_path = ec ? fs::path(root_dir) : root_path;

    auto dir_opts = fs::directory_options::skip_permission_denied;
    for (auto it = fs::recursive_directory_iterator(base_path, dir_opts, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto& p = it->path();
        const std::string fname = p.filename().string();

        if (it->is_directory(ec)) {
            ++tel.directories_traversed;
            if (should_skip_dir(fname, include_bloat_dirs)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file(ec)) continue;

        uint64_t sz = it->file_size(ec);
        if (ec) sz = 0;
        ++tel.raw_syscalls_issued;

        std::string rel_str = fs::relative(p, base_path, ec).generic_string();
        if (ec || rel_str.empty()) {
            rel_str = p.filename().generic_string();
        }

        results.push_back(KernelFileEntry{
            p.generic_string(),
            std::move(rel_str),
            extract_ext(fname),
            sz,
            0,
            0
        });
    }

    tel.files_discovered = results.size();
    const auto t1 = std::chrono::steady_clock::now();
    tel.enumeration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (telemetry_out) *telemetry_out = tel;
    return results;
}

std::vector<KernelFileEntry> KernelFs::enumerate_fast(
    const std::string& root_dir,
    bool include_bloat_dirs,
    KernelEnumTelemetry* telemetry_out
) {
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<KernelFileEntry> results;
    results.reserve(4096);

    KernelEnumTelemetry tel{};
    tel.io_uring_supported = probe_io_uring_support();

    std::error_code ec;
    const fs::path canon = fs::canonical(root_dir, ec);
    const std::string base_root = (ec ? fs::path(root_dir) : canon).generic_string();

#if defined(__linux__) && defined(SYS_getdents64)
    tel.backend = KernelEnumBackend::LinuxGetdents64Statx;

    struct DirWorkItem {
        std::string full_dir;
        std::string rel_prefix;
    };
    std::vector<DirWorkItem> stack;
    stack.reserve(256);
    stack.push_back(DirWorkItem{base_root, ""});

    // 64 KB aligned kernel dentries slab buffer to minimize user/kernel context switches
    alignas(64) std::vector<char> dent_buf(65536);

    while (!stack.empty()) {
        DirWorkItem curr = std::move(stack.back());
        stack.pop_back();

        int dir_fd = ::open(curr.full_dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir_fd < 0) continue;
        ++tel.directories_traversed;
        ++tel.raw_syscalls_issued;

        while (true) {
            long nread = ::syscall(SYS_getdents64, dir_fd, dent_buf.data(), dent_buf.size());
            ++tel.raw_syscalls_issued;
            if (nread <= 0) break;

            for (long bpos = 0; bpos < nread;) {
                const auto* d = reinterpret_cast<const linux_dirent64_raw*>(dent_buf.data() + bpos);
                std::string_view name(d->d_name);
                bpos += d->d_reclen;

                if (name == "." || name == "..") continue;

                unsigned char dtype = d->d_type;
                uint64_t fsize = 0;
                uint64_t mtime_ns = 0;
                bool stat_done = false;

                // Use Linux 4.11+ statx syscall relative to open dir_fd (avoids full path resolution in kernel)
                if (dtype == DT_REG || dtype == DT_UNKNOWN) {
                    struct statx stx{};
                    int sret = ::statx(
                        dir_fd,
                        d->d_name,
                        AT_SYMLINK_NOFOLLOW | AT_STATX_DONT_SYNC,
                        STATX_TYPE | STATX_SIZE | STATX_MTIME,
                        &stx
                    );
                    ++tel.raw_syscalls_issued;
                    if (sret == 0) {
                        stat_done = true;
                        fsize = static_cast<uint64_t>(stx.stx_size);
                        mtime_ns = static_cast<uint64_t>(stx.stx_mtime.tv_sec) * 1'000'000'000ULL +
                                   static_cast<uint64_t>(stx.stx_mtime.tv_nsec);
                        if (dtype == DT_UNKNOWN) {
                            if (S_ISDIR(stx.stx_mode)) dtype = DT_DIR;
                            else if (S_ISREG(stx.stx_mode)) dtype = DT_REG;
                        }
                    }
                }

                if (dtype == DT_DIR) {
                    if (!should_skip_dir(name, include_bloat_dirs)) {
                        std::string child_full = curr.full_dir + "/" + std::string(name);
                        std::string child_rel = curr.rel_prefix.empty()
                            ? std::string(name)
                            : (curr.rel_prefix + "/" + std::string(name));
                        stack.push_back(DirWorkItem{std::move(child_full), std::move(child_rel)});
                    }
                } else if (dtype == DT_REG && stat_done) {
                    std::string child_full = curr.full_dir + "/" + std::string(name);
                    std::string child_rel = curr.rel_prefix.empty()
                        ? std::string(name)
                        : (curr.rel_prefix + "/" + std::string(name));
                    results.push_back(KernelFileEntry{
                        std::move(child_full),
                        std::move(child_rel),
                        extract_ext(name),
                        fsize,
                        mtime_ns,
                        d->d_ino
                    });
                }
            }
        }
        ::close(dir_fd);
    }

    tel.files_discovered = results.size();
    const auto t1 = std::chrono::steady_clock::now();
    tel.enumeration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (telemetry_out) *telemetry_out = tel;
    return results;

#elif defined(_WIN32)
    tel.backend = KernelEnumBackend::WindowsNtfsUsnOrLargeFetch;

    // Probe NTFS volume handle for Direct $MFT / USN Journal capability (`FSCTL_QUERY_USN_JOURNAL`)
    HANDLE vol = CreateFileA("\\\\.\\C:", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, 0, nullptr);
    if (vol != INVALID_HANDLE_VALUE) {
        USN_JOURNAL_DATA_V0 usn_data{};
        DWORD bytes_ret = 0;
        if (DeviceIoControl(vol, FSCTL_QUERY_USN_JOURNAL, nullptr, 0,
                            &usn_data, sizeof(usn_data), &bytes_ret, nullptr)) {
            tel.ntfs_usn_journal_accessible = true;
        }
        CloseHandle(vol);
    }

    // Batched kernel directory walk via FindFirstFileExA(FindExInfoBasic, FIND_FIRST_EX_LARGE_FETCH)
    struct DirWorkItem {
        std::string full_dir;
        std::string rel_prefix;
    };
    std::vector<DirWorkItem> stack;
    stack.push_back(DirWorkItem{base_root, ""});

    while (!stack.empty()) {
        DirWorkItem curr = std::move(stack.back());
        stack.pop_back();
        ++tel.directories_traversed;

        std::string pattern = curr.full_dir + "\\*";
        WIN32_FIND_DATAA fdata{};
        HANDLE hFind = FindFirstFileExA(
            pattern.c_str(),
            FindExInfoBasic,
            &fdata,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH
        );
        ++tel.raw_syscalls_issued;
        if (hFind == INVALID_HANDLE_VALUE) continue;

        do {
            std::string_view name(fdata.cFileName);
            if (name == "." || name == "..") continue;

            const bool is_dir = (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool is_reparse = (fdata.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            if (is_reparse) continue;

            std::string child_full = curr.full_dir + "/" + std::string(name);
            std::string child_rel = curr.rel_prefix.empty()
                ? std::string(name)
                : (curr.rel_prefix + "/" + std::string(name));

            if (is_dir) {
                if (!should_skip_dir(name, include_bloat_dirs)) {
                    stack.push_back(DirWorkItem{std::move(child_full), std::move(child_rel)});
                }
            } else {
                uint64_t fsz = (static_cast<uint64_t>(fdata.nFileSizeHigh) << 32) |
                               static_cast<uint64_t>(fdata.nFileSizeLow);
                uint64_t ft = (static_cast<uint64_t>(fdata.ftLastWriteTime.dwHighDateTime) << 32) |
                              static_cast<uint64_t>(fdata.ftLastWriteTime.dwLowDateTime);
                results.push_back(KernelFileEntry{
                    std::move(child_full),
                    std::move(child_rel),
                    extract_ext(name),
                    fsz,
                    ft * 100ULL,
                    0
                });
            }
        } while (FindNextFileA(hFind, &fdata));

        FindClose(hFind);
    }

    tel.files_discovered = results.size();
    const auto t1 = std::chrono::steady_clock::now();
    tel.enumeration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (telemetry_out) *telemetry_out = tel;
    return results;
#else
    return enumerate_std_filesystem(root_dir, include_bloat_dirs, telemetry_out);
#endif
}

} // namespace kryptos::core
