#pragma once

#include <cstdint>
#include <bit>

namespace kestrel {

#if defined(_MSC_VER)
    #include <stdlib.h>
    inline uint16_t bswap16(uint16_t x) noexcept { return _byteswap_ushort(x); }
    inline uint32_t bswap32(uint32_t x) noexcept { return _byteswap_ulong(x); }
    inline uint64_t bswap64(uint64_t x) noexcept { return _byteswap_uint64(x); }
#else
    inline uint16_t bswap16(uint16_t x) noexcept { return __builtin_bswap16(x); }
    inline uint32_t bswap32(uint32_t x) noexcept { return __builtin_bswap32(x); }
    inline uint64_t bswap64(uint64_t x) noexcept { return __builtin_bswap64(x); }
#endif

inline uint64_t bswap48(const uint8_t* ptr) noexcept {
    return (static_cast<uint64_t>(ptr[0]) << 40) |
           (static_cast<uint64_t>(ptr[1]) << 32) |
           (static_cast<uint64_t>(ptr[2]) << 24) |
           (static_cast<uint64_t>(ptr[3]) << 16) |
           (static_cast<uint64_t>(ptr[4]) << 8)  |
           (static_cast<uint64_t>(ptr[5]));
}

}
