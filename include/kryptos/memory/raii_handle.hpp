#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX — RAII Resource Management & Zero-Copy Memory-Mapped File Handles
// Compliant with SASMP v1.3.0 / memory-management v3.0.0
// ═══════════════════════════════════════════════════════════════════════════════

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace kryptos::memory {

// ─────────────────────────────────────────────────────────────────────────────
// RAII Wrapper for Standard C FILE* Handles (Explicit Move, Non-Copyable)
// ─────────────────────────────────────────────────────────────────────────────
class FileHandle {
    FILE* handle_{nullptr};

public:
    explicit FileHandle(const char* path, const char* mode)
        : handle_(std::fopen(path, mode)) {
        if (!handle_) {
            throw std::runtime_error(std::string("Failed to open file: ") + path);
        }
    }

    ~FileHandle() noexcept {
        if (handle_) {
            std::fclose(handle_);
        }
    }

    // Non-copyable
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    // Movable
    FileHandle(FileHandle&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}

    FileHandle& operator=(FileHandle&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                std::fclose(handle_);
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] FILE* get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
};

// ─────────────────────────────────────────────────────────────────────────────
// RAII Cross-Platform Zero-Copy Memory-Mapped File View (mmap / MapViewOfFile)
// ─────────────────────────────────────────────────────────────────────────────
class MappedFile {
    const uint8_t* data_{nullptr};
    size_t size_{0};
#if defined(_WIN32)
    HANDLE file_handle_{INVALID_HANDLE_VALUE};
    HANDLE map_handle_{nullptr};
#else
    int fd_{-1};
#endif
    std::vector<uint8_t> fallback_buffer_{};
    bool is_mapped_{false};

public:
    MappedFile() noexcept = default;

    explicit MappedFile(const std::string& path) noexcept {
        open(path);
    }

    ~MappedFile() noexcept {
        close();
    }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    MappedFile(MappedFile&& other) noexcept
        : data_(std::exchange(other.data_, nullptr)),
          size_(std::exchange(other.size_, 0)),
#if defined(_WIN32)
          file_handle_(std::exchange(other.file_handle_, INVALID_HANDLE_VALUE)),
          map_handle_(std::exchange(other.map_handle_, nullptr)),
#else
          fd_(std::exchange(other.fd_, -1)),
#endif
          fallback_buffer_(std::move(other.fallback_buffer_)),
          is_mapped_(std::exchange(other.is_mapped_, false)) {}

    MappedFile& operator=(MappedFile&& other) noexcept {
        if (this != &other) {
            close();
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
#if defined(_WIN32)
            file_handle_ = std::exchange(other.file_handle_, INVALID_HANDLE_VALUE);
            map_handle_ = std::exchange(other.map_handle_, nullptr);
#else
            fd_ = std::exchange(other.fd_, -1);
#endif
            fallback_buffer_ = std::move(other.fallback_buffer_);
            is_mapped_ = std::exchange(other.is_mapped_, false);
        }
        return *this;
    }

    bool open(const std::string& path) noexcept {
        close();
#if defined(_WIN32)
        file_handle_ = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_handle_ == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER file_size{};
        if (!GetFileSizeEx(file_handle_, &file_size)) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            return false;
        }
        size_ = static_cast<size_t>(file_size.QuadPart);
        if (size_ == 0) return true;
        map_handle_ = CreateFileMappingA(file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!map_handle_) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            return false;
        }
        data_ = static_cast<const uint8_t*>(MapViewOfFile(map_handle_, FILE_MAP_READ, 0, 0, 0));
        is_mapped_ = (data_ != nullptr);
        return is_mapped_;
#else
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0) return false;
        struct stat st{};
        if (::fstat(fd_, &st) != 0) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        size_ = static_cast<size_t>(st.st_size);
        if (size_ == 0) {
            ::close(fd_);
            fd_ = -1;
            return true;
        }
        void* mapped = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (mapped != MAP_FAILED) {
            data_ = static_cast<const uint8_t*>(mapped);
            is_mapped_ = true;
            return true;
        }
        // Fallback to standard read if mmap is disallowed on virtual FS
        fallback_buffer_.resize(size_);
        ssize_t bytes_read = ::read(fd_, fallback_buffer_.data(), size_);
        ::close(fd_);
        fd_ = -1;
        if (bytes_read >= 0) {
            size_ = static_cast<size_t>(bytes_read);
            data_ = fallback_buffer_.data();
            return true;
        }
        return false;
#endif
    }

    void close() noexcept {
#if defined(_WIN32)
        if (is_mapped_ && data_) {
            UnmapViewOfFile(data_);
        }
        if (map_handle_) {
            CloseHandle(map_handle_);
            map_handle_ = nullptr;
        }
        if (file_handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (is_mapped_ && data_ && size_ > 0) {
            ::munmap(const_cast<uint8_t*>(data_), size_);
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
        data_ = nullptr;
        size_ = 0;
        is_mapped_ = false;
        fallback_buffer_.clear();
    }

    [[nodiscard]] const uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::string_view view() const noexcept {
        if (!data_ || size_ == 0) return {};
        return std::string_view(reinterpret_cast<const char*>(data_), size_);
    }
};

} // namespace kryptos::memory
