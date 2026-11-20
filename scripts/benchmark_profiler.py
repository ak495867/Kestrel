import os
import sys
import platform
import subprocess
import time
import math
import re

def get_cpu_info():
    info = {
        "model": platform.processor() or "Unknown x86_64",
        "cores_logical": os.cpu_count() or 1,
        "arch": platform.machine(),
        "system": platform.system(),
        "release": platform.release(),
    }
    if platform.system() == "Windows":
        try:
            ps_cmd = "powershell -NoProfile -Command \"Get-CimInstance Win32_Processor | Select-Object -Property Name,MaxClockSpeed,NumberOfCores,L2CacheSize,L3CacheSize | ConvertTo-Json\""
            out = subprocess.check_output(ps_cmd, shell=True, text=True, stderr=subprocess.DEVNULL)
            import json
            data = json.loads(out)
            if isinstance(data, list):
                data = data[0]
            if "Name" in data and data["Name"]:
                info["model"] = str(data["Name"]).strip()
            if "MaxClockSpeed" in data and data["MaxClockSpeed"]:
                info["mhz"] = float(data["MaxClockSpeed"])
            if "NumberOfCores" in data and data["NumberOfCores"]:
                info["cores_physical"] = int(data["NumberOfCores"])
            if "L2CacheSize" in data and data["L2CacheSize"]:
                info["l2_kb"] = int(data["L2CacheSize"])
            if "L3CacheSize" in data and data["L3CacheSize"]:
                info["l3_kb"] = int(data["L3CacheSize"])
        except Exception:
            pass
    elif platform.system() == "Linux":
        try:
            with open("/proc/cpuinfo", "r") as f:
                for line in f:
                    if "model name" in line:
                        info["model"] = line.split(":", 1)[1].strip()
                        break
        except Exception:
            pass
    return info

def print_cache_architecture_analysis(cpu_info):
    print("=" * 70)
    print("KESTREL CACHE HIERARCHY & MEMORY FOOTPRINT PROFILING")
    print("=" * 70)
    print(f"Host Processor:         {cpu_info.get('model', 'Unknown')}")
    print(f"Architecture:           {cpu_info.get('arch', 'Unknown')}")
    print(f"Logical Cores:          {cpu_info.get('cores_logical', 'Unknown')}")
    if "cores_physical" in cpu_info:
        print(f"Physical Cores:         {cpu_info.get('cores_physical')}")
    if "mhz" in cpu_info:
        print(f"Clock Frequency:        {cpu_info.get('mhz'):.1f} MHz")
    if "l2_kb" in cpu_info:
        print(f"L2 Cache Capacity:      {cpu_info.get('l2_kb')} KB per core")
    if "l3_kb" in cpu_info:
        print(f"L3 Cache Capacity:      {cpu_info.get('l3_kb')} KB shared")

    print("\n--- OrderBook Data Structure Sizing ---")
    keys_mb = (4194304 * 8) / (1024 * 1024)
    shares_mb = (4194304 * 4) / (1024 * 1024)
    prices_mb = (4194304 * 4) / (1024 * 1024)
    levels_mb = (500000 * 8 * 2) / (1024 * 1024)
    bitmaps_kb = (16384 * 8 * 2 + 256 * 8 * 2) / 1024
    total_mb = keys_mb + shares_mb + prices_mb + levels_mb + (bitmaps_kb / 1024)

    print(f"  keys_ array (4,194,304 x 8B):       {keys_mb:.2f} MB")
    print(f"  shares_ array (4,194,304 x 4B):     {shares_mb:.2f} MB")
    print(f"  prices_ array (4,194,304 x 4B):     {prices_mb:.2f} MB")
    print(f"  bid/ask levels (2 x 500k x 8B):     {levels_mb:.2f} MB")
    print(f"  BBO hierarchical bitmaps:           {bitmaps_kb:.2f} KB")
    print(f"  Total Structure-of-Arrays Size:     {total_mb:.2f} MB")

    print("\n--- Cache Locality & Memory Subsystem Analysis ---")
    print("  L1 Cache Line Density: 8 consecutive 64-bit keys fit in a single 64B cacheline")
    print("  Wire Streaming:        _MM_HINT_NTA non-temporal prefetching bypasses L1/L2 thrashing")
    print("  BBO Acceleration:      2-level hierarchical bitmask (256B summary + 128KB leaf) resides in L2/L3")
    print("  Cross-Core SPSC Queue: Ring buffer aligns producer/consumer head & tail to independent 64B lines")
    print("=" * 70)

def run_benchmark(binary, args=None, runs=3):
    cmd = [binary] + (args or [])
    throughputs = []
    latencies = []
    raw_outputs = []

    for r in range(runs):
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        raw_outputs.append(proc.stdout)
        tp_match = re.search(r"Peak Throughput:\s*([\d\.]+)\s*M msg/sec", proc.stdout)
        lat_match = re.search(r"Latency:\s*([\d\.]+)\s*ns/msg", proc.stdout)
        if tp_match:
            throughputs.append(float(tp_match.group(1)))
        if lat_match:
            latencies.append(float(lat_match.group(1)))

    return throughputs, latencies, raw_outputs

def main():
    cpu = get_cpu_info()
    print_cache_architecture_analysis(cpu)

    exe = "./kestrel_bench.exe" if platform.system() == "Windows" else "./kestrel_bench"
    if not os.path.exists(exe):
        exe = "./build/kestrel_bench.exe" if platform.system() == "Windows" else "./build/kestrel_bench"

    if not os.path.exists(exe):
        print(f"[-] Executable {exe} not found. Please compile first.")
        return 1

    targets = []
    if os.path.exists("sample_nasdaq.pcap"):
        targets.append(("sample_nasdaq.pcap", "Synthetic High-Density ITCH Stream"))
    if os.path.exists("real_nasdaq.pcap"):
        targets.append(("real_nasdaq.pcap", "Real Recorded NASDAQ Exchange Capture"))

    for pcap, desc in targets:
        print(f"\n[*] Benchmarking: {pcap} ({desc})...")
        tps, lats, outputs = run_benchmark(exe, [pcap], runs=3)
        if tps:
            avg_tp = sum(tps) / len(tps)
            max_tp = max(tps)
            min_tp = min(tps)
            avg_lat = sum(lats) / len(lats) if lats else 0.0
            print(f"  Runs:               {len(tps)}")
            print(f"  Peak Throughput:    {max_tp:.2f} M msg/sec")
            print(f"  Avg Throughput:     {avg_tp:.2f} M msg/sec")
            print(f"  Min Throughput:     {min_tp:.2f} M msg/sec")
            print(f"  Avg Latency:        {avg_lat:.2f} ns/msg")
            if "mhz" in cpu:
                cycles_per_msg = (avg_lat * 1e-9) * (cpu["mhz"] * 1e6)
                print(f"  Estimated Cycles:   {cycles_per_msg:.2f} cycles/msg @ {cpu['mhz']:.0f} MHz")

    return 0

if __name__ == "__main__":
    sys.exit(main())
