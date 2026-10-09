#include "kestrel/kernel_bypass.hpp"
#include "kestrel/order_book.hpp"
#include <iostream>
#include <chrono>
#include <vector>

int main() {
    kestrel::KernelBypassDevice dev;
    kestrel::OrderBook book;

    constexpr size_t TEST_ORDERS = 1000000;
    constexpr size_t BATCH_SIZE = 64;
    kestrel::DmaOrderDescriptor batch[BATCH_SIZE];

    size_t generated = 0;
    size_t processed = 0;

    auto start = std::chrono::high_resolution_clock::now();
    while (processed < TEST_ORDERS) {
        while (generated < TEST_ORDERS && (generated - processed) < (KESTREL_RING_ENTRY_COUNT - 128)) {
            size_t to_gen = std::min(size_t(64), TEST_ORDERS - generated);
            for (size_t i = 0; i < to_gen; ++i) {
                kestrel::DmaOrderDescriptor desc{};
                desc.order_id = 5000000 + generated + i;
                desc.shares = 100;
                desc.price = 1800000;
                desc.stock_locate = 1;
                desc.side = ((generated + i) % 2 == 0) ? 'B' : 'S';
                desc.msg_type = 'A';
                dev.mock_inject(desc);
            }
            generated += to_gen;
        }

        size_t n = dev.poll_batch(batch, BATCH_SIZE);
        for (size_t i = 0; i < n; ++i) {
            book.add_order(batch[i].order_id, static_cast<char>(batch[i].side), batch[i].shares, batch[i].price);
            processed++;
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> diff = end - start;
    double throughput = processed / diff.count() / 1e6;

    std::cout << "[+] Kernel-Bypass Ingestion Messages: " << processed << "\n";
    std::cout << "[+] Ingestion Elapsed:                " << diff.count() << " s\n";
    std::cout << "[+] Kernel-Bypass Ingestion Rate:     " << throughput << " M desc/sec\n";
    book.print_top(3);

    return 0;
}
