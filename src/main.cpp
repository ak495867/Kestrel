#include "kestrel/mmap.hpp"
#include "kestrel/parser.hpp"
#include "kestrel/order_book.hpp"
#include "kestrel/spsc_queue.hpp"
#include "kestrel/affinity.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <cstring>
#include <thread>
#include <atomic>

static void generate_synthetic_pcap(const std::string& path, size_t message_count) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return;

    kestrel::PcapFileHeader file_hdr{};
    file_hdr.magic_number = 0xa1b2c3d4;
    file_hdr.version_major = 2;
    file_hdr.version_minor = 4;
    file_hdr.snaplen = 65535;
    file_hdr.network = 1;
    out.write(reinterpret_cast<const char*>(&file_hdr), sizeof(file_hdr));

    const size_t msgs_per_packet = 10;
    size_t generated = 0;
    uint64_t current_oid = 100000;

    std::vector<uint8_t> packet_buf(65536);

    while (generated < message_count) {
        size_t batch = std::min(msgs_per_packet, message_count - generated);

        size_t mold_data_size = sizeof(kestrel::MoldUDP64Header) + batch * (sizeof(kestrel::MoldUDP64MessageBlock) + sizeof(kestrel::ItchAddOrder));
        size_t udp_size = sizeof(kestrel::UdpHeader) + mold_data_size;
        size_t ip_size = sizeof(kestrel::IPv4Header) + udp_size;
        size_t eth_size = sizeof(kestrel::EthernetHeader) + ip_size;

        kestrel::PcapPacketHeader pkt_hdr{};
        pkt_hdr.ts_sec = 1700000000;
        pkt_hdr.ts_usec = static_cast<uint32_t>(generated);
        pkt_hdr.incl_len = static_cast<uint32_t>(eth_size);
        pkt_hdr.orig_len = static_cast<uint32_t>(eth_size);
        out.write(reinterpret_cast<const char*>(&pkt_hdr), sizeof(pkt_hdr));

        auto eth = reinterpret_cast<kestrel::EthernetHeader*>(packet_buf.data());
        std::memset(eth, 0, sizeof(*eth));
        eth->ether_type = kestrel::bswap16(0x0800);

        auto ip = reinterpret_cast<kestrel::IPv4Header*>(packet_buf.data() + sizeof(kestrel::EthernetHeader));
        std::memset(ip, 0, sizeof(*ip));
        ip->version_ihl = 0x45;
        ip->protocol = 17;
        ip->total_length = kestrel::bswap16(static_cast<uint16_t>(ip_size));

        auto udp = reinterpret_cast<kestrel::UdpHeader*>(packet_buf.data() + sizeof(kestrel::EthernetHeader) + sizeof(kestrel::IPv4Header));
        std::memset(udp, 0, sizeof(*udp));
        udp->src_port = kestrel::bswap16(12345);
        udp->dest_port = kestrel::bswap16(54321);
        udp->length = kestrel::bswap16(static_cast<uint16_t>(udp_size));

        uint8_t* payload = packet_buf.data() + sizeof(kestrel::EthernetHeader) + sizeof(kestrel::IPv4Header) + sizeof(kestrel::UdpHeader);
        auto mold = reinterpret_cast<kestrel::MoldUDP64Header*>(payload);
        std::memset(mold, 0, sizeof(*mold));
        mold->sequence_number = kestrel::bswap64(generated + 1);
        mold->message_count = kestrel::bswap16(static_cast<uint16_t>(batch));

        size_t offset = sizeof(kestrel::MoldUDP64Header);
        for (size_t i = 0; i < batch; ++i) {
            auto block = reinterpret_cast<kestrel::MoldUDP64MessageBlock*>(payload + offset);
            block->message_length = kestrel::bswap16(sizeof(kestrel::ItchAddOrder));
            offset += sizeof(kestrel::MoldUDP64MessageBlock);

            auto add_msg = reinterpret_cast<kestrel::ItchAddOrder*>(payload + offset);
            std::memset(add_msg, 0, sizeof(*add_msg));
            add_msg->msg_type = 'A';
            add_msg->stock_locate = kestrel::bswap16(1);
            add_msg->tracking_number = kestrel::bswap16(static_cast<uint16_t>(i));
            add_msg->order_reference_number = kestrel::bswap64(current_oid++);
            add_msg->buy_sell_indicator = (i % 2 == 0) ? 'B' : 'S';
            add_msg->shares = kestrel::bswap32(100 + static_cast<uint32_t>((i % 5) * 50));
            std::memcpy(add_msg->stock, "AAPL    ", 8);
            uint32_t base_price = (i % 2 == 0) ? 1800000 - static_cast<uint32_t>(i * 100) : 1800500 + static_cast<uint32_t>(i * 100);
            add_msg->price = kestrel::bswap32(base_price);

            offset += sizeof(kestrel::ItchAddOrder);
        }

        out.write(reinterpret_cast<const char*>(packet_buf.data()), eth_size);
        generated += batch;
    }
}

struct QueueOrderEvent {
    uint64_t order_id;
    uint32_t shares;
    uint32_t price;
    uint16_t stock_locate;
    char side;
    bool is_sentinel;
};

int main(int argc, char* argv[]) {
    std::string pcap_path = "sample_nasdaq.pcap";

    if (argc > 1) {
        pcap_path = argv[1];
    } else {
        std::cout << "[*] Generating synthetic NASDAQ ITCH PCAP (2,000,000 messages)...\n";
        generate_synthetic_pcap(pcap_path, 2000000);
    }

    try {
        std::cout << "[*] Memory-mapping file: " << pcap_path << "\n";
        kestrel::MemoryMappedFile mmap_file(pcap_path);
        std::cout << "[+] Successfully mapped " << mmap_file.size() << " bytes.\n";

        std::cout << "\n=== [MODE 1] SINGLE-THREADED APEX BENCHMARK ===\n";
        kestrel::PcapItchParser parser;
        kestrel::OrderBook warm_book;
        parser.parse(mmap_file.data(), mmap_file.size(), &warm_book);

        kestrel::ParserStats stats{};
        double min_elapsed = 1e9;
        double max_throughput = 0.0;

        for (int run = 0; run < 5; ++run) {
            kestrel::OrderBook run_book;
            auto start = std::chrono::high_resolution_clock::now();
            stats = parser.parse(mmap_file.data(), mmap_file.size(), &run_book);
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> diff = end - start;
            double elapsed_sec = diff.count();
            double throughput = stats.itch_messages / elapsed_sec / 1e6;
            if (elapsed_sec < min_elapsed) {
                min_elapsed = elapsed_sec;
                max_throughput = throughput;
            }
        }

        std::cout << "[+] Single-Threaded Best Time:       " << min_elapsed << " s\n";
        std::cout << "[+] Single-Threaded Peak Throughput: " << max_throughput << " M msg/sec\n";
        std::cout << "[+] Sequence Gaps Detected:          " << stats.sequence_gaps << "\n";

        std::cout << "\n=== [MODE 2] TWO-THREAD SPSC PIPELINE (PARSER -> ALPHA ENGINE) ===\n";
        constexpr size_t RING_CAPACITY = 1048576;
        auto queue = std::make_unique<kestrel::SPSCQueue<QueueOrderEvent, RING_CAPACITY>>();
        kestrel::OrderBook consumer_book;

        std::atomic<uint64_t> consumer_processed{0};
        std::atomic<double> vwap_sum{0.0};
        std::atomic<uint64_t> vwap_volume{0};

        auto consumer_start = std::chrono::high_resolution_clock::now();
        std::chrono::high_resolution_clock::time_point consumer_end;

        std::thread consumer([&]() {
            kestrel::set_current_thread_affinity(4);
            QueueOrderEvent evt{};
            uint64_t total = 0;
            double local_vwap_sum = 0.0;
            uint64_t local_volume = 0;

            while (true) {
                if (queue->pop(evt)) {
                    if (evt.is_sentinel) break;
                    if (__builtin_expect(total == 0, 0)) {
                        consumer_start = std::chrono::high_resolution_clock::now();
                    }
                    consumer_book.add_order(evt.order_id, evt.side, evt.shares, evt.price);
                    local_vwap_sum += static_cast<double>(evt.price) * evt.shares;
                    local_volume += evt.shares;
                    total++;
                } else {
                    _mm_pause();
                }
            }
            consumer_end = std::chrono::high_resolution_clock::now();
            consumer_processed.store(total);
            vwap_sum.store(local_vwap_sum);
            vwap_volume.store(local_volume);
        });

        std::thread producer([&]() {
            kestrel::set_current_thread_affinity(2);
            const uint8_t* data = mmap_file.data();
            size_t file_size = mmap_file.size();
            size_t offset = sizeof(kestrel::PcapFileHeader);

            while (offset + sizeof(kestrel::PcapPacketHeader) <= file_size) {
                auto pkt_hdr = reinterpret_cast<const kestrel::PcapPacketHeader*>(data + offset);
                uint32_t incl_len = pkt_hdr->incl_len;
                offset += sizeof(kestrel::PcapPacketHeader);
                if (offset + incl_len > file_size) break;

                const uint8_t* payload = data + offset + sizeof(kestrel::EthernetHeader) + sizeof(kestrel::IPv4Header) + sizeof(kestrel::UdpHeader);
                auto mold = reinterpret_cast<const kestrel::MoldUDP64Header*>(payload);
                uint16_t msg_count = kestrel::bswap16(mold->message_count);
                size_t mold_offset = sizeof(kestrel::MoldUDP64Header);

                for (uint16_t i = 0; i < msg_count; ++i) {
                    mold_offset += sizeof(kestrel::MoldUDP64MessageBlock);
                    const uint8_t* msg_bytes = payload + mold_offset;
                    auto res = kestrel::parse_add_order_fast(msg_bytes);
                    auto m = reinterpret_cast<const kestrel::ItchAddOrder*>(msg_bytes);
                    uint16_t locate = kestrel::bswap16(m->stock_locate);

                    QueueOrderEvent evt{res.order_id, res.shares, res.price, locate, res.side, false};
                    while (!queue->push(evt)) {
                        _mm_pause();
                    }

                    mold_offset += sizeof(kestrel::ItchAddOrder);
                }

                offset += incl_len;
            }

            QueueOrderEvent sentinel{0, 0, 0, 0, 0, true};
            while (!queue->push(sentinel)) {
                _mm_pause();
            }
        });

        producer.join();
        consumer.join();

        std::chrono::duration<double> diff2 = consumer_end - consumer_start;
        double spsc_elapsed = diff2.count();
        double spsc_throughput = consumer_processed.load() / spsc_elapsed / 1e6;
        double vwap = (vwap_volume.load() > 0) ? (vwap_sum.load() / vwap_volume.load() / 10000.0) : 0.0;

        std::cout << "[+] SPSC Messages Processed:        " << consumer_processed.load() << "\n";
        std::cout << "[+] SPSC Pipeline Elapsed:          " << spsc_elapsed << " s\n";
        std::cout << "[+] SPSC Cross-Core Throughput:     " << spsc_throughput << " M msg/sec\n";
        std::cout << "[+] Consumer Real-Time VWAP Metric: " << vwap << "\n";
        std::cout << "========================================================\n";

        consumer_book.print_top(5);

    } catch (const std::exception& e) {
        std::cerr << "[-] Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
