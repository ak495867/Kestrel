#pragma once

#include <cstdint>

namespace kestrel {

#pragma pack(push, 1)

struct MoldUDP64Header {
    uint8_t  session[10];
    uint64_t sequence_number;
    uint16_t message_count;
};

struct MoldUDP64MessageBlock {
    uint16_t message_length;
};

struct ItchMsgHeader {
    uint8_t msg_type;
};

struct ItchSystemEvent {
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    char     event_code;
};

struct ItchAddOrder {
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_reference_number;
    char     buy_sell_indicator;
    uint32_t shares;
    char     stock[8];
    uint32_t price;
};

struct ItchAddOrderMPID {
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_reference_number;
    char     buy_sell_indicator;
    uint32_t shares;
    char     stock[8];
    uint32_t price;
    char     attribution[4];
};

struct ItchOrderExecuted {
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_reference_number;
    uint32_t executed_shares;
    uint64_t match_number;
};

struct ItchOrderExecutedWithPrice {
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_reference_number;
    uint32_t executed_shares;
    uint64_t match_number;
    char     printable;
    uint32_t execution_price;
};

struct ItchOrderCancel {
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_reference_number;
    uint32_t canceled_shares;
};

struct ItchOrderDelete {
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t order_reference_number;
};

struct ItchOrderReplace {
    uint8_t  msg_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
    uint64_t original_order_reference_number;
    uint64_t new_order_reference_number;
    uint32_t shares;
    uint32_t price;
};

#pragma pack(pop)

}
