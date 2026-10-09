#include "kestrel/order_book.hpp"
#include <map>
#include <unordered_map>
#include <random>
#include <cassert>
#include <iostream>

struct ReferenceOrder {
    uint32_t shares;
    uint32_t price;
    char side;
};

class ReferenceOrderBook {
public:
    void add_order(uint64_t oid, char side, uint32_t shares, uint32_t price) {
        auto it = orders_.find(oid);
        if (it != orders_.end()) {
            reduce_level(it->second.side, it->second.price / 100, it->second.shares);
        }
        orders_[oid] = {shares, price, side};
        uint32_t tick = price / 100;
        if (side == 'B') {
            bids_[tick] += shares;
        } else {
            asks_[tick] += shares;
        }
    }

    void execute_order(uint64_t oid, uint32_t shares) {
        auto it = orders_.find(oid);
        if (it == orders_.end()) return;
        uint32_t tick = it->second.price / 100;
        if (it->second.shares <= shares) {
            reduce_level(it->second.side, tick, it->second.shares);
            orders_.erase(it);
        } else {
            it->second.shares -= shares;
            reduce_level(it->second.side, tick, shares);
        }
    }

    void cancel_order(uint64_t oid, uint32_t shares) {
        execute_order(oid, shares);
    }

    void delete_order(uint64_t oid) {
        auto it = orders_.find(oid);
        if (it == orders_.end()) return;
        reduce_level(it->second.side, it->second.price / 100, it->second.shares);
        orders_.erase(it);
    }

    void replace_order(uint64_t old_oid, uint64_t new_oid, uint32_t new_shares, uint32_t new_price) {
        auto it = orders_.find(old_oid);
        if (it == orders_.end()) return;
        char side = it->second.side;
        delete_order(old_oid);
        add_order(new_oid, side, new_shares, new_price);
    }

    uint32_t best_bid() const {
        for (auto it = bids_.rbegin(); it != bids_.rend(); ++it) {
            if (it->second > 0) return it->first;
        }
        return 0;
    }

    uint32_t best_ask() const {
        for (auto it = asks_.begin(); it != asks_.end(); ++it) {
            if (it->second > 0) return it->first;
        }
        return 0xFFFFFFFF;
    }

    uint64_t level_volume(char side, uint32_t tick) const {
        if (side == 'B') {
            auto it = bids_.find(tick);
            return (it != bids_.end()) ? it->second : 0;
        } else {
            auto it = asks_.find(tick);
            return (it != asks_.end()) ? it->second : 0;
        }
    }

    size_t order_count() const { return orders_.size(); }

private:
    void reduce_level(char side, uint32_t tick, uint32_t shares) {
        if (side == 'B') {
            if (bids_[tick] <= shares) bids_.erase(tick);
            else bids_[tick] -= shares;
        } else {
            if (asks_[tick] <= shares) asks_.erase(tick);
            else asks_[tick] -= shares;
        }
    }

    std::unordered_map<uint64_t, ReferenceOrder> orders_;
    std::map<uint32_t, uint64_t> bids_;
    std::map<uint32_t, uint64_t> asks_;
};

int main() {
    kestrel::OrderBook fast_book;
    ReferenceOrderBook ref_book;

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<uint64_t> oid_dist(1, 50000);
    std::uniform_int_distribution<uint32_t> price_dist(10000, 30000);
    std::uniform_int_distribution<uint32_t> shares_dist(10, 500);
    std::uniform_int_distribution<int> op_dist(0, 9);
    std::uniform_int_distribution<int> side_dist(0, 1);

    constexpr size_t NUM_OPS = 200000;

    for (size_t step = 0; step < NUM_OPS; ++step) {
        int op = op_dist(rng);
        uint64_t oid = oid_dist(rng);

        if (op < 5) {
            char side = side_dist(rng) ? 'B' : 'S';
            uint32_t shares = shares_dist(rng);
            uint32_t price = price_dist(rng);
            fast_book.add_order(oid, side, shares, price);
            ref_book.add_order(oid, side, shares, price);
        } else if (op < 7) {
            uint32_t shares = shares_dist(rng);
            fast_book.execute_order(oid, shares);
            ref_book.execute_order(oid, shares);
        } else if (op == 7) {
            uint32_t shares = shares_dist(rng);
            fast_book.cancel_order(oid, shares);
            ref_book.cancel_order(oid, shares);
        } else if (op == 8) {
            fast_book.delete_order(oid);
            ref_book.delete_order(oid);
        } else {
            uint64_t new_oid = oid_dist(rng) + 100000;
            uint32_t new_shares = shares_dist(rng);
            uint32_t new_price = price_dist(rng);
            fast_book.replace_order(oid, new_oid, new_shares, new_price);
            ref_book.replace_order(oid, new_oid, new_shares, new_price);
        }

        if (step % 1000 == 0) {
            assert(fast_book.order_count() == ref_book.order_count());
            assert(fast_book.best_bid() == ref_book.best_bid());
            assert(fast_book.best_ask() == ref_book.best_ask());
        }
    }

    assert(fast_book.order_count() == ref_book.order_count());
    assert(fast_book.best_bid() == ref_book.best_bid());
    assert(fast_book.best_ask() == ref_book.best_ask());

    std::cout << "[+] Differential Testing (200,000 Operations against Reference Model): PASSED.\n";
    std::cout << "[+] Final Order Count: " << fast_book.order_count() << "\n";
    std::cout << "[+] Final Best Bid:    " << fast_book.best_bid() << " (Ref: " << ref_book.best_bid() << ")\n";
    std::cout << "[+] Final Best Ask:    " << fast_book.best_ask() << " (Ref: " << ref_book.best_ask() << ")\n";

    return 0;
}
