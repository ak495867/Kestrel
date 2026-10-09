#include "kestrel/mmap.hpp"
#include "kestrel/parser.hpp"
#include "kestrel/order_book.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <cstring>

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

        kestrel::PcapItchParser parser;
        kestrel::OrderBook book;

        std::cout << "[*] Warmup & parsing raw packets & rebuilding Limit Order Book...\n";
        parser.parse(mmap_file.data(), mmap_file.size(), &book);

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

        std::cout << "\n================ BENCHMARK RESULTS ================\n";
        std::cout << "[+] Total Packets:    " << stats.total_packets << "\n";
        std::cout << "[+] ITCH Messages:    " << stats.itch_messages << "\n";
        std::cout << "[+] Add Orders:       " << stats.add_orders << "\n";
        std::cout << "[+] Executed Orders:  " << stats.executed_orders << "\n";
        std::cout << "[+] Canceled Orders:  " << stats.canceled_orders << "\n";
        std::cout << "[+] Deleted Orders:   " << stats.deleted_orders << "\n";
        std::cout << "[+] Replaced Orders:  " << stats.replaced_orders << "\n";
        std::cout << "[+] Best Elapsed:     " << min_elapsed << " seconds\n";
        std::cout << "[+] Peak Throughput:  " << max_throughput << " M msg/sec\n";
        std::cout << "===================================================\n";

        book.print_top(5);

    } catch (const std::exception& e) {
        std::cerr << "[-] Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
