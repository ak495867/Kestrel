#pragma once

#include <cstddef>
#include <atomic>
#include <vector>
#include <new>

namespace kestrel {

#if defined(__cpp_lib_hardware_interference_size)
    constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
    constexpr size_t CACHE_LINE_SIZE = 64;
#endif

template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

public:
    SPSCQueue() : buffer_(new T[Capacity]) {}

    ~SPSCQueue() {
        delete[] buffer_;
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    template <typename... Args>
    bool emplace(Args&&... args) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        if ((current_tail - cached_head_) >= Capacity) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if ((current_tail - cached_head_) >= Capacity) {
                return false;
            }
        }

        buffer_[current_tail & MASK] = T{std::forward<Args>(args)...};
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    bool push(const T& item) noexcept {
        return emplace(item);
    }

    bool pop(T& item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        if (current_head == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (current_head == cached_tail_) {
                return false;
            }
        }

        item = buffer_[current_head & MASK];
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] size_t size() const noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_relaxed);
        return (tail >= head) ? (tail - head) : (Capacity - (head - tail));
    }

private:
    static constexpr size_t MASK = Capacity - 1;

    T* const buffer_;

    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
    alignas(CACHE_LINE_SIZE) size_t cached_head_{0};

    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE_SIZE) size_t cached_tail_{0};
};

}
