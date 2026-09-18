#!/usr/bin/env python3
"""
plot_benchmark.py - Plot UFS/SSD write performance curve from fast_fill_bench CSV log.
Usage:
    python3 plot_benchmark.py [path/to/fill_bench.csv]
"""

import sys
import os
import pandas as pd
import matplotlib.pyplot as plt

def main():
    csv_file = sys.argv[1] if len(sys.argv) > 1 else "fill_bench.csv"
    if not os.path.exists(csv_file):
        print(f"Error: CSV file '{csv_file}' not found.")
        sys.exit(1)

    df = pd.read_csv(csv_file)
    print(f"[+] Loaded {len(df)} records from {csv_file}")

    fig, ax1 = plt.subplots(figsize=(13, 6), dpi=150)

    # Plot instantaneous speed and average speed
    line1 = ax1.plot(df["total_written_gb"], df["block_speed_mbs"], 
                     color="#e83e8c", alpha=0.75, linewidth=1.2, label="Instant Speed (MB/s)")
    line2 = ax1.plot(df["total_written_gb"], df["avg_speed_mbs"], 
                     color="#0d6efd", linewidth=2.0, label="Cumulative Avg Speed (MB/s)")

    ax1.set_xlabel("Total Written (GB)", fontsize=12, fontweight="bold")
    ax1.set_ylabel("Write Speed (MB/s)", fontsize=12, fontweight="bold")
    ax1.grid(True, linestyle="--", alpha=0.5)

    # Plot remaining space on secondary axis
    ax2 = ax1.twinx()
    line3 = ax2.plot(df["total_written_gb"], df["free_space_gb"], 
                     color="#ffc107", linestyle=":", linewidth=1.5, label="Free Space (GB)")
    ax2.set_ylabel("Free Space (GB)", fontsize=12, color="#856404")

    # Combine legends
    lines = line1 + line2 + line3
    labels = [l.get_label() for l in lines]
    ax1.legend(lines, labels, loc="upper right", framealpha=0.9)

    plt.title("UFS Storage Sustained Full-Disk Write Benchmark", fontsize=14, fontweight="bold", pad=12)
    plt.tight_layout()

    out_png = os.path.splitext(csv_file)[0] + "_curve.png"
    plt.savefig(out_png)
    print(f"[+] Chart successfully saved to: {out_png}")

if __name__ == "__main__":
    main()
