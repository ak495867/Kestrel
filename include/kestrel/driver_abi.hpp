#pragma once

#include <cstdint>
#include <cstddef>

namespace kestrel {

#define KESTREL_IOCTL_MAGIC 0x8E
#define KESTREL_IOCTL_ALLOC_RING      0x8E01
#define KESTREL_IOCTL_GET_STATS       0x8E02
#define KESTREL_IOCTL_RESET_STATS     0x8E03
#define KESTREL_IOCTL_ARM_INTERRUPT   0x8E04

#define KESTREL_RING_SIZE_BYTES (8 * 1024 * 1024)
#define KESTREL_RING_ENTRY_COUNT 262144
#define KESTREL_RING_ENTRY_MASK  (KESTREL_RING_ENTRY_COUNT - 1)

#define KESTREL_REG_BAR_INDEX 0
#define KESTREL_REG_CTRL         0x00
#define KESTREL_REG_STATUS       0x04
#define KESTREL_REG_RING_BASE_LO 0x08
#define KESTREL_REG_RING_BASE_HI 0x0C
#define KESTREL_REG_RING_SIZE    0x10
#define KESTREL_REG_HEAD_PTR     0x14
#define KESTREL_REG_TAIL_PTR     0x18
#define KESTREL_REG_DROPPED_PKTS 0x20
#define KESTREL_REG_PROCESSED    0x28

#define KESTREL_CTRL_ENABLE      (1 << 0)
#define KESTREL_CTRL_RESET       (1 << 1)
#define KESTREL_CTRL_IRQ_ENABLE  (1 << 2)

struct alignas(32) DmaOrderDescriptor {
    uint64_t order_id;
    uint32_t shares;
    uint32_t price;
    uint16_t stock_locate;
    uint8_t  side;
    uint8_t  msg_type;
    uint64_t timestamp_ns;
    uint32_t flags;
    uint32_t reserved;
};

struct KestrelUserRing {
    alignas(64) uint32_t head;
    alignas(64) uint32_t tail;
    DmaOrderDescriptor descriptors[KESTREL_RING_ENTRY_COUNT];
};

struct KestrelDriverStats {
    uint64_t total_rx_packets;
    uint64_t total_orders_parsed;
    uint64_t total_ring_overflows;
    uint64_t dma_bus_errors;
};

}
