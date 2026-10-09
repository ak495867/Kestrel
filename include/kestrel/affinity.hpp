#pragma once

#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace kestrel {

inline void set_thread_affinity(std::thread& th, size_t core_id) noexcept {
#if defined(_WIN32)
    HANDLE handle = reinterpret_cast<HANDLE>(th.native_handle());
    DWORD_PTR mask = static_cast<DWORD_PTR>(1ULL << core_id);
    SetThreadAffinityMask(handle, mask);
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(th.native_handle(), sizeof(cpu_set_t), &cpuset);
#endif
}

inline void set_current_thread_affinity(size_t core_id) noexcept {
#if defined(_WIN32)
    HANDLE handle = GetCurrentThread();
    DWORD_PTR mask = static_cast<DWORD_PTR>(1ULL << core_id);
    SetThreadAffinityMask(handle, mask);
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

}
