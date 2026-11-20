#include "kestrel/kernel_bypass.hpp"
#include "kestrel/order_book.hpp"
#include "kestrel/affinity.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>

static void test_ring_invariants() {
    kestrel::KernelBypassDevice dev;
    kestrel::DmaOrderDescriptor batch[128];

    size_t initial_poll = dev.poll_batch(batch, 128);
    assert(initial_poll == 0);

    kestrel::DmaOrderDescriptor sample{};
    sample.order_id = 999;
    sample.shares = 100;
    sample.price = 15000;
    sample.side = 'B';
    sample.msg_type = 'A';

    bool injected = dev.mock_inject(sample);
    assert(injected);

    size_t polled = dev.poll_batch(batch, 128);
    assert(polled == 1);
    assert(batch[0].order_id == 999);
    assert(batch[0].shares == 100);
    assert(batch[0].price == 15000);
    assert(batch[0].side == 'B');

    size_t empty_again = dev.poll_batch(batch, 128);
    assert(empty_again == 0);
}

int main() {
    test_ring_invariants();

    kestrel::KernelBypassDevice dev;
    kestrel::OrderBook book;

    std::cout << "[*] Running Kernel-Bypass PCIe DMA Consumer Benchmark ("
              << (dev.is_hardware_bound() ? "Physical /dev/kestrel_uio Device" : "Software Loopback Ring Emulation")
              << ")...\n";

    constexpr size_t TEST_ORDERS = 2000000;
    constexpr size_t BATCH_SIZE = 64;

    std::atomic<bool> producer_done{false};
    std::atomic<size_t> total_ingested{0};

    auto start_time = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point end_time;

    std::thread consumer([&]() {
        kestrel::set_current_thread_affinity(4);
        kestrel::DmaOrderDescriptor batch[BATCH_SIZE];
        size_t processed = 0;

        while (processed < TEST_ORDERS) {
            size_t n = dev.poll_batch(batch, BATCH_SIZE);
            if (n == 0) {
                _mm_pause();
                continue;
            }
            for (size_t i = 0; i < n; ++i) {
                book.add_order(batch[i].order_id, static_cast<char>(batch[i].side), batch[i].shares, batch[i].price);
                processed++;
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        total_ingested.store(processed);
    });

    std::thread hardware_emitter([&]() {
        kestrel::set_current_thread_affinity(2);
        size_t sent = 0;
        kestrel::DmaOrderDescriptor desc{};
        desc.shares = 100;
        desc.price = 1800000;
        desc.stock_locate = 1;
        desc.msg_type = 'A';

        while (sent < TEST_ORDERS) {
            desc.order_id = 5000000 + sent;
            desc.side = (sent % 2 == 0) ? 'B' : 'S';
            while (!dev.mock_inject(desc)) {
                _mm_pause();
            }
            sent++;
        }
        producer_done.store(true);
    });

    hardware_emitter.join();
    consumer.join();

    std::chrono::duration<double> diff = end_time - start_time;
    double throughput = total_ingested.load() / diff.count() / 1e6;

    std::cout << "[+] Kernel-Bypass Ingestion Messages: " << total_ingested.load() << "\n";
    std::cout << "[+] Ingestion Elapsed:                " << diff.count() << " s\n";
    std::cout << "[+] Kernel-Bypass Ingestion Rate:     " << throughput << " M desc/sec\n";
    std::cout << "[+] Average Latency:                  " << (diff.count() / total_ingested.load() * 1e9) << " ns/desc\n";

    book.print_top(3);

    return 0;
}
