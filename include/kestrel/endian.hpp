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

inline ParsedAddOrder parse_add_order_simd(const uint8_t* msg_bytes) noexcept {
    __m128i chunk0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(msg_bytes + 11));
    __m128i chunk1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(msg_bytes + 20));

    const __m128i mask_oid = _mm_setr_epi8(7, 6, 5, 4, 3, 2, 1, 0, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i mask_fields = _mm_setr_epi8(3, 2, 1, 0, -1, -1, -1, -1, -1, -1, -1, -1, 15, 14, 13, 12);

    __m128i shuffled_oid = _mm_shuffle_epi8(chunk0, mask_oid);
    __m128i shuffled_fields = _mm_shuffle_epi8(chunk1, mask_fields);

    uint64_t oid;
    uint32_t shares;
    uint32_t price;

    _mm_storel_epi64(reinterpret_cast<__m128i*>(&oid), shuffled_oid);
    shares = static_cast<uint32_t>(_mm_cvtsi128_si32(shuffled_fields));
    price = static_cast<uint32_t>(_mm_extract_epi32(shuffled_fields, 3));
    char side = static_cast<char>(msg_bytes[19]);

    return {oid, shares, price, side};
}

}
