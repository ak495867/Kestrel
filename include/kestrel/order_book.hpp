#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <iostream>
#include <iomanip>
#include <string>
#include <algorithm>
#include <cstring>
#include <immintrin.h>

namespace kestrel {

class OrderBook {
public:
    static constexpr size_t MAP_CAPACITY = 4194304;
    static constexpr size_t MAP_MASK = MAP_CAPACITY - 1;
    static constexpr uint64_t EMPTY_KEY = 0;
    static constexpr uint64_t DELETED_KEY = 0xFFFFFFFFFFFFFFFFULL;
    static constexpr size_t NUM_BUCKETS = 16384;
    static constexpr size_t NUM_SUMMARY = 256;

    static inline size_t hash_order_id(uint64_t order_id) noexcept {
        return (order_id ^ (order_id >> 16)) & MAP_MASK;
    }

    void reset() noexcept {
        active_orders_ = 0;
        best_bid_tick_ = 0;
        best_ask_tick_ = 0xFFFFFFFF;
        std::memset(keys_.get(), 0, MAP_CAPACITY * sizeof(uint64_t));
        std::memset(shares_.get(), 0, MAP_CAPACITY * sizeof(uint32_t));
        std::memset(prices_.get(), 0, MAP_CAPACITY * sizeof(uint32_t));
        std::memset(bid_levels_.get(), 0, 500000 * sizeof(uint64_t));
        std::memset(ask_levels_.get(), 0, 500000 * sizeof(uint64_t));
        std::memset(bid_bitmap_.get(), 0, NUM_BUCKETS * sizeof(uint64_t));
        std::memset(ask_bitmap_.get(), 0, NUM_BUCKETS * sizeof(uint64_t));
        std::memset(bid_summary_.get(), 0, NUM_SUMMARY * sizeof(uint64_t));
        std::memset(ask_summary_.get(), 0, NUM_SUMMARY * sizeof(uint64_t));
    }

    OrderBook() {
        keys_ = std::make_unique<uint64_t[]>(MAP_CAPACITY);
        shares_ = std::make_unique<uint32_t[]>(MAP_CAPACITY);
        prices_ = std::make_unique<uint32_t[]>(MAP_CAPACITY);
        bid_levels_ = std::make_unique<uint64_t[]>(500000);
        ask_levels_ = std::make_unique<uint64_t[]>(500000);
        bid_bitmap_ = std::make_unique<uint64_t[]>(NUM_BUCKETS);
        ask_bitmap_ = std::make_unique<uint64_t[]>(NUM_BUCKETS);
        bid_summary_ = std::make_unique<uint64_t[]>(NUM_SUMMARY);
        ask_summary_ = std::make_unique<uint64_t[]>(NUM_SUMMARY);
        reset();
    }

    inline void add_order(uint64_t order_id, char side, uint32_t shares, uint32_t price) noexcept {
        size_t idx = hash_order_id(order_id);
        uint32_t packed_price = (price & 0x7FFFFFFF) | (static_cast<uint32_t>(side == 'B') << 31);

        uint64_t* k_ptr = keys_.get();
        uint32_t* s_ptr = shares_.get();
        uint32_t* p_ptr = prices_.get();

        uint64_t k0 = k_ptr[idx];
        if (__builtin_expect(k0 == EMPTY_KEY, 1)) {
            k_ptr[idx] = order_id;
            s_ptr[idx] = shares;
            p_ptr[idx] = packed_price;
            active_orders_++;
        } else if (__builtin_expect(k0 == order_id, 0)) {
            uint32_t old_raw = p_ptr[idx];
            char old_side = (old_raw >> 31) ? 'B' : 'S';
            uint32_t old_tick = (old_raw & 0x7FFFFFFF) / 100;
            reduce_level(old_side, old_tick, s_ptr[idx]);
            s_ptr[idx] = shares;
            p_ptr[idx] = packed_price;
        } else {
            size_t first_deleted = (k0 == DELETED_KEY) ? idx : MAP_CAPACITY;
            idx = (idx + 1) & MAP_MASK;
            while (true) {
                uint64_t k = k_ptr[idx];
                if (k == EMPTY_KEY) {
                    if (first_deleted != MAP_CAPACITY) idx = first_deleted;
                    k_ptr[idx] = order_id;
                    s_ptr[idx] = shares;
                    p_ptr[idx] = packed_price;
                    active_orders_++;
                    break;
                } else if (k == DELETED_KEY) {
                    if (first_deleted == MAP_CAPACITY) first_deleted = idx;
                } else if (k == order_id) {
                    uint32_t old_raw = p_ptr[idx];
                    char old_side = (old_raw >> 31) ? 'B' : 'S';
                    uint32_t old_tick = (old_raw & 0x7FFFFFFF) / 100;
                    reduce_level(old_side, old_tick, s_ptr[idx]);
                    s_ptr[idx] = shares;
                    p_ptr[idx] = packed_price;
                    break;
                }
                idx = (idx + 1) & MAP_MASK;
            }
        }

        uint32_t tick = price / 100;
        if (__builtin_expect(tick < 500000, 1)) {
            size_t b_idx = tick >> 6;
            size_t s_idx = b_idx >> 6;
            if (side == 'B') {
                bid_levels_[tick] += shares;
                bid_bitmap_[b_idx] |= (1ULL << (tick & 63));
                bid_summary_[s_idx] |= (1ULL << (b_idx & 63));
                if (tick > best_bid_tick_ || best_bid_tick_ >= 500000) best_bid_tick_ = tick;
            } else {
                ask_levels_[tick] += shares;
                ask_bitmap_[b_idx] |= (1ULL << (tick & 63));
                ask_summary_[s_idx] |= (1ULL << (b_idx & 63));
                if (tick < best_ask_tick_) best_ask_tick_ = tick;
            }
        }
    }

    inline void execute_order(uint64_t order_id, uint32_t shares) noexcept {
        size_t idx = hash_order_id(order_id);
        uint64_t* k_ptr = keys_.get();
        uint32_t* s_ptr = shares_.get();
        uint32_t* p_ptr = prices_.get();

        while (true) {
            uint64_t k = k_ptr[idx];
            if (k == EMPTY_KEY) return;
            if (k == order_id) {
                uint32_t raw_price = p_ptr[idx];
                char side = (raw_price >> 31) ? 'B' : 'S';
                uint32_t tick = (raw_price & 0x7FFFFFFF) / 100;
                if (s_ptr[idx] <= shares) {
                    reduce_level(side, tick, s_ptr[idx]);
                    k_ptr[idx] = DELETED_KEY;
                    active_orders_--;
                } else {
                    s_ptr[idx] -= shares;
                    reduce_level(side, tick, shares);
                }
                return;
            }
            idx = (idx + 1) & MAP_MASK;
        }
    }

    inline void cancel_order(uint64_t order_id, uint32_t shares) noexcept {
        execute_order(order_id, shares);
    }

    inline void delete_order(uint64_t order_id) noexcept {
        size_t idx = hash_order_id(order_id);
        uint64_t* k_ptr = keys_.get();
        uint32_t* s_ptr = shares_.get();
        uint32_t* p_ptr = prices_.get();

        while (true) {
            uint64_t k = k_ptr[idx];
            if (k == EMPTY_KEY) return;
            if (k == order_id) {
                uint32_t raw_price = p_ptr[idx];
                char side = (raw_price >> 31) ? 'B' : 'S';
                uint32_t tick = (raw_price & 0x7FFFFFFF) / 100;
                reduce_level(side, tick, s_ptr[idx]);
                k_ptr[idx] = DELETED_KEY;
                active_orders_--;
                return;
            }
            idx = (idx + 1) & MAP_MASK;
        }
    }

    inline void replace_order(uint64_t old_order_id, uint64_t new_order_id, uint32_t new_shares, uint32_t new_price) noexcept {
        size_t idx = hash_order_id(old_order_id);
        uint64_t* k_ptr = keys_.get();
        uint32_t* s_ptr = shares_.get();
        uint32_t* p_ptr = prices_.get();

        while (true) {
            uint64_t k = k_ptr[idx];
            if (k == EMPTY_KEY) return;
            if (k == old_order_id) {
                uint32_t raw_price = p_ptr[idx];
                char side = (raw_price >> 31) ? 'B' : 'S';
                uint32_t old_tick = (raw_price & 0x7FFFFFFF) / 100;
                reduce_level(side, old_tick, s_ptr[idx]);
                k_ptr[idx] = DELETED_KEY;
                active_orders_--;
                add_order(new_order_id, side, new_shares, new_price);
                return;
            }
            idx = (idx + 1) & MAP_MASK;
        }
    }

    [[nodiscard]] size_t order_count() const noexcept { return active_orders_; }

    [[nodiscard]] uint64_t level_volume(char side, uint32_t tick) const noexcept {
        if (tick >= 500000) return 0;
        return (side == 'B') ? bid_levels_[tick] : ask_levels_[tick];
    }

    [[nodiscard]] uint32_t best_bid() noexcept {
        if (best_bid_tick_ > 0 && best_bid_tick_ < 500000 && bid_levels_[best_bid_tick_] > 0) return best_bid_tick_;
        for (ssize_t s = static_cast<ssize_t>(NUM_SUMMARY - 1); s >= 0; --s) {
            uint64_t s_mask = bid_summary_[s];
            while (s_mask != 0) {
                int s_bit = 63 - __builtin_clzll(s_mask);
                size_t bucket = (static_cast<size_t>(s) << 6) | s_bit;
                uint64_t mask = bid_bitmap_[bucket];
                while (mask != 0) {
                    int bit = 63 - __builtin_clzll(mask);
                    uint32_t cand = static_cast<uint32_t>((bucket << 6) | bit);
                    if (cand < 500000 && bid_levels_[cand] > 0) {
                        best_bid_tick_ = cand;
                        return cand;
                    }
                    mask &= ~(1ULL << bit);
                }
                s_mask &= ~(1ULL << s_bit);
            }
        }
        best_bid_tick_ = 0;
        return 0;
    }

    [[nodiscard]] uint32_t best_ask() noexcept {
        if (best_ask_tick_ < 500000 && ask_levels_[best_ask_tick_] > 0) return best_ask_tick_;
        for (size_t s = 0; s < NUM_SUMMARY; ++s) {
            uint64_t s_mask = ask_summary_[s];
            while (s_mask != 0) {
                int s_bit = __builtin_ctzll(s_mask);
                size_t bucket = (s << 6) | s_bit;
                uint64_t mask = ask_bitmap_[bucket];
                while (mask != 0) {
                    int bit = __builtin_ctzll(mask);
                    uint32_t cand = static_cast<uint32_t>((bucket << 6) | bit);
                    if (cand < 500000 && ask_levels_[cand] > 0) {
                        best_ask_tick_ = cand;
                        return cand;
                    }
                    mask &= ~(1ULL << bit);
                }
                s_mask &= ~(1ULL << s_bit);
            }
        }
        best_ask_tick_ = 0xFFFFFFFF;
        return 0xFFFFFFFF;
    }

    void print_top(size_t levels = 5) noexcept {
        std::cout << "\n========== ORDER BOOK TOP " << levels << " ==========\n";
        std::cout << std::setw(12) << "BID QTY" << " | " << std::setw(10) << "BID PX"
                  << " || " << std::setw(10) << "ASK PX" << " | " << std::setw(12) << "ASK QTY\n";
        std::cout << "--------------------------------------------------------\n";

        uint32_t b_tick = best_bid();
        uint32_t a_tick = best_ask();

        for (size_t i = 0; i < levels; ++i) {
            while (b_tick > 0 && bid_levels_[b_tick] == 0) --b_tick;
            while (a_tick < 500000 && ask_levels_[a_tick] == 0) ++a_tick;

            std::string bid_str = (b_tick > 0 && bid_levels_[b_tick] > 0) ? std::to_string(bid_levels_[b_tick]) : "-";
            std::string bid_px  = (b_tick > 0 && bid_levels_[b_tick] > 0) ? std::to_string((b_tick * 100) / 10000.0) : "-";
            std::string ask_px  = (a_tick < 500000 && ask_levels_[a_tick] > 0) ? std::to_string((a_tick * 100) / 10000.0) : "-";
            std::string ask_str = (a_tick < 500000 && ask_levels_[a_tick] > 0) ? std::to_string(ask_levels_[a_tick]) : "-";

            std::cout << std::setw(12) << bid_str << " | " << std::setw(10) << bid_px
                      << " || " << std::setw(10) << ask_px << " | " << std::setw(12) << ask_str << "\n";

            if (b_tick > 0) --b_tick;
            if (a_tick < 500000) ++a_tick;
        }
        std::cout << "========================================================\n\n";
    }

private:
    inline void reduce_level(char side, uint32_t tick, uint32_t shares) noexcept {
        if (__builtin_expect(tick >= 500000, 0)) return;
        size_t b_idx = tick >> 6;
        size_t s_idx = b_idx >> 6;
        if (side == 'B') {
            if (bid_levels_[tick] <= shares) {
                bid_levels_[tick] = 0;
                bid_bitmap_[b_idx] &= ~(1ULL << (tick & 63));
                if (bid_bitmap_[b_idx] == 0) {
                    bid_summary_[s_idx] &= ~(1ULL << (b_idx & 63));
                }
                if (tick == best_bid_tick_) best_bid_tick_ = 0;
            } else {
                bid_levels_[tick] -= shares;
            }
        } else {
            if (ask_levels_[tick] <= shares) {
                ask_levels_[tick] = 0;
                ask_bitmap_[b_idx] &= ~(1ULL << (tick & 63));
                if (ask_bitmap_[b_idx] == 0) {
                    ask_summary_[s_idx] &= ~(1ULL << (b_idx & 63));
                }
                if (tick == best_ask_tick_) best_ask_tick_ = 0xFFFFFFFF;
            } else {
                ask_levels_[tick] -= shares;
            }
        }
    }

    size_t active_orders_{0};
    uint32_t best_bid_tick_{0};
    uint32_t best_ask_tick_{0xFFFFFFFF};

    std::unique_ptr<uint64_t[]> keys_;
    std::unique_ptr<uint32_t[]> shares_;
    std::unique_ptr<uint32_t[]> prices_;
    std::unique_ptr<uint64_t[]> bid_levels_;
    std::unique_ptr<uint64_t[]> ask_levels_;
    std::unique_ptr<uint64_t[]> bid_bitmap_;
    std::unique_ptr<uint64_t[]> ask_bitmap_;
    std::unique_ptr<uint64_t[]> bid_summary_;
    std::unique_ptr<uint64_t[]> ask_summary_;
};

}
