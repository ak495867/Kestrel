#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <map>
#include <iostream>
#include <iomanip>
#include <string>

namespace kestrel {

struct BookOrder {
    uint64_t order_id{0};
    uint32_t shares{0};
    uint32_t price{0};
    char side{'B'};
};

class OrderBook {
public:
    void add_order(uint64_t order_id, char side, uint32_t shares, uint32_t price) {
        orders_[order_id] = {order_id, shares, price, side};
        if (side == 'B') {
            bids_[price] += shares;
        } else {
            asks_[price] += shares;
        }
    }

    void execute_order(uint64_t order_id, uint32_t shares) {
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return;

        if (it->second.shares <= shares) {
            remove_price_level(it->second.side, it->second.price, it->second.shares);
            orders_.erase(it);
        } else {
            it->second.shares -= shares;
            reduce_price_level(it->second.side, it->second.price, shares);
        }
    }

    void cancel_order(uint64_t order_id, uint32_t shares) {
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return;

        if (it->second.shares <= shares) {
            remove_price_level(it->second.side, it->second.price, it->second.shares);
            orders_.erase(it);
        } else {
            it->second.shares -= shares;
            reduce_price_level(it->second.side, it->second.price, shares);
        }
    }

    void delete_order(uint64_t order_id) {
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return;

        remove_price_level(it->second.side, it->second.price, it->second.shares);
        orders_.erase(it);
    }

    void replace_order(uint64_t old_order_id, uint64_t new_order_id, uint32_t new_shares, uint32_t new_price) {
        auto it = orders_.find(old_order_id);
        if (it == orders_.end()) return;

        char side = it->second.side;
        remove_price_level(side, it->second.price, it->second.shares);
        orders_.erase(it);

        add_order(new_order_id, side, new_shares, new_price);
    }

    [[nodiscard]] size_t order_count() const noexcept { return orders_.size(); }
    [[nodiscard]] size_t bid_depth() const noexcept { return bids_.size(); }
    [[nodiscard]] size_t ask_depth() const noexcept { return asks_.size(); }

    void print_top(size_t levels = 5) const {
        std::cout << "\n========== ORDER BOOK TOP " << levels << " ==========\n";
        std::cout << std::setw(12) << "BID QTY" << " | " << std::setw(10) << "BID PX"
                  << " || " << std::setw(10) << "ASK PX" << " | " << std::setw(12) << "ASK QTY\n";
        std::cout << "--------------------------------------------------------\n";

        auto bid_it = bids_.begin();
        auto ask_it = asks_.begin();

        for (size_t i = 0; i < levels; ++i) {
            std::string bid_str = (bid_it != bids_.end()) ? std::to_string(bid_it->second) : "-";
            std::string bid_px  = (bid_it != bids_.end()) ? std::to_string(bid_it->first / 10000.0) : "-";
            std::string ask_px  = (ask_it != asks_.end()) ? std::to_string(ask_it->first / 10000.0) : "-";
            std::string ask_str = (ask_it != asks_.end()) ? std::to_string(ask_it->second) : "-";

            std::cout << std::setw(12) << bid_str << " | " << std::setw(10) << bid_px
                      << " || " << std::setw(10) << ask_px << " | " << std::setw(12) << ask_str << "\n";

            if (bid_it != bids_.end()) ++bid_it;
            if (ask_it != asks_.end()) ++ask_it;
        }
        std::cout << "========================================================\n\n";
    }

private:
    void reduce_price_level(char side, uint32_t price, uint32_t shares) {
        if (side == 'B') {
            auto it = bids_.find(price);
            if (it != bids_.end()) {
                if (it->second <= shares) bids_.erase(it);
                else it->second -= shares;
            }
        } else {
            auto it = asks_.find(price);
            if (it != asks_.end()) {
                if (it->second <= shares) asks_.erase(it);
                else it->second -= shares;
            }
        }
    }

    void remove_price_level(char side, uint32_t price, uint32_t shares) {
        reduce_price_level(side, price, shares);
    }

    std::unordered_map<uint64_t, BookOrder> orders_;
    std::map<uint32_t, uint64_t, std::greater<uint32_t>> bids_;
    std::map<uint32_t, uint64_t, std::less<uint32_t>> asks_;
};

}
