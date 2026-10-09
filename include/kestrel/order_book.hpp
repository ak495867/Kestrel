#pragma once

#include <cstdint>
#include <vector>
#include <iostream>
#include <iomanip>
#include <string>
#include <algorithm>
#include <cstring>

namespace kestrel {

struct BookOrder {
    uint32_t shares{0};
    uint32_t price{0};
    char side{0};
};

class OrderBook {
public:
    static constexpr size_t MAP_CAPACITY = 4194304;
    static constexpr size_t MAP_MASK = MAP_CAPACITY - 1;
    static constexpr uint64_t EMPTY_KEY = 0;
    static constexpr uint64_t DELETED_KEY = 0xFFFFFFFFFFFFFFFFULL;
    static constexpr size_t NUM_BUCKETS = 16384;

    OrderBook() {
        keys_.assign(MAP_CAPACITY, EMPTY_KEY);
        values_.resize(MAP_CAPACITY);
        bid_levels_.assign(500000, 0);
        ask_levels_.assign(500000, 0);
        bid_bitmap_.assign(NUM_BUCKETS, 0);
        ask_bitmap_.assign(NUM_BUCKETS, 0);
    }

    inline void add_order(uint64_t order_id, char side, uint32_t shares, uint32_t price) noexcept {
        size_t idx = hash_oid(order_id);
        size_t first_deleted = MAP_CAPACITY;

        while (true) {
            uint64_t k = keys_[idx];
            if (k == EMPTY_KEY) {
                if (first_deleted != MAP_CAPACITY) idx = first_deleted;
                keys_[idx] = order_id;
                values_[idx] = {shares, price, side};
                active_orders_++;
                break;
            } else if (k == DELETED_KEY) {
                if (first_deleted == MAP_CAPACITY) first_deleted = idx;
            } else if (k == order_id) {
                values_[idx] = {shares, price, side};
                break;
            }
            idx = (idx + 1) & MAP_MASK;
        }

        uint32_t tick = price / 100;
        if (__builtin_expect(tick < 500000, 1)) {
            if (side == 'B') {
                bid_levels_[tick] += shares;
                bid_bitmap_[tick >> 6] |= (1ULL << (tick & 63));
                if (tick > best_bid_tick_) best_bid_tick_ = tick;
            } else {
                ask_levels_[tick] += shares;
                ask_bitmap_[tick >> 6] |= (1ULL << (tick & 63));
                if (tick < best_ask_tick_) best_ask_tick_ = tick;
            }
        }
    }

    inline void execute_order(uint64_t order_id, uint32_t shares) noexcept {
        size_t idx = hash_oid(order_id);
        while (true) {
            uint64_t k = keys_[idx];
            if (k == EMPTY_KEY) return;
            if (k == order_id) {
                auto& ord = values_[idx];
                uint32_t tick = ord.price / 100;
                if (ord.shares <= shares) {
                    reduce_level(ord.side, tick, ord.shares);
                    keys_[idx] = DELETED_KEY;
                    ord.side = 0;
                    active_orders_--;
                } else {
                    ord.shares -= shares;
                    reduce_level(ord.side, tick, shares);
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
        size_t idx = hash_oid(order_id);
        while (true) {
            uint64_t k = keys_[idx];
            if (k == EMPTY_KEY) return;
            if (k == order_id) {
                auto& ord = values_[idx];
                uint32_t tick = ord.price / 100;
                reduce_level(ord.side, tick, ord.shares);
                keys_[idx] = DELETED_KEY;
                ord.side = 0;
                active_orders_--;
                return;
            }
            idx = (idx + 1) & MAP_MASK;
        }
    }

    inline void replace_order(uint64_t old_order_id, uint64_t new_order_id, uint32_t new_shares, uint32_t new_price) noexcept {
        size_t idx = hash_oid(old_order_id);
        while (true) {
            uint64_t k = keys_[idx];
            if (k == EMPTY_KEY) return;
            if (k == old_order_id) {
                auto ord = values_[idx];
                char side = ord.side;
                uint32_t old_tick = ord.price / 100;
                reduce_level(side, old_tick, ord.shares);
                keys_[idx] = DELETED_KEY;
                active_orders_--;
                add_order(new_order_id, side, new_shares, new_price);
                return;
            }
            idx = (idx + 1) & MAP_MASK;
        }
    }

    [[nodiscard]] size_t order_count() const noexcept { return active_orders_; }

    [[nodiscard]] uint32_t best_bid() noexcept {
        if (best_bid_tick_ == 0 || bid_levels_[best_bid_tick_] > 0) return best_bid_tick_;
        size_t bucket = best_bid_tick_ >> 6;
        while (bucket < NUM_BUCKETS) {
            uint64_t mask = bid_bitmap_[bucket];
            if (bucket == (best_bid_tick_ >> 6)) {
                mask &= ((1ULL << (best_bid_tick_ & 63)) - 1);
            }
            if (mask != 0) {
                int bit = 63 - __builtin_clzll(mask);
                best_bid_tick_ = static_cast<uint32_t>((bucket << 6) | bit);
                return best_bid_tick_;
            }
            if (bucket == 0) break;
            --bucket;
        }
        best_bid_tick_ = 0;
        return 0;
    }

    [[nodiscard]] uint32_t best_ask() noexcept {
        if (best_ask_tick_ == 0xFFFFFFFF || (best_ask_tick_ < 500000 && ask_levels_[best_ask_tick_] > 0)) return best_ask_tick_;
        size_t bucket = (best_ask_tick_ < 500000) ? (best_ask_tick_ >> 6) : 0;
        while (bucket < NUM_BUCKETS) {
            uint64_t mask = ask_bitmap_[bucket];
            if (bucket == (best_ask_tick_ >> 6)) {
                mask &= ~((1ULL << ((best_ask_tick_ & 63) + 1)) - 1);
            }
            if (mask != 0) {
                int bit = __builtin_ctzll(mask);
                best_ask_tick_ = static_cast<uint32_t>((bucket << 6) | bit);
                return best_ask_tick_;
            }
            ++bucket;
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
    static inline size_t hash_oid(uint64_t x) noexcept {
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return x & MAP_MASK;
    }

    inline void reduce_level(char side, uint32_t tick, uint32_t shares) noexcept {
        if (__builtin_expect(tick >= 500000, 0)) return;
        if (side == 'B') {
            if (bid_levels_[tick] <= shares) {
                bid_levels_[tick] = 0;
                bid_bitmap_[tick >> 6] &= ~(1ULL << (tick & 63));
                if (tick == best_bid_tick_) best_bid_tick_ = 0;
            } else {
                bid_levels_[tick] -= shares;
            }
        } else {
            if (ask_levels_[tick] <= shares) {
                ask_levels_[tick] = 0;
                ask_bitmap_[tick >> 6] &= ~(1ULL << (tick & 63));
                if (tick == best_ask_tick_) best_ask_tick_ = 0xFFFFFFFF;
            } else {
                ask_levels_[tick] -= shares;
            }
        }
    }

    size_t active_orders_{0};
    uint32_t best_bid_tick_{0};
    uint32_t best_ask_tick_{0xFFFFFFFF};

    std::vector<uint64_t> keys_;
    std::vector<BookOrder> values_;
    std::vector<uint64_t> bid_levels_;
    std::vector<uint64_t> ask_levels_;
    std::vector<uint64_t> bid_bitmap_;
    std::vector<uint64_t> ask_bitmap_;
};

}
