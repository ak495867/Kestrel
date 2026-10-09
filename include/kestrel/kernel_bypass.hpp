#pragma once

#include "driver_abi.hpp"
#include <string>
#include <stdexcept>
#include <cstring>
#include <immintrin.h>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace kestrel {

class KernelBypassDevice {
public:
    explicit KernelBypassDevice(const std::string& dev_path = "/dev/kestrel_uio") {
#if defined(__linux__)
        fd_ = ::open(dev_path.c_str(), O_RDWR | O_SYNC);
        if (fd_ < 0) {
            is_mock_ = true;
            allocate_mock();
            return;
        }

        void* mapped = ::mmap(nullptr, sizeof(KestrelUserRing), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (mapped == MAP_FAILED) {
            ::close(fd_);
            fd_ = -1;
            is_mock_ = true;
            allocate_mock();
            return;
        }

        ring_ = static_cast<KestrelUserRing*>(mapped);
        is_mock_ = false;
#else
        (void)dev_path;
        is_mock_ = true;
        allocate_mock();
#endif
    }

    ~KernelBypassDevice() {
#if defined(__linux__)
        if (!is_mock_ && ring_) {
            ::munmap(ring_, sizeof(KestrelUserRing));
            ring_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
        if (is_mock_ && mock_storage_) {
            delete[] mock_storage_;
            mock_storage_ = nullptr;
            ring_ = nullptr;
        }
    }

    KernelBypassDevice(const KernelBypassDevice&) = delete;
    KernelBypassDevice& operator=(const KernelBypassDevice&) = delete;

    inline size_t poll_batch(DmaOrderDescriptor* out_desc, size_t max_count) noexcept {
        uint32_t head = cached_head_;
        uint32_t tail = __atomic_load_n(&ring_->tail, __ATOMIC_ACQUIRE);

        size_t available = (tail >= head) ? (tail - head) : (KESTREL_RING_ENTRY_COUNT - head + tail);
        if (available == 0) return 0;

        size_t to_read = (max_count < available) ? max_count : available;
        for (size_t i = 0; i < to_read; ++i) {
            out_desc[i] = ring_->descriptors[(head + i) & KESTREL_RING_ENTRY_MASK];
        }

        cached_head_ = (head + to_read) & KESTREL_RING_ENTRY_MASK;
        __atomic_store_n(&ring_->head, cached_head_, __ATOMIC_RELEASE);
        return to_read;
    }

    inline void mock_inject(const DmaOrderDescriptor& desc) noexcept {
        if (!is_mock_) return;
        uint32_t tail = ring_->tail;
        ring_->descriptors[tail & KESTREL_RING_ENTRY_MASK] = desc;
        __atomic_store_n(&ring_->tail, (tail + 1) & KESTREL_RING_ENTRY_MASK, __ATOMIC_RELEASE);
    }

    [[nodiscard]] bool is_hardware_bound() const noexcept { return !is_mock_; }

private:
    void allocate_mock() {
        mock_storage_ = new uint8_t[sizeof(KestrelUserRing) + 64];
        uintptr_t addr = reinterpret_cast<uintptr_t>(mock_storage_);
        uintptr_t aligned = (addr + 63) & ~63ULL;
        ring_ = reinterpret_cast<KestrelUserRing*>(aligned);
        std::memset(ring_, 0, sizeof(KestrelUserRing));
    }

#if defined(__linux__)
    int fd_{-1};
#endif
    KestrelUserRing* ring_{nullptr};
    uint8_t* mock_storage_{nullptr};
    uint32_t cached_head_{0};
    bool is_mock_{true};
};

}
