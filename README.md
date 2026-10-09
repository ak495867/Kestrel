# Kestrel: Wire-Speed NASDAQ ITCH 5.0 PCAP Engine (C++20 / SIMD / SystemVerilog)

[![Language](https://img.shields.io/badge/Language-C%2B%2B20-00599C?style=flat-square&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/20)
[![Hardware](https://img.shields.io/badge/Hardware-SystemVerilog%20%2F%20AXI4--Stream-orange?style=flat-square)]()
[![Single-Thread](https://img.shields.io/badge/Single--Thread%20Peak-127.3M%20msg%2Fsec-brightgreen?style=flat-square)]()
[![SPSC Cross-Core](https://img.shields.io/badge/SPSC%20Cross--Core-99.2M%20msg%2Fsec-blue?style=flat-square)]()

An ultra-low latency, zero-copy, memory-mapped NASDAQ TotalView-ITCH 5.0 order book engine, PCAP parser, and SystemVerilog FPGA parser core. Built to bypass OS kernel overhead, eliminate cache thrashing, and process raw network streams at wire speed.

---

## Performance Benchmarks

Measured on raw multi-gigabit PCAP packet streams with live Limit Order Book state reconstruction, multi-asset locate demuxing, transport gap verification, and online signal tracking:

| Mode | Throughput | Latency / Msg | Execution Details |
| :--- | :--- | :--- | :--- |
| **Single-Threaded Apex** | **127.34 M msg/sec** | **~7.85 ns** | In-place zero-copy parsing + SoA flat hash table + bitmap BBO |
| **Two-Thread SPSC Pipeline** | **99.23 M msg/sec** | **~10.08 ns** | Core-pinned batched producer-consumer ring buffer + online VWAP |

```text
=== [MODE 1] SINGLE-THREADED APEX BENCHMARK ===
[+] Single-Threaded Best Time:       0.015706 s
[+] Single-Threaded Peak Throughput: 127.34 M msg/sec
[+] Sequence Gaps Detected:          0

=== [MODE 2] TWO-THREAD SPSC PIPELINE (PARSER -> ALPHA ENGINE) ===
[+] SPSC Messages Processed:        2000000
[+] SPSC Pipeline Elapsed:          0.0201553 s
[+] SPSC Cross-Core Throughput:     99.2295 M msg/sec
[+] Consumer Real-Time VWAP Metric: 180.03
```

---

## Architecture & Data Flow

```mermaid
flowchart TD
    subgraph INGESTION["Network & Storage Ingestion"]
        A["Raw NASDAQ ITCH 5.0 PCAP File"] --> B["MemoryMappedFile (Virtual Memory / MapViewOfFile)"]
    end

    subgraph PIPELINE["Core-Pinned Cross-Core Pipeline"]
        B --> C["Core 2: Producer Thread"]
        C --> D["Zero-Copy Packet Header Stripper"]
        D --> E["SIMD / MOVBE Hardware Endian Inversion"]
        E --> F["Batched Lock-Free SPSC Ring Buffer (alignas 64)"]
        F --> G["Core 4: Consumer Thread"]
        G --> H["SoA Open-Addressed Order Book (Bitmapped BBO)"]
        G --> I["Real-Time Online VWAP Engine"]
    end

    subgraph HARDWARE["FPGA Offload Option (SystemVerilog)"]
        J["10GbE MAC (AXI4-Stream 64-bit)"] --> K["itch_parser_fsm.sv"]
        K --> L["Zero-Cycle Endian Permutation Wires"]
        L --> M["PCIe DMA Engine -> SPSC Queue"]
    end
```

---

## Detailed Pipeline Breakdown

```mermaid
sequenceDiagram
    autonumber
    participant Wire as PCAP / NIC Wire
    participant P as Core 2 (Producer)
    participant Q as SPSC Ring Buffer
    participant C as Core 4 (Consumer)
    participant LOB as SoA LOB
    participant BBO as Bitmap BBO

    Wire->>P: Zero-Copy Pointer (Ethernet / IP / UDP / MoldUDP64)
    Note over P: In-place struct overlay & MOVBE endian swap
    P->>Q: push_batch(events[64])
    Note over Q: MESI cacheline isolated (64B padding), batched release
    Q->>C: pop_batch(events[64])
    C->>LOB: add_order(order_id, side, shares, price)
    C->>BBO: Level update & bitmask scan (__builtin_clzll / ctzll)
    C->>C: Rolling VWAP update (local L1 accumulator)
```

---

## Architectural Highlights

### 1. Structure-of-Arrays (SoA) Open-Addressed Order Book
Order tracking uses a flat Structure-of-Arrays (SoA) open-addressed hash map separating 64-bit keys from quantities and packed prices:
- Linear probe loops scan pure 64-bit integer vectors (`8 bytes` per slot), fitting 8 candidate keys per 64-byte L1 cacheline.
- Folded single-cycle hashing `(order_id ^ (order_id >> 16)) & MASK` eliminates integer division and multi-cycle arithmetic stalls.
- Deleted entries are marked with tombstones and reclaimed dynamically on subsequent insertions.

### 2. $O(1)$ Bitmapped BBO Tracking
- Price levels are mirrored across 64-bit bitmasks (`bid_bitmap_`, `ask_bitmap_`).
- Level exhaustion and cancellations update masks in a single bitwise operation.
- Best Bid and Offer (BBO) lookups resolve via hardware bit-scan intrinsics (`__builtin_clzll` and `__builtin_ctzll`), eliminating linear price ladder traversal.

### 3. Batched Lock-Free SPSC Ring Buffer
Cross-core pipelining between the packet parser (Core 2) and the order book engine (Core 4) utilizes chunked block transfers:
- Producer and consumer exchange events via `push_batch()` and `pop_batch()` in 64-element blocks.
- Amortizes atomic `memory_order_release` and `memory_order_acquire` memory barriers across batches, preventing inter-core MESI cacheline invalidation storms.

### 4. Zero-Copy Memory Mapping (`mmap` / `MapViewOfFile`)
No user-to-kernel copies via `fread` or `std::ifstream`. The raw capture file is mapped directly into virtual address space via Win32 `CreateFileMapping` / `MapViewOfFile` (`FILE_FLAG_SEQUENTIAL_SCAN`) and POSIX `mmap` (`POSIX_MADV_SEQUENTIAL`, `MADV_HUGEPAGE`), eliminating kernel page thrashing.

### 5. Multi-Asset Locate Demuxing & Sequence Validation
- Ingested messages are demultiplexed by 16-bit `stock_locate` identifiers into dedicated per-instrument books.
- MoldUDP64 packet headers are monitored continuously against `expected_seq` to detect upstream drops and network transport gaps.

### 6. Hardware Endianness Inversion & Struct Overlays
- Network framing is overlaid directly via contiguous `#pragma pack(push, 1)` structs.
- Big-endian wire fields are reversed via hardware `MOVBE` instructions and compiler builtins (`__builtin_bswap16`, `__builtin_bswap32`, `__builtin_bswap64`).

### 7. Non-Temporal Memory Prefetching (`_MM_HINT_NTA`)
Prevents multi-gigabyte PCAP packet streaming from thrashing CPU L2/L3 caches:
- Employs non-temporal cache line prefetch hints (`_mm_prefetch(..., _MM_HINT_NTA)`) to load incoming packet blocks directly through streaming buffers.
- Preserves CPU L2/L3 cache capacity exclusively for active order book tables.

### 8. SystemVerilog AXI4-Stream Hardware Core (`hardware/`)
Includes a synthesizable line-rate hardware parser FSM targeting 10GbE / 25GbE FPGA SmartNIC MAC interfaces:
- Ingests 64-bit AXI4-Stream flits clocked at 322.26 MHz.
- Real-time hardware header stripping across Ethernet, IPv4, UDP, and MoldUDP64.
- Zero-cycle endianness transformation using physical wire routing.
- Validated testbench provided in `hardware/tb_itch_parser_fsm.sv`.

---

## Directory Layout

```text
Kestrel/
├── include/
│   └── kestrel/
│       ├── affinity.hpp        # OS thread CPU core pinning
│       ├── endian.hpp          # Hardware MOVBE and byte-swap utilities
│       ├── itch.hpp            # ITCH 5.0 and MoldUDP64 wire structures
│       ├── mmap.hpp            # OS virtual memory-mapped file wrapper
│       ├── order_book.hpp      # SoA open-addressed Limit Order Book with bitmapped BBO
│       ├── parser.hpp          # Multi-asset locate-demuxing zero-copy parser
│       ├── pcap.hpp            # Packed PCAP, Ethernet, IPv4, UDP framing
│       └── spsc_queue.hpp      # Batched cacheline-padded lock-free ring buffer
├── src/
│   └── main.cpp                # Dual-mode benchmark harness
├── hardware/
│   ├── itch_parser_fsm.sv      # Synthesizable AXI4-Stream ITCH parser FSM
│   └── tb_itch_parser_fsm.sv   # SystemVerilog testbench harness
├── CMakeLists.txt              # C++20, -O3, -flto, -mavx2 build pipeline
└── README.md
```

---

## Build & Run

### Prerequisites
- C++20 compliant compiler (Clang 16+, GCC 12+, MSVC 2022)
- CMake 3.20+
- Ninja or Make

### Compilation
```bash
cmake -B build -G "Ninja" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Execution
```bash
# Run benchmark on generated/cached dataset
./build/kestrel_bench

# Or run against an external recorded NASDAQ ITCH PCAP file
./build/kestrel_bench /path/to/capture.pcap
```
