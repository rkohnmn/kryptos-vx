#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX — Custom PoolAllocator & Contiguous StringArena
// Compliant with SASMP v1.3.0 / memory-management v3.0.0
// ═══════════════════════════════════════════════════════════════════════════════

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace kryptos::memory {

// ─────────────────────────────────────────────────────────────────────────────
// Fixed-Block PoolAllocator<T> for Zero-Fragmentation Object Reuse
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, size_t BlockSize = 4096>
class PoolAllocator {
    union Block {
        alignas(T) std::array<std::byte, sizeof(T)> data;
        Block* next{nullptr};
    };

    Block* free_list_{nullptr};
    std::vector<std::unique_ptr<Block[]>> blocks_{};
    size_t allocated_count_{0};

public:
    PoolAllocator() = default;
    ~PoolAllocator() = default;

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&&) noexcept = default;
    PoolAllocator& operator=(PoolAllocator&&) noexcept = default;

    [[nodiscard]] T* allocate() {
        if (!free_list_) {
            allocate_block();
        }
        Block* block = free_list_;
        free_list_ = block->next;
        ++allocated_count_;
        return reinterpret_cast<T*>(block->data.data());
    }

    template <typename... Args>
    [[nodiscard]] T* construct(Args&&... args) {
        T* mem = allocate();
        return new (mem) T(std::forward<Args>(args)...);
    }

    void deallocate(T* ptr) noexcept {
        if (!ptr) return;
        Block* block = reinterpret_cast<Block*>(ptr);
        block->next = free_list_;
        free_list_ = block;
        if (allocated_count_ > 0) --allocated_count_;
    }

    void destroy(T* ptr) noexcept {
        if (!ptr) return;
        ptr->~T();
        deallocate(ptr);
    }

    [[nodiscard]] size_t active_allocations() const noexcept { return allocated_count_; }
    [[nodiscard]] size_t block_count() const noexcept { return blocks_.size(); }

private:
    void allocate_block() {
        constexpr size_t count = (BlockSize / sizeof(Block)) > 0 ? (BlockSize / sizeof(Block)) : 64;
        auto new_blocks = std::make_unique<Block[]>(count);
        for (size_t i = 0; i < count - 1; ++i) {
            new_blocks[i].next = &new_blocks[i + 1];
        }
        new_blocks[count - 1].next = free_list_;
        free_list_ = &new_blocks[0];
        blocks_.push_back(std::move(new_blocks));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// High-Locality Contiguous StringArena (Stores paths & identifiers with 0 heap
// overhead per string; returns stable std::string_view slices)
// ─────────────────────────────────────────────────────────────────────────────
class StringArena {
    static constexpr size_t DEFAULT_SLAB_SIZE = 1024 * 1024; // 1 MB slabs
    struct Slab {
        std::unique_ptr<char[]> buffer;
        size_t capacity{0};
        size_t used{0};
    };

    std::vector<Slab> slabs_{};
    mutable std::mutex mutex_{};
    size_t total_bytes_{0};

public:
    StringArena() {
        allocate_slab(DEFAULT_SLAB_SIZE);
    }

    [[nodiscard]] std::string_view intern(std::string_view str) {
        std::lock_guard<std::mutex> lock(mutex_);
        const size_t needed = str.size() + 1;
        if (slabs_.empty() || (slabs_.back().used + needed > slabs_.back().capacity)) {
            const size_t slab_sz = (needed > DEFAULT_SLAB_SIZE) ? (needed * 2) : DEFAULT_SLAB_SIZE;
            allocate_slab(slab_sz);
        }
        Slab& current = slabs_.back();
        char* dest = current.buffer.get() + current.used;
        if (!str.empty()) {
            std::memcpy(dest, str.data(), str.size());
        }
        dest[str.size()] = '\0';
        current.used += needed;
        total_bytes_ += needed;
        return std::string_view(dest, str.size());
    }

    void reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& slab : slabs_) {
            slab.used = 0;
        }
        total_bytes_ = 0;
    }

    [[nodiscard]] size_t total_bytes_allocated() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_bytes_;
    }

    [[nodiscard]] size_t slab_count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return slabs_.size();
    }

private:
    void allocate_slab(size_t capacity) {
        Slab s;
        s.buffer = std::make_unique<char[]>(capacity);
        s.capacity = capacity;
        s.used = 0;
        slabs_.push_back(std::move(s));
    }
};

} // namespace kryptos::memory
