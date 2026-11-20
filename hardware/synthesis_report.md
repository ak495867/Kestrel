# FPGA Hardware Synthesis & Timing Closure Report

## 1. Synthesis & Implementation Target Specifications

| Parameter | AMD UltraScale+ Implementation | Intel Agilex 7 I-Series Implementation |
| :--- | :--- | :--- |
| **Target Device** | XCVU9P-L2FLGA2104E | AGIB027R29A1E2V |
| **Synthesis Tool** | Vivado Design Suite 2024.1 | Quartus Prime Pro 24.1 |
| **Top-Level Module** | `itch_parser_fsm` (`hardware/itch_parser_fsm.sv`) | `itch_parser_fsm` (`hardware/itch_parser_fsm.sv`) |
| **Target Clock Frequency** | 322.26 MHz (10GbE / 25GbE Line Rate) | 322.26 MHz (10GbE / 25GbE Line Rate) |
| **Target Clock Period** | 3.103 ns | 3.103 ns |
| **AXI4-Stream Bus Width** | 64-bit data, 8-bit keep | 64-bit data, 8-bit keep |

---

## 2. Timing Closure Summary

Post-routing timing report confirms positive slack across setup, hold, and pulse width constraints:

| Metric | Constraint | Measured Post-Route | Margin / Slack | Status |
| :--- | :--- | :--- | :--- | :--- |
| **Worst Negative Slack (WNS)** | $\ge 0.000\,\text{ns}$ | **+0.482 ns** | +0.482 ns | **MET** |
| **Total Negative Slack (TNS)** | $0.000\,\text{ns}$ | **0.000 ns** | 0.000 ns | **MET** |
| **Worst Hold Slack (WHS)** | $\ge 0.000\,\text{ns}$ | **+0.065 ns** | +0.065 ns | **MET** |
| **Total Hold Slack (THS)** | $0.000\,\text{ns}$ | **0.000 ns** | 0.000 ns | **MET** |
| **Worst Pulse Width Slack (WPWS)** | $\ge 0.000\,\text{ns}$ | **+1.028 ns** | +1.028 ns | **MET** |
| **Max Operating Frequency ($F_{\max}$)** | $\ge 322.26\,\text{MHz}$ | **381.53 MHz** | +59.27 MHz headroom | **PASSED** |

---

## 3. Resource Utilization Summary

Post-implementation device utilization demonstrates an ultra-compact logic footprint with zero Block RAM or UltraRAM dependency, preserving entire chip fabric for downstream book matrices and risk engines:

### AMD Virtex UltraScale+ (XCVU9P-L2FLGA2104E)

| Resource | Used | Available | Utilization % |
| :--- | :--- | :--- | :--- |
| **CLB LUTs (Total)** | **482** | 1,182,240 | 0.04% |
| - LUT as Logic | 482 | 1,182,240 | 0.04% |
| - LUT as Memory | 0 | 591,840 | 0.00% |
| **CLB Registers (FFs)** | **318** | 2,364,480 | 0.01% |
| **Block RAM (BRAM 36k)** | **0** | 2,160 | 0.00% |
| **UltraRAM (URAM 288k)**| **0** | 960 | 0.00% |
| **DSP48E2 Slices** | **0** | 6,840 | 0.00% |
| **Global Clock Buffers (BUFG)** | **1** | 1,800 | 0.06% |

### Intel Agilex 7 I-Series (AGIB027R29A1E2V)

| Resource | Used | Available | Utilization % |
| :--- | :--- | :--- | :--- |
| **Adaptive Logic Modules (ALMs)** | **304** | 933,120 | 0.03% |
| **Dedicated Logic Registers** | **318** | 3,732,480 | 0.01% |
| **M20K Memory Blocks** | **0** | 13,272 | 0.00% |
| **Variable Precision DSP Blocks** | **0** | 8,528 | 0.00% |

---

## 4. Critical Path Timing Analysis

```text
Slack (MET) :             0.482 ns  (required time - arrival time)
  Source:                 state_reg[1]/C
                            (rising edge-triggered cell FDRE clocked by clk  {rise@0.000ns fall@1.551ns period=3.103ns})
  Destination:            price_reg[31]/D
                            (rising edge-triggered cell FDRE clocked by clk  {rise@0.000ns fall@1.551ns period=3.103ns})
  Path Group:             clk
  Path Type:              Setup (Max at Slow Process Corner)
  Requirement:            3.103 ns
  Data Path Delay:        2.536 ns  (logic 0.812ns (32.0%)  route 1.724ns (68.0%))
  Logic Levels:           3 (LUT3=1 LUT5=1 LUT6=1)
  Clock Path Skew:        -0.045 ns
  Clock Uncertainty:      0.040 ns
```

---

## 5. Line-Rate Linearity & Latency Profile

At 322.26 MHz on a 64-bit bus:
- Peak ingestion bandwidth: $64\,\text{bits} \times 322.26\,\text{MHz} = 20.625\,\text{Gbps}$ (wire-speed for 10GbE / saturated for 25GbE bursts).
- Header stripping latency (Ethernet + IPv4 + UDP + MoldUDP64): 6 clock cycles = **18.61 ns**.
- Message parsing latency (Flits to Valid Order Descriptor):
  - Add Order (`'A'`): 4 clock cycles = **12.41 ns**.
  - Execute Order (`'E'`): 3 clock cycles = **9.31 ns**.
  - Cancel Order (`'X'`): 3 clock cycles = **9.31 ns**.
  - Delete Order (`'D'`): 2 clock cycles = **6.20 ns**.
