#pragma once

#include <cstdint>
#include <immintrin.h>

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

struct ParsedAddOrder {
    uint64_t order_id;
    uint32_t shares;
    uint32_t price;
    char side;
};

inline ParsedAddOrder parse_add_order_fast(const uint8_t* msg_bytes) noexcept {
#if defined(__MOVBE__) || defined(__AVX2__)
    uint64_t raw_oid;
    uint32_t raw_shares;
    uint32_t raw_price;
    __builtin_memcpy(&raw_oid, msg_bytes + 11, sizeof(uint64_t));
    __builtin_memcpy(&raw_shares, msg_bytes + 20, sizeof(uint32_t));
    __builtin_memcpy(&raw_price, msg_bytes + 32, sizeof(uint32_t));
    return {
        bswap64(raw_oid),
        bswap32(raw_shares),
        bswap32(raw_price),
        static_cast<char>(msg_bytes[19])
    };
#else
    uint64_t raw_oid;
    uint32_t raw_shares;
    uint32_t raw_price;
    __builtin_memcpy(&raw_oid, msg_bytes + 11, sizeof(uint64_t));
    __builtin_memcpy(&raw_shares, msg_bytes + 20, sizeof(uint32_t));
    __builtin_memcpy(&raw_price, msg_bytes + 32, sizeof(uint32_t));
    return {
        bswap64(raw_oid),
        bswap32(raw_shares),
        bswap32(raw_price),
        static_cast<char>(msg_bytes[19])
    };
#endif
}

}
