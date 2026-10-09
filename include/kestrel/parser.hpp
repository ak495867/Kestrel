#pragma once

#include "pcap.hpp"
#include "itch.hpp"
#include "endian.hpp"
#include "order_book.hpp"
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <array>

namespace kestrel {

struct ParserStats {
    uint64_t total_packets{0};
    uint64_t itch_messages{0};
    uint64_t add_orders{0};
    uint64_t executed_orders{0};
    uint64_t canceled_orders{0};
    uint64_t deleted_orders{0};
    uint64_t replaced_orders{0};
    uint64_t other_messages{0};
    uint64_t sequence_gaps{0};
};

class PcapItchParser {
public:
    static constexpr size_t MAX_LOCATES = 65536;

    PcapItchParser() {
        books_ = new OrderBook*[MAX_LOCATES]();
    }

    ~PcapItchParser() {
        for (size_t i = 0; i < MAX_LOCATES; ++i) {
            delete books_[i];
        }
        delete[] books_;
    }

    OrderBook& get_book(uint16_t stock_locate) noexcept {
        if (!books_[stock_locate]) {
            books_[stock_locate] = new OrderBook();
        }
        return *books_[stock_locate];
    }

    ParserStats parse(const uint8_t* data, size_t file_size, OrderBook* default_book = nullptr) {
        ParserStats stats{};
        if (file_size < sizeof(PcapFileHeader)) return stats;

        auto pcap_hdr = reinterpret_cast<const PcapFileHeader*>(data);
        bool swap_pcap = (pcap_hdr->magic_number == 0xd4c3b2a1);

        size_t offset = sizeof(PcapFileHeader);
        uint64_t expected_seq = 0;

        while (offset + sizeof(PcapPacketHeader) <= file_size) {
            _mm_prefetch(reinterpret_cast<const char*>(data + offset + 256), _MM_HINT_NTA);

            auto pkt_hdr = reinterpret_cast<const PcapPacketHeader*>(data + offset);
            uint32_t incl_len = swap_pcap ? bswap32(pkt_hdr->incl_len) : pkt_hdr->incl_len;
            offset += sizeof(PcapPacketHeader);

            if (offset + incl_len > file_size) break;
            stats.total_packets++;

            size_t pkt_offset = 0;
            if (incl_len < sizeof(EthernetHeader)) {
                offset += incl_len;
                continue;
            }

            auto eth = reinterpret_cast<const EthernetHeader*>(data + offset);
            pkt_offset += sizeof(EthernetHeader);

            uint16_t ether_type = bswap16(eth->ether_type);
            if (ether_type != 0x0800) {
                offset += incl_len;
                continue;
            }

            if (pkt_offset + sizeof(IPv4Header) > incl_len) {
                offset += incl_len;
                continue;
            }

            auto ip = reinterpret_cast<const IPv4Header*>(data + offset + pkt_offset);
            uint8_t ihl = (ip->version_ihl & 0x0F) * 4;
            if (ip->protocol != 17 || pkt_offset + ihl + sizeof(UdpHeader) > incl_len) {
                offset += incl_len;
                continue;
            }
            pkt_offset += ihl;

            auto udp = reinterpret_cast<const UdpHeader*>(data + offset + pkt_offset);
            uint16_t udp_len = bswap16(udp->length);
            pkt_offset += sizeof(UdpHeader);

            if (udp_len < sizeof(UdpHeader)) {
                offset += incl_len;
                continue;
            }
            size_t payload_len = udp_len - sizeof(UdpHeader);
            if (pkt_offset + payload_len > incl_len) {
                payload_len = incl_len - pkt_offset;
            }

            const uint8_t* payload = data + offset + pkt_offset;
            parse_mold_payload(payload, payload_len, stats, default_book, expected_seq);

            offset += incl_len;
        }

        return stats;
    }

private:
    inline void parse_mold_payload(const uint8_t* payload, size_t len, ParserStats& stats, OrderBook* default_book, uint64_t& expected_seq) {
        if (len < sizeof(MoldUDP64Header)) return;

        auto mold = reinterpret_cast<const MoldUDP64Header*>(payload);
        uint64_t seq = bswap64(mold->sequence_number);
        uint16_t msg_count = bswap16(mold->message_count);
        size_t mold_offset = sizeof(MoldUDP64Header);

        if (msg_count == 0xFFFF) return;

        if (expected_seq != 0 && seq != expected_seq) {
            stats.sequence_gaps++;
        }
        expected_seq = seq + msg_count;

        for (uint16_t i = 0; i < msg_count && mold_offset + sizeof(MoldUDP64MessageBlock) <= len; ++i) {
            auto block = reinterpret_cast<const MoldUDP64MessageBlock*>(payload + mold_offset);
            uint16_t msg_len = bswap16(block->message_length);
            mold_offset += sizeof(MoldUDP64MessageBlock);

            if (mold_offset + msg_len > len) break;

            const uint8_t* msg_bytes = payload + mold_offset;
            if (i + 1 < msg_count && mold_offset + msg_len < len) {
                _mm_prefetch(reinterpret_cast<const char*>(payload + mold_offset + msg_len), _MM_HINT_NTA);
            }

            if (__builtin_expect(msg_len == 0, 0)) {
                mold_offset += msg_len;
                continue;
            }

            stats.itch_messages++;
            uint8_t type = msg_bytes[0];

            switch (type) {
                case 'A': {
                    if (__builtin_expect(msg_len < sizeof(ItchAddOrder), 0)) break;
                    stats.add_orders++;
                    auto m = reinterpret_cast<const ItchAddOrder*>(msg_bytes);
                    uint16_t locate = bswap16(m->stock_locate);
                    OrderBook& book = default_book ? *default_book : get_book(locate);
                    auto res = parse_add_order_fast(msg_bytes);
                    book.add_order(res.order_id, res.side, res.shares, res.price);
                    break;
                }
                case 'F': {
                    if (__builtin_expect(msg_len < sizeof(ItchAddOrderMPID), 0)) break;
                    stats.add_orders++;
                    auto m = reinterpret_cast<const ItchAddOrderMPID*>(msg_bytes);
                    uint16_t locate = bswap16(m->stock_locate);
                    OrderBook& book = default_book ? *default_book : get_book(locate);
                    uint64_t oid = bswap64(m->order_reference_number);
                    uint32_t shares = bswap32(m->shares);
                    uint32_t px = bswap32(m->price);
                    book.add_order(oid, m->buy_sell_indicator, shares, px);
                    break;
                }
                case 'E': {
                    if (__builtin_expect(msg_len < sizeof(ItchOrderExecuted), 0)) break;
                    stats.executed_orders++;
                    auto m = reinterpret_cast<const ItchOrderExecuted*>(msg_bytes);
                    uint16_t locate = bswap16(m->stock_locate);
                    OrderBook& book = default_book ? *default_book : get_book(locate);
                    uint64_t oid = bswap64(m->order_reference_number);
                    uint32_t shares = bswap32(m->executed_shares);
                    book.execute_order(oid, shares);
                    break;
                }
                case 'C': {
                    if (__builtin_expect(msg_len < sizeof(ItchOrderExecutedWithPrice), 0)) break;
                    stats.executed_orders++;
                    auto m = reinterpret_cast<const ItchOrderExecutedWithPrice*>(msg_bytes);
                    uint16_t locate = bswap16(m->stock_locate);
                    OrderBook& book = default_book ? *default_book : get_book(locate);
                    uint64_t oid = bswap64(m->order_reference_number);
                    uint32_t shares = bswap32(m->executed_shares);
                    book.execute_order(oid, shares);
                    break;
                }
                case 'X': {
                    if (__builtin_expect(msg_len < sizeof(ItchOrderCancel), 0)) break;
                    stats.canceled_orders++;
                    auto m = reinterpret_cast<const ItchOrderCancel*>(msg_bytes);
                    uint16_t locate = bswap16(m->stock_locate);
                    OrderBook& book = default_book ? *default_book : get_book(locate);
                    uint64_t oid = bswap64(m->order_reference_number);
                    uint32_t shares = bswap32(m->canceled_shares);
                    book.cancel_order(oid, shares);
                    break;
                }
                case 'D': {
                    if (__builtin_expect(msg_len < sizeof(ItchOrderDelete), 0)) break;
                    stats.deleted_orders++;
                    auto m = reinterpret_cast<const ItchOrderDelete*>(msg_bytes);
                    uint16_t locate = bswap16(m->stock_locate);
                    OrderBook& book = default_book ? *default_book : get_book(locate);
                    uint64_t oid = bswap64(m->order_reference_number);
                    book.delete_order(oid);
                    break;
                }
                case 'U': {
                    if (__builtin_expect(msg_len < sizeof(ItchOrderReplace), 0)) break;
                    stats.replaced_orders++;
                    auto m = reinterpret_cast<const ItchOrderReplace*>(msg_bytes);
                    uint16_t locate = bswap16(m->stock_locate);
                    OrderBook& book = default_book ? *default_book : get_book(locate);
                    uint64_t old_oid = bswap64(m->original_order_reference_number);
                    uint64_t new_oid = bswap64(m->new_order_reference_number);
                    uint32_t shares = bswap32(m->shares);
                    uint32_t px = bswap32(m->price);
                    book.replace_order(old_oid, new_oid, shares, px);
                    break;
                }
                default: {
                    stats.other_messages++;
                    break;
                }
            }

            mold_offset += msg_len;
        }
    }

    OrderBook** books_{nullptr};
};

}
