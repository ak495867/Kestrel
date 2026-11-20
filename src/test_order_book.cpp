#include "kestrel/order_book.hpp"
#include <cassert>
#include <iostream>
#include <vector>

void test_add_order_duplicate_id() {
    kestrel::OrderBook book;

    book.add_order(1001, 'B', 500, 15000);
    assert(book.order_count() == 1);
    assert(book.level_volume('B', 150) == 500);
    assert(book.best_bid() == 150);

    book.add_order(1001, 'B', 800, 15000);
    assert(book.order_count() == 1);
    assert(book.level_volume('B', 150) == 800);
    assert(book.best_bid() == 150);

    book.add_order(1001, 'B', 300, 16000);
    assert(book.order_count() == 1);
    assert(book.level_volume('B', 150) == 0);
    assert(book.level_volume('B', 160) == 300);
    assert(book.best_bid() == 160);

    book.add_order(1001, 'S', 400, 17000);
    assert(book.order_count() == 1);
    assert(book.level_volume('B', 160) == 0);
    assert(book.level_volume('S', 170) == 400);
    assert(book.best_bid() == 0);
    assert(book.best_ask() == 170);

    book.delete_order(1001);
    assert(book.order_count() == 0);
    assert(book.level_volume('S', 170) == 0);
    assert(book.best_ask() == 0xFFFFFFFF);
}

void test_deletion_followed_by_reinsertion() {
    kestrel::OrderBook book;

    book.add_order(5001, 'B', 100, 20000);
    assert(book.order_count() == 1);
    assert(book.level_volume('B', 200) == 100);
    assert(book.best_bid() == 200);

    book.delete_order(5001);
    assert(book.order_count() == 0);
    assert(book.level_volume('B', 200) == 0);
    assert(book.best_bid() == 0);

    book.add_order(5001, 'B', 250, 20000);
    assert(book.order_count() == 1);
    assert(book.level_volume('B', 200) == 250);
    assert(book.best_bid() == 200);

    book.delete_order(5001);
    assert(book.order_count() == 0);
    assert(book.best_bid() == 0);

    book.add_order(5001, 'S', 300, 22000);
    assert(book.order_count() == 1);
    assert(book.level_volume('S', 220) == 300);
    assert(book.best_ask() == 220);

    book.cancel_order(5001, 300);
    assert(book.order_count() == 0);
    assert(book.best_ask() == 0xFFFFFFFF);

    for (int cycle = 0; cycle < 100; ++cycle) {
        book.add_order(9999, 'B', 10, 10000 + cycle * 100);
        assert(book.order_count() == 1);
        book.delete_order(9999);
        assert(book.order_count() == 0);
    }
    assert(book.best_bid() == 0);
}

void test_orders_moving_between_price_levels() {
    kestrel::OrderBook book;

    book.add_order(1, 'B', 100, 18000);
    book.add_order(2, 'B', 200, 20000);
    assert(book.order_count() == 2);
    assert(book.best_bid() == 200);

    book.add_order(2, 'B', 200, 15000);
    assert(book.order_count() == 2);
    assert(book.level_volume('B', 200) == 0);
    assert(book.level_volume('B', 180) == 100);
    assert(book.level_volume('B', 150) == 200);
    assert(book.best_bid() == 180);

    book.add_order(2, 'B', 200, 22000);
    assert(book.order_count() == 2);
    assert(book.level_volume('B', 150) == 0);
    assert(book.level_volume('B', 220) == 200);
    assert(book.best_bid() == 220);

    book.add_order(10, 'S', 100, 30000);
    book.add_order(11, 'S', 150, 28000);
    assert(book.best_ask() == 280);

    book.add_order(11, 'S', 150, 32000);
    assert(book.level_volume('S', 280) == 0);
    assert(book.best_ask() == 300);

    book.add_order(11, 'S', 150, 25000);
    assert(book.best_ask() == 250);
}

void test_bitmap_cleanup_when_level_reaches_zero() {
    kestrel::OrderBook book;

    for (uint32_t tick = 60; tick <= 70; ++tick) {
        book.add_order(1000 + tick, 'B', 50, tick * 100);
    }
    assert(book.best_bid() == 70);

    for (uint32_t tick = 70; tick >= 60; --tick) {
        assert(book.best_bid() == tick);
        book.delete_order(1000 + tick);
        assert(book.level_volume('B', tick) == 0);
    }
    assert(book.best_bid() == 0);

    for (uint32_t tick = 60; tick <= 70; ++tick) {
        book.add_order(2000 + tick, 'S', 50, tick * 100);
    }
    assert(book.best_ask() == 60);

    for (uint32_t tick = 60; tick <= 70; ++tick) {
        assert(book.best_ask() == tick);
        book.delete_order(2000 + tick);
        assert(book.level_volume('S', tick) == 0);
    }
    assert(book.best_ask() == 0xFFFFFFFF);

    book.add_order(3001, 'B', 100, 500000);
    book.add_order(3002, 'B', 100, 500000);
    assert(book.level_volume('B', 5000) == 200);
    book.execute_order(3001, 100);
    assert(book.level_volume('B', 5000) == 100);
    assert(book.best_bid() == 5000);
    book.execute_order(3002, 100);
    assert(book.level_volume('B', 5000) == 0);
    assert(book.best_bid() == 0);
}

void test_hash_table_saturation_and_probing_termination() {
    kestrel::OrderBook book;

    constexpr size_t TEST_ORDERS = 5000;
    for (size_t i = 1; i <= TEST_ORDERS; ++i) {
        book.add_order(i, (i % 2 == 0) ? 'B' : 'S', 10, 10000 + static_cast<uint32_t>(i % 500) * 100);
    }
    assert(book.order_count() == TEST_ORDERS);

    for (size_t i = 1; i <= TEST_ORDERS; i += 2) {
        book.delete_order(i);
    }
    assert(book.order_count() == TEST_ORDERS / 2);

    for (size_t i = 1; i <= TEST_ORDERS; i += 2) {
        book.add_order(i, 'B', 20, 15000);
    }
    assert(book.order_count() == TEST_ORDERS);

    book.delete_order(999999999ULL);
    book.execute_order(888888888ULL, 100);
    book.replace_order(777777777ULL, 666666666ULL, 100, 20000);

    size_t synthetic_colliding_base = 10000000;
    for (size_t i = 0; i < 200; ++i) {
        uint64_t colliding_oid = synthetic_colliding_base + (i * kestrel::OrderBook::MAP_CAPACITY);
        book.add_order(colliding_oid, 'B', 5, 20000);
    }

    for (size_t i = 0; i < 200; ++i) {
        uint64_t colliding_oid = synthetic_colliding_base + (i * kestrel::OrderBook::MAP_CAPACITY);
        book.delete_order(colliding_oid);
    }
}

void test_best_bid_maintenance() {
    kestrel::OrderBook book;

    assert(book.best_bid() == 0);

    book.add_order(1, 'B', 100, 10000);
    book.add_order(2, 'B', 200, 12000);
    book.add_order(3, 'B', 300, 15000);
    assert(book.best_bid() == 150);

    book.delete_order(3);
    assert(book.best_bid() == 120);

    book.cancel_order(2, 200);
    assert(book.best_bid() == 100);

    book.execute_order(1, 100);
    assert(book.best_bid() == 0);

    book.add_order(10, 'B', 50, 18000);
    book.add_order(11, 'B', 75, 18000);
    assert(book.best_bid() == 180);
    book.delete_order(10);
    assert(book.best_bid() == 180);
    book.delete_order(11);
    assert(book.best_bid() == 0);
}

void test_best_ask_maintenance() {
    kestrel::OrderBook book;

    assert(book.best_ask() == 0xFFFFFFFF);

    book.add_order(1, 'S', 100, 25000);
    book.add_order(2, 'S', 200, 22000);
    book.add_order(3, 'S', 300, 20000);
    assert(book.best_ask() == 200);

    book.delete_order(3);
    assert(book.best_ask() == 220);

    book.execute_order(2, 200);
    assert(book.best_ask() == 250);

    book.cancel_order(1, 100);
    assert(book.best_ask() == 0xFFFFFFFF);

    book.add_order(20, 'S', 50, 19000);
    book.add_order(21, 'S', 75, 19000);
    assert(book.best_ask() == 190);
    book.delete_order(20);
    assert(book.best_ask() == 190);
    book.delete_order(21);
    assert(book.best_ask() == 0xFFFFFFFF);
}

void test_order_replace() {
    kestrel::OrderBook book;

    book.add_order(100, 'B', 500, 14000);
    assert(book.best_bid() == 140);

    book.replace_order(100, 200, 600, 14500);
    assert(book.order_count() == 1);
    assert(book.level_volume('B', 140) == 0);
    assert(book.level_volume('B', 145) == 600);
    assert(book.best_bid() == 145);

    book.replace_order(200, 300, 400, 13000);
    assert(book.order_count() == 1);
    assert(book.level_volume('B', 145) == 0);
    assert(book.level_volume('B', 130) == 400);
    assert(book.best_bid() == 130);
}

int main() {
    test_add_order_duplicate_id();
    test_deletion_followed_by_reinsertion();
    test_orders_moving_between_price_levels();
    test_bitmap_cleanup_when_level_reaches_zero();
    test_hash_table_saturation_and_probing_termination();
    test_best_bid_maintenance();
    test_best_ask_maintenance();
    test_order_replace();

    std::cout << "[+] All OrderBook correctness invariants, BBO tests, and probing safety PASSED.\n";
    return 0;
}
