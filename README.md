# FastFillBench (fast_fill_bench)

<p align="center">
  <b>专为 Android (Termux ARM64) 与 Linux 设计的超高效无特权闪存全盘写入测试与监控工具</b>
  <br />
  <i>A high-efficiency, unprivileged flash storage fill benchmark and throughput logger.</i>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Platform-Android%20Termux%20%7C%20Linux-brightgreen?style=flat-square" alt="Platform" />
  <img src="https://img.shields.io/badge/Arch-ARM64%20%7C%20x86__64-blue?style=flat-square" alt="Arch" />
  <img src="https://img.shields.io/badge/Language-C99-orange?style=flat-square" alt="Language" />
  <img src="https://img.shields.io/badge/Binary%20Size-~75KB-success?style=flat-square" alt="Size" />
  <img src="https://img.shields.io/badge/Root-Not%20Required-blueviolet?style=flat-square" alt="No Root" />
  <img src="https://img.shields.io/badge/License-MIT-green?style=flat-square" alt="License" />
</p>

---

## 📖 简介 (Introduction)

在测试 Android 手机的闪存（UFS / EMMC）持续写入性能、SLC 模拟缓存区大小（TurboWrite）以及出缓后的垃圾回收（GC / TLC 直写）速度时，传统的做法是执行：

```bash
dd if=/dev/zero of=/sdcard/test.tmp bs=1G status=progress conv=fsync
```

然而，在移动设备上使用上述 `dd` 命令存在致命缺陷：
1. **CPU 无谓开销大**：每写 1GB 需从 `/dev/zero` 内核驱动读取数十万次，产生巨量系统调用切换与 CPU 发热。
2. **极易触发杀后台（LMKD 闪退）**：`bs=1G` 会一次性申请 1GB 物理常驻内存，在手机存储逼近写满时，Android 底层的 Low Memory Killer 极易将 Termux 进程闪退杀死。
3. **统计粗糙/易失效**：难以分离 Page Cache 写入耗时与物理闪存 `fsync` 刷盘耗时，且 Android 自带的 toybox `dd` 与 GNU `dd` 参数格式不兼容。

**FastFillBench** 是为此场景打造的纯原生 C 语言方案，体积仅 **75KB**，**无需 Root 权限**，直接以极低开销跑满手机 UFS 硬件物理带宽，实时监控写入量、物理刷盘延时、剩余空间并持久化为 CSV。

---

## ✨ 核心特性 (Features)

* 🚀 **零设备读取（Zero-Syscall Zeroing）**：在用户态预分配对齐的零内存缓冲区（Zero-Buffer），**0 次读取 `/dev/zero`**，消除多余系统调用，让 CPU 专注于打满 I/O。
* 🛡️ **防杀后台设计（Anti-LMK Safety）**：流式采用 32MB 内存缓冲区循环写入，无论写入 100GB 还是 1TB，**常驻内存仅 32MB**，绝不触发安卓 OOM 杀进程。
* ⏱️ **高精度分离采样**：纳秒级高精度单调时钟（`CLOCK_MONOTONIC`），分别记录**内存缓存写入耗时（Write Time）**与**物理闪存刷盘耗时（Sync Time）**，精准捕捉真实的“出缓”拐点。
* 📊 **实时文件系统监控与 CSV 记录**：每写满指定块（默认 1GB）调用一次 `statvfs` 查询硬件真实剩余空间，并立即 `fflush` 落盘到 CSV，断电/拔电数据不丢失。
* 🛑 **安全优雅退出**：
  * **写满自停**：精准捕获底层 `ENOSPC` 磁盘已满信号，保留最后实际写入字节后安全停止。
  * **随时中断**：按下 <kbd>Ctrl+C</kbd> 毫秒级响应，补齐最后这一块数据并输出总体测试总结。

---

## 🖥️ 运行效果 (Demo)

终端实时彩色交互输出：

```text
===============================================================
 Fast Storage Fill & Benchmark Started
 Target file : /sdcard/test.tmp
 CSV log     : /sdcard/fill_bench.csv
 Block size  : 1024.00 MB (fsync per block)
 Chunk size  : 32.00 MB (RAM buffer)
 Sync method : fsync()
 Initial Free: 624.52 GB / Total: 954.21 GB
 Press Ctrl+C at any time to safely stop and save results.
===============================================================

[#0230] Written:  230.00 GB | Free:  395.55 GB | Speed:   500.4 MB/s (Sync: 1.43s) | Avg:   843.9 MB/s
[#0231] Written:  231.00 GB | Free:  394.55 GB | Speed:   448.5 MB/s (Sync: 1.67s) | Avg:   840.7 MB/s
[#0232] Written:  232.00 GB | Free:  393.55 GB | Speed:   409.1 MB/s (Sync: 1.83s) | Avg:   836.9 MB/s
[#0233] Written:  233.00 GB | Free:  392.55 GB | Speed:   313.4 MB/s (Sync: 2.65s) | Avg:   831.0 MB/s
[#0234] Written:  234.00 GB | Free:  391.55 GB | Speed:   322.4 MB/s (Sync: 2.55s) | Avg:   825.4 MB/s
...
```

---

## 🚀 快速上手 (Quick Start)

### 方式 A：在 Termux 中源码一键编译（推荐）

1. **授予存储权限并安装编译器**（仅需一次）：
   ```bash
   termux-setup-storage
   pkg update && pkg install -y clang make
   ```
2. **下载并编译**：
   ```bash
   git clone https://github.com/your-username/fast-fill-bench.git
   cd fast-fill-bench
   make
   ```
3. **开始测试**：
   ```bash
   ./fast_fill_bench
   ```

---

### 方式 B：免安装编译器直接运行（纯静态二进制）

如果你不想在手机上安装几百兆的编译环境，可以直接下载 [Releases](https://github.com/your-username/fast-fill-bench/releases) 中预编译好的静态 ARM64 二进制文件，或者通过以下命令在 Termux 中还原：

```bash
termux-setup-storage
# 下载并赋予执行权限
curl -sL https://github.com/your-username/fast-fill-bench/releases/latest/download/fast_fill_bench -o fast_fill_bench
chmod +x fast_fill_bench
./fast_fill_bench
```

---

## ⚙️ 命令行参数 (CLI Options)

```text
Usage: fast_fill_bench [options]

Options:
  -o <path>       测试目标文件路径 (默认: /sdcard/test.tmp)
  -l <path>       CSV 日志输出路径 (默认: /sdcard/fill_bench.csv)
  -b <size>       刷盘间隔块大小 (默认: 1G，支持 512M, 1G, 2G 等)
  -c <size>       单次写入缓冲区大小 (默认: 32M，支持 4M, 16M, 32M 等)
  -s              使用 fdatasync() 替代 fsync() (减少元数据同步开销)
  -d              尝试启用 O_DIRECT 绕过页面缓存 (需底层文件系统支持)
  -h, --help      显示帮助信息
```

#### 常见使用场景：

* **写入公共 Download 目录（解决部分 Android 11+ 根目录写保护）**：
  ```bash
  ./fast_fill_bench -o /sdcard/Download/test.tmp -l /sdcard/Download/fill_bench.csv
  ```
* **提高采样密度（每 256MB 统计一次）**：
  ```bash
  ./fast_fill_bench -b 256M
  ```

---

## 📈 CSV 结构与数据可视化

测试过程中生成的 CSV 文件包含以下字段：

| 字段 | 含义 | 说明 |
| :--- | :--- | :--- |
| `timestamp` | 本轮完成时间 | 格式: `YYYY-MM-DD HH:MM:SS` |
| `iteration` | 采样序号 | 1, 2, 3... |
| `block_bytes` | 本轮实际写入字节数 | 默认 1073741824 (1GB) |
| `total_written_gb` | 累计写入量 (GB) | 方便作为折线图横坐标 |
| `free_space_gb` | 磁盘当前剩余容量 (GB) | `statvfs` 实时物理查询 |
| `disk_total_gb` | 磁盘总容量 (GB) | 存储介质总容量 |
| `write_time_sec` | 写入内存缓存耗时 (秒) | 系统调用耗时 |
| `sync_time_sec` | 物理刷入闪存耗时 (秒) | `fsync()` 硬件等待耗时 |
| `block_speed_mbs` | **瞬时真实写入速度 (MB/s)** | 包含硬件刷盘的真实速度 |
| `avg_speed_mbs` | **全局平均写入速度 (MB/s)** | 从运行开始至今的综合平均 |
| `status` | 状态 | `OK`, `DISK_FULL`, `INTERRUPTED` |

### 一键生成全盘性能衰减折线图

仓库内自带 Python 绘图工具：

```bash
pip install pandas matplotlib
python3 plot_benchmark.py fill_bench.csv
```
执行后将自动输出类似专业测评的高清全盘写入折线图 `fill_bench_curve.png`。

---

## 🧹 清理与释放空间

测试结束后，测试生成的临时大文件占据了大量空间，运行以下命令即可恢复：

```bash
rm /sdcard/test.tmp
# 或
rm /sdcard/Download/test.tmp
```

---

## 📄 开源许可证 (License)

本项目采用 [MIT License](LICENSE) 开源协议。
