#include "kestrel/pcap.hpp"
#include "kestrel/itch.hpp"
#include "kestrel/endian.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>

int main() {
    std::ifstream in("real_itch_slice.bin", std::ios::binary);
    if (!in) {
        std::cerr << "[-] Failed to open real_itch_slice.bin\n";
        return 1;
    }

    std::ofstream out("real_nasdaq.pcap", std::ios::binary);
    if (!out) {
        std::cerr << "[-] Failed to open real_nasdaq.pcap\n";
        return 1;
    }

    kestrel::PcapFileHeader file_hdr{};
    file_hdr.magic_number = 0xa1b2c3d4;
    file_hdr.version_major = 2;
    file_hdr.version_minor = 4;
    file_hdr.snaplen = 65535;
    file_hdr.network = 1;
    out.write(reinterpret_cast<const char*>(&file_hdr), sizeof(file_hdr));

    std::vector<uint8_t> packet_buf(65536);
    std::vector<uint8_t> msg_buf(65536);

    uint64_t seq_num = 1;
    uint32_t pkt_count = 0;
    uint32_t total_msgs = 0;

    while (in.peek() != EOF) {
        uint8_t len_bytes[2];
        if (!in.read(reinterpret_cast<char*>(len_bytes), 2)) break;
        uint16_t msg_len = (static_cast<uint16_t>(len_bytes[0]) << 8) | len_bytes[1];
        if (msg_len == 0 || msg_len > 1024) break;

        if (!in.read(reinterpret_cast<char*>(msg_buf.data()), msg_len)) break;

        size_t mold_data_size = sizeof(kestrel::MoldUDP64Header) + sizeof(kestrel::MoldUDP64MessageBlock) + msg_len;
        size_t udp_size = sizeof(kestrel::UdpHeader) + mold_data_size;
        size_t ip_size = sizeof(kestrel::IPv4Header) + udp_size;
        size_t eth_size = sizeof(kestrel::EthernetHeader) + ip_size;

        kestrel::PcapPacketHeader pkt_hdr{};
        pkt_hdr.ts_sec = 1580385600;
        pkt_hdr.ts_usec = pkt_count;
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
        mold->sequence_number = kestrel::bswap64(seq_num++);
        mold->message_count = kestrel::bswap16(1);

        auto block = reinterpret_cast<kestrel::MoldUDP64MessageBlock*>(payload + sizeof(kestrel::MoldUDP64Header));
        block->message_length = kestrel::bswap16(msg_len);

        std::memcpy(payload + sizeof(kestrel::MoldUDP64Header) + sizeof(kestrel::MoldUDP64MessageBlock), msg_buf.data(), msg_len);

        out.write(reinterpret_cast<const char*>(packet_buf.data()), eth_size);
        pkt_count++;
        total_msgs++;
    }

    std::cout << "[+] Successfully encapsulated " << total_msgs << " real NASDAQ ITCH 5.0 messages into real_nasdaq.pcap\n";
    return 0;
}
