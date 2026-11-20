import os
import sys
import subprocess
import time
import platform

def run_cmd(args):
    if isinstance(args, str):
        if platform.system() == "Windows":
            args = args.replace("./", ".\\")
        proc = subprocess.run(args, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    else:
        proc = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return proc.returncode, proc.stdout, proc.stderr

def build_all():
    print("[*] Compiling Kestrel binaries using clang++ (-O3, -mavx2, -std=c++20)...")
    cxx = "clang++"
    flags = "-std=c++20 -O3 -mavx2 -Iinclude"
    targets = [
        ("src/main.cpp", "kestrel_bench.exe" if platform.system() == "Windows" else "kestrel_bench"),
        ("src/test_order_book.cpp", "kestrel_ob_test.exe" if platform.system() == "Windows" else "kestrel_ob_test"),
        ("src/test_differential.cpp", "kestrel_differential_test.exe" if platform.system() == "Windows" else "kestrel_differential_test"),
        ("src/test_bypass.cpp", "kestrel_bypass_test.exe" if platform.system() == "Windows" else "kestrel_bypass_test")
    ]
    for src, out in targets:
        cmd = f"{cxx} {flags} {src} -o {out}"
        rc, stdout, stderr = run_cmd(cmd)
        if rc != 0:
            print(f"[-] Compilation failed for {src}:\n{stderr}")
            return False
        print(f"  [+] Built: {out}")
    return True

def run_suite():
    ext = ".exe" if platform.system() == "Windows" else ""
    tests = [
        ("OrderBook Correctness & Invariants", f"kestrel_ob_test{ext}"),
        ("Differential Randomized Reference Model", f"kestrel_differential_test{ext}"),
        ("Kernel-Bypass PCIe DMA Loopback Ring", f"kestrel_bypass_test{ext}")
    ]

    print("\n" + "=" * 60)
    print("RUNNING AUTOMATED TEST SUITE")
    print("=" * 60)

    all_passed = True
    for name, exe in tests:
        full_exe = os.path.abspath(exe)
        print(f"\n[*] Executing: {name} ({exe})...")
        t0 = time.time()
        rc, stdout, stderr = run_cmd([full_exe])
        elapsed = time.time() - t0
        if rc == 0:
            print(f"[+] {name}: PASSED ({elapsed:.3f} s)")
            for line in stdout.strip().splitlines()[-2:]:
                print(f"    {line}")
        else:
            print(f"[-] {name}: FAILED\n{stderr}")
            all_passed = False

    if not all_passed:
        print("\n[-] Test suite encountered failures.")
        return 1

    print("\n" + "=" * 60)
    print("RUNNING WORKLOAD BENCHMARKS")
    print("=" * 60)

    bench_exe = os.path.abspath(f"kestrel_bench{ext}")
    benchmarks = []
    if os.path.exists("sample_nasdaq.pcap"):
        benchmarks.append(("Synthetic 2M Stream", [bench_exe, "sample_nasdaq.pcap"]))
    if os.path.exists("real_nasdaq.pcap"):
        benchmarks.append(("Real NASDAQ Capture", [bench_exe, "real_nasdaq.pcap"]))

    for name, cmd in benchmarks:
        print(f"\n[*] Running Benchmark: {name}...")
        rc, stdout, stderr = run_cmd(cmd)
        if rc == 0:
            for line in stdout.splitlines():
                if any(k in line for k in ["Peak Throughput", "Latency", "Elapsed", "Processed", "VWAP", "Gaps"]):
                    print(f"    {line.strip()}")
        else:
            print(f"[-] Benchmark failed:\n{stderr}")

    print("\n[+] All verification tests and benchmarks completed successfully.")
    return 0

def main():
    if not build_all():
        return 1
    return run_suite()

if __name__ == "__main__":
    sys.exit(main())
