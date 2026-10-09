#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <iostream>
#include <iomanip>
#include <string>
#include <algorithm>

namespace kestrel {

struct BookOrder {
    uint32_t shares{0};
    uint32_t price{0};
    char side{0};
};

class OrderBook {
public:
    explicit OrderBook(size_t max_orders = 10'000'000, uint32_t max_price_ticks = 500'000)
        : max_orders_(max_orders), max_price_ticks_(max_price_ticks) {
        orders_.resize(max_orders_);
        bid_levels_.resize(max_price_ticks_, 0);
        ask_levels_.resize(max_price_ticks_, 0);
    }

    inline void add_order(uint64_t order_id, char side, uint32_t shares, uint32_t price) noexcept {
        if (__builtin_expect(order_id < max_orders_, 1)) {
            orders_[order_id] = {shares, price, side};
            active_orders_++;
        }

        uint32_t tick = price / 100;
        if (__builtin_expect(tick < max_price_ticks_, 1)) {
            if (side == 'B') {
                bid_levels_[tick] += shares;
                best_bid_tick_ = std::max(best_bid_tick_, tick);
            } else {
                ask_levels_[tick] += shares;
                best_ask_tick_ = std::min(best_ask_tick_, tick);
            }
        }
    }

    inline void execute_order(uint64_t order_id, uint32_t shares) noexcept {
        if (__builtin_expect(order_id >= max_orders_, 0)) return;
        auto& ord = orders_[order_id];
        if (!ord.side) return;

        uint32_t tick = ord.price / 100;
        if (ord.shares <= shares) {
            reduce_level(ord.side, tick, ord.shares);
            ord.side = 0;
            active_orders_--;
        } else {
            ord.shares -= shares;
            reduce_level(ord.side, tick, shares);
        }
    }

    inline void cancel_order(uint64_t order_id, uint32_t shares) noexcept {
        execute_order(order_id, shares);
    }

    inline void delete_order(uint64_t order_id) noexcept {
        if (__builtin_expect(order_id >= max_orders_, 0)) return;
        auto& ord = orders_[order_id];
        if (!ord.side) return;

        uint32_t tick = ord.price / 100;
        reduce_level(ord.side, tick, ord.shares);
        ord.side = 0;
        active_orders_--;
    }

    inline void replace_order(uint64_t old_order_id, uint64_t new_order_id, uint32_t new_shares, uint32_t new_price) noexcept {
        if (__builtin_expect(old_order_id >= max_orders_, 0)) return;
        auto& ord = orders_[old_order_id];
        char side = ord.side;
        if (!side) return;

        uint32_t old_tick = ord.price / 100;
        reduce_level(side, old_tick, ord.shares);
        ord.side = 0;
        active_orders_--;

        add_order(new_order_id, side, new_shares, new_price);
    }

    [[nodiscard]] size_t order_count() const noexcept { return active_orders_; }

    void print_top(size_t levels = 5) const {
        std::cout << "\n========== ORDER BOOK TOP " << levels << " ==========\n";
        std::cout << std::setw(12) << "BID QTY" << " | " << std::setw(10) << "BID PX"
                  << " || " << std::setw(10) << "ASK PX" << " | " << std::setw(12) << "ASK QTY\n";
        std::cout << "--------------------------------------------------------\n";

        uint32_t b_tick = best_bid_tick_;
        uint32_t a_tick = best_ask_tick_;

        for (size_t i = 0; i < levels; ++i) {
            while (b_tick > 0 && bid_levels_[b_tick] == 0) --b_tick;
            while (a_tick < max_price_ticks_ && ask_levels_[a_tick] == 0) ++a_tick;

            std::string bid_str = (b_tick > 0 && bid_levels_[b_tick] > 0) ? std::to_string(bid_levels_[b_tick]) : "-";
            std::string bid_px  = (b_tick > 0 && bid_levels_[b_tick] > 0) ? std::to_string((b_tick * 100) / 10000.0) : "-";
            std::string ask_px  = (a_tick < max_price_ticks_ && ask_levels_[a_tick] > 0) ? std::to_string((a_tick * 100) / 10000.0) : "-";
            std::string ask_str = (a_tick < max_price_ticks_ && ask_levels_[a_tick] > 0) ? std::to_string(ask_levels_[a_tick]) : "-";

            std::cout << std::setw(12) << bid_str << " | " << std::setw(10) << bid_px
                      << " || " << std::setw(10) << ask_px << " | " << std::setw(12) << ask_str << "\n";

            if (b_tick > 0) --b_tick;
            if (a_tick < max_price_ticks_) ++a_tick;
        }
        std::cout << "========================================================\n\n";
    }

private:
    inline void reduce_level(char side, uint32_t tick, uint32_t shares) noexcept {
        if (__builtin_expect(tick >= max_price_ticks_, 0)) return;
        if (side == 'B') {
            if (bid_levels_[tick] <= shares) {
                bid_levels_[tick] = 0;
            } else {
                bid_levels_[tick] -= shares;
            }
        } else {
            if (ask_levels_[tick] <= shares) {
                ask_levels_[tick] = 0;
            } else {
                ask_levels_[tick] -= shares;
            }
        }
    }

    size_t max_orders_;
    uint32_t max_price_ticks_;
    size_t active_orders_{0};
    uint32_t best_bid_tick_{0};
    uint32_t best_ask_tick_{0xFFFFFFFF};

    std::vector<BookOrder> orders_;
    std::vector<uint64_t> bid_levels_;
    std::vector<uint64_t> ask_levels_;
};

}
