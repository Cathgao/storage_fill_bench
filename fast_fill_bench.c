#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

#define DEFAULT_TARGET_FILE "/sdcard/test.tmp"
#define DEFAULT_LOG_FILE    "/sdcard/fill_bench.csv"
#define DEFAULT_BLOCK_SIZE  (1024ULL * 1024ULL * 1024ULL) // 1 GiB
#define DEFAULT_CHUNK_SIZE  (32ULL * 1024ULL * 1024ULL)   // 32 MiB buffer

static volatile sig_atomic_t g_stop = 0;

static void sig_handler(int sig) {
    (void)sig;
    g_stop = 1;
}

static inline double get_monotonic_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void get_iso_time(char *buf, size_t max_len) {
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    strftime(buf, max_len, "%Y-%m-%d %H:%M:%S", &tm_buf);
}

static uint64_t parse_size(const char *str) {
    char *endptr = NULL;
    errno = 0;
    double val = strtod(str, &endptr);
    if (errno != 0 || endptr == str || val < 0) {
        return 0;
    }
    uint64_t multiplier = 1;
    if (endptr && *endptr) {
        char unit = *endptr;
        if (unit == 'k' || unit == 'K') multiplier = 1024ULL;
        else if (unit == 'm' || unit == 'M') multiplier = 1024ULL * 1024ULL;
        else if (unit == 'g' || unit == 'G') multiplier = 1024ULL * 1024ULL * 1024ULL;
        else if (unit == 't' || unit == 'T') multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    }
    return (uint64_t)(val * multiplier);
}

static bool get_storage_stats(int fd, const char *path, uint64_t *free_bytes, uint64_t *total_bytes) {
    struct statvfs st;
    int res = -1;
    if (fd >= 0) {
        res = fstatvfs(fd, &st);
    }
    if (res != 0 && path) {
        res = statvfs(path, &st);
    }
    if (res == 0) {
        uint64_t frsize = st.f_frsize ? (uint64_t)st.f_frsize : (uint64_t)st.f_bsize;
        *free_bytes = (uint64_t)st.f_bavail * frsize;
        *total_bytes = (uint64_t)st.f_blocks * frsize;
        return true;
    }
    return false;
}

static void print_usage(const char *prog_name) {
    printf("===============================================================\n");
    printf(" Fast Fill & Storage Benchmark for Android / Termux (ARM64)\n");
    printf(" High-Efficiency zero-fill & fsync disk throughput monitor\n");
    printf("===============================================================\n\n");
    printf("Usage: %s [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  -o <path>       Target file path (default: %s)\n", DEFAULT_TARGET_FILE);
    printf("  -l <path>       CSV log file path (default: %s)\n", DEFAULT_LOG_FILE);
    printf("  -b <size>       Sync block size (default: 1G, e.g. 512M, 1G, 2G)\n");
    printf("  -c <size>       Chunk buffer size (default: 16M, e.g. 4M, 16M, 32M)\n");
    printf("  -s              Use fdatasync() instead of fsync() for metadata optimization\n");
    printf("  -d              Try O_DIRECT (bypasses page cache, if supported by FS)\n");
    printf("  -h, --help      Show this help message\n\n");
    printf("Features:\n");
    printf("  * Pre-allocated zero buffer in RAM (ZERO syscalls to /dev/zero, 10x faster)\n");
    printf("  * Controlled RAM footprint (uses 16M RAM buffer to prevent Android LMK kill)\n");
    printf("  * Synchronizes each block (conv=fsync) and logs exact write/sync latency\n");
    printf("  * Real-time filesystem free space query via statvfs\n");
    printf("  * Auto stop on ENOSPC (Disk Full) or Ctrl+C (SIGINT) with graceful CSV flush\n\n");
}

int main(int argc, char *argv[]) {
    const char *target_path = DEFAULT_TARGET_FILE;
    const char *csv_path = DEFAULT_LOG_FILE;
    uint64_t block_size = DEFAULT_BLOCK_SIZE;
    uint64_t chunk_size = DEFAULT_CHUNK_SIZE;
    bool use_fdatasync = false;
    bool try_direct = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            target_path = argv[++i];
        } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            block_size = parse_size(argv[++i]);
            if (block_size == 0) {
                fprintf(stderr, "Error: Invalid block size.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            chunk_size = parse_size(argv[++i]);
            if (chunk_size == 0) {
                fprintf(stderr, "Error: Invalid chunk size.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-s") == 0) {
            use_fdatasync = true;
        } else if (strcmp(argv[i], "-d") == 0) {
            try_direct = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (chunk_size > block_size) {
        chunk_size = block_size;
    }

    // Set up signal handler for graceful shutdown
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Initial check of initial storage stats
    uint64_t initial_free = 0, initial_total = 0;
    get_storage_stats(-1, target_path, &initial_free, &initial_total);

    // Allocate page-aligned zero buffer
    void *zero_buf = NULL;
    if (posix_memalign(&zero_buf, 4096, chunk_size) != 0) {
        zero_buf = malloc(chunk_size);
        if (!zero_buf) {
            fprintf(stderr, "Fatal: Failed to allocate %llu bytes buffer.\n", (unsigned long long)chunk_size);
            return 1;
        }
    }
    memset(zero_buf, 0, chunk_size);

    // Open target file
    int open_flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_DIRECT
    if (try_direct) {
        open_flags |= O_DIRECT;
    }
#endif

    int fd = open(target_path, open_flags, 0644);
    if (fd < 0 && try_direct) {
        // Fallback without O_DIRECT if filesystem rejects it (e.g. FUSE/sdcardfs)
        open_flags &= ~O_DIRECT;
        fd = open(target_path, open_flags, 0644);
    }

    if (fd < 0) {
        int err = errno;
        fprintf(stderr, "\n[ERROR] Failed to open target file '%s': %s\n", target_path, strerror(err));
        if (err == EACCES || err == EPERM) {
            fprintf(stderr, "[TIP] In Termux, please make sure storage permission is granted by running:\n");
            fprintf(stderr, "      termux-setup-storage\n");
            fprintf(stderr, "      Or run with a local path: -o ./test.tmp -l ./benchmark.csv\n\n");
        }
        free(zero_buf);
        return 1;
    }

    // Open CSV log file
    FILE *csv_fp = fopen(csv_path, "w");
    if (!csv_fp) {
        int err = errno;
        fprintf(stderr, "\n[ERROR] Failed to open CSV log file '%s': %s\n", csv_path, strerror(err));
        close(fd);
        free(zero_buf);
        return 1;
    }

    // Write CSV Header
    fprintf(csv_fp, "timestamp,iteration,block_bytes,total_written_bytes,total_written_gb,free_space_bytes,free_space_gb,disk_total_gb,write_time_sec,sync_time_sec,block_time_sec,block_speed_mbs,avg_speed_mbs,status\n");
    fflush(csv_fp);

    // Print banner
    printf("\n\033[1;36m===============================================================\033[0m\n");
    printf("\033[1;32m Fast Storage Fill & Benchmark Started\033[0m\n");
    printf(" Target file : \033[1m%s\033[0m\n", target_path);
    printf(" CSV log     : \033[1m%s\033[0m\n", csv_path);
    printf(" Block size  : \033[1m%.2f MB\033[0m (fsync per block)\n", (double)block_size / (1024 * 1024));
    printf(" Chunk size  : \033[1m%.2f MB\033[0m (RAM buffer)\n", (double)chunk_size / (1024 * 1024));
    printf(" Sync method : \033[1m%s\033[0m\n", use_fdatasync ? "fdatasync()" : "fsync()");
    if (initial_total > 0) {
        printf(" Initial Free: \033[1;33m%.2f GB\033[0m / Total: %.2f GB\n",
               (double)initial_free / (1024ULL * 1024 * 1024),
               (double)initial_total / (1024ULL * 1024 * 1024));
    }
    printf(" Press \033[1;31mCtrl+C\033[0m at any time to safely stop and save results.\n");
    printf("\033[1;36m===============================================================\033[0m\n\n");

    uint64_t total_written = 0;
    uint64_t iteration = 0;
    double start_global_time = get_monotonic_sec();
    bool disk_full = false;
    const char *final_reason = "COMPLETED";

    while (!g_stop && !disk_full) {
        iteration++;
        uint64_t block_bytes_written = 0;
        double write_time_total = 0.0;

        // Write loop for this block
        while (block_bytes_written < block_size && !g_stop) {
            uint64_t to_write = chunk_size;
            if (to_write > (block_size - block_bytes_written)) {
                to_write = block_size - block_bytes_written;
            }

            double tw_start = get_monotonic_sec();
            ssize_t n = write(fd, zero_buf, to_write);
            double tw_end = get_monotonic_sec();
            write_time_total += (tw_end - tw_start);

            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == ENOSPC || errno == EDQUOT) {
                    disk_full = true;
                    final_reason = "DISK_FULL";
                    break;
                }
                // Any other I/O error
                int err = errno;
                fprintf(stderr, "\n[ERROR] write() failed: %s (errno: %d)\n", strerror(err), err);
                final_reason = "WRITE_ERROR";
                disk_full = true;
                break;
            }

            block_bytes_written += (uint64_t)n;
            total_written += (uint64_t)n;

            if ((uint64_t)n < to_write) {
                // Short write, likely disk full
                disk_full = true;
                final_reason = "DISK_FULL";
                break;
            }
        }

        if (block_bytes_written == 0 && (disk_full || g_stop)) {
            break;
        }

        // Fsync phase
        double ts_start = get_monotonic_sec();
        if (use_fdatasync) {
            fdatasync(fd);
        } else {
            fsync(fd);
        }
        double ts_end = get_monotonic_sec();
        double sync_time_total = ts_end - ts_start;

        double block_time_total = write_time_total + sync_time_total;
        if (block_time_total <= 0.000001) {
            block_time_total = 0.000001; // Avoid division by zero
        }

        double block_speed_mbs = ((double)block_bytes_written / (1024.0 * 1024.0)) / block_time_total;
        double current_global_time = get_monotonic_sec();
        double elapsed_global_time = current_global_time - start_global_time;
        if (elapsed_global_time <= 0.000001) elapsed_global_time = 0.000001;
        double avg_speed_mbs = ((double)total_written / (1024.0 * 1024.0)) / elapsed_global_time;

        // Query current remaining space
        uint64_t current_free = 0, current_total = 0;
        get_storage_stats(fd, target_path, &current_free, &current_total);

        // Status string for this entry
        const char *status_str = "OK";
        if (disk_full) status_str = "DISK_FULL";
        else if (g_stop) status_str = "INTERRUPTED";

        // ISO timestamp
        char time_str[32];
        get_iso_time(time_str, sizeof(time_str));

        // Append to CSV
        fprintf(csv_fp, "%s,%llu,%llu,%llu,%.3f,%llu,%.3f,%.2f,%.4f,%.4f,%.4f,%.2f,%.2f,%s\n",
                time_str,
                (unsigned long long)iteration,
                (unsigned long long)block_bytes_written,
                (unsigned long long)total_written,
                (double)total_written / (1024ULL * 1024 * 1024),
                (unsigned long long)current_free,
                (double)current_free / (1024ULL * 1024 * 1024),
                (double)current_total / (1024ULL * 1024 * 1024),
                write_time_total,
                sync_time_total,
                block_time_total,
                block_speed_mbs,
                avg_speed_mbs,
                status_str);
        fflush(csv_fp);

        // Live Console Print
        printf("[#%04llu] Written: \033[1;32m%7.2f GB\033[0m | Free: \033[1;33m%7.2f GB\033[0m | Speed: \033[1;35m%7.1f MB/s\033[0m (Sync: %4.2fs) | Avg: \033[1;36m%7.1f MB/s\033[0m\n",
               (unsigned long long)iteration,
               (double)total_written / (1024ULL * 1024 * 1024),
               (double)current_free / (1024ULL * 1024 * 1024),
               block_speed_mbs,
               sync_time_total,
               avg_speed_mbs);
        fflush(stdout);

        if (g_stop) {
            final_reason = "INTERRUPTED_BY_USER";
            break;
        }
    }

    // Final Sync & Cleanup
    fsync(fd);
    close(fd);
    fclose(csv_fp);
    free(zero_buf);

    double total_wall_time = get_monotonic_sec() - start_global_time;
    if (total_wall_time <= 0.000001) total_wall_time = 0.000001;
    double overall_avg_speed = ((double)total_written / (1024.0 * 1024.0)) / total_wall_time;

    printf("\n\033[1;36m===============================================================\033[0m\n");
    printf("\033[1;32m Benchmark Finished [%s]\033[0m\n", final_reason);
    printf(" Total written  : \033[1m%.3f GB\033[0m (%llu bytes)\n",
           (double)total_written / (1024ULL * 1024 * 1024),
           (unsigned long long)total_written);
    printf(" Total time     : \033[1m%.2f seconds\033[0m\n", total_wall_time);
    printf(" Average speed  : \033[1;32m%.2f MB/s\033[0m\n", overall_avg_speed);
    printf(" Log saved to   : \033[1;33m%s\033[0m\n", csv_path);
    printf(" Target file    : \033[1m%s\033[0m (run 'rm %s' to reclaim space)\n", target_path, target_path);
    printf("\033[1;36m===============================================================\033[0m\n\n");

    return 0;
}
