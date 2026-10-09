#include "kestrel/order_book.hpp"
#include <cassert>
#include <iostream>

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

    book.delete_order(200);
    assert(book.order_count() == 0);
    assert(book.best_bid() == 0);
}

int main() {
    test_add_order_duplicate_id();
    test_best_bid_maintenance();
    test_best_ask_maintenance();
    test_order_replace();

    std::cout << "[+] All OrderBook BBO and duplicate ID test invariants PASSED.\n";
    return 0;
}
