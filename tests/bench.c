/*
 * bench.c — DMA throughput and IRQ latency benchmark for the edu driver.
 *
 * Measures two metrics:
 *   1. DMA throughput: bytes/s for EDU_IOC_DMA_ROUNDTRIP at max size (4096 B)
 *   2. IRQ round-trip latency: wall-clock time from write() to completion
 *      for a factorial computation (8! — fast, deterministic).
 *
 * Uses clock_gettime(CLOCK_MONOTONIC), 60 repetitions per metric.
 * Reports: min, median, p95, max (latency in µs; throughput in MiB/s).
 *
 * IMPORTANT: values measured under QEMU/KVM are relative — they reflect
 * emulation overhead, not real PCI hardware. Useful for comparing
 * driver versions (INTx vs MSI, locking changes) on the same host,
 * not for predicting real-hardware performance.
 *
 * Usage: bench <device>   (e.g. /dev/edu-fact0)
 * Exit: 0 on success, 1 on failure.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "../edu_ioctl.h"

#define REPS 60

static long long ns_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int cmp_ll(const void *a, const void *b)
{
	long long x = *(const long long *)a;
	long long y = *(const long long *)b;
	return (x > y) - (x < y);
}

static void print_stats(const char *label, long long *samples, int n,
			int as_throughput, int buf_bytes)
{
	long long sorted[REPS];
	memcpy(sorted, samples, n * sizeof(long long));
	qsort(sorted, n, sizeof(long long), cmp_ll);

	long long min  = sorted[0];
	long long med  = sorted[n / 2];
	long long p95  = sorted[(int)(n * 0.95)];
	long long max  = sorted[n - 1];

	if (as_throughput) {
		/* Convert ns/op → KB/s (EDU DMA is QEMU-emulated, not fast) */
		double med_kbs  = (double)buf_bytes / (med  / 1e9) / 1024;
		double p95_kbs  = (double)buf_bytes / (p95  / 1e9) / 1024;
		double min_kbs  = (double)buf_bytes / (min  / 1e9) / 1024;
		double max_kbs  = (double)buf_bytes / (max  / 1e9) / 1024;
		printf("%-30s  min=%.0f  median=%.0f  p95=%.0f  max=%.0f  KB/s\n",
		       label, min_kbs, med_kbs, p95_kbs, max_kbs);
	} else {
		/* nanoseconds → microseconds */
		printf("%-30s  min=%lld  median=%lld  p95=%lld  max=%lld  µs\n",
		       label, min / 1000, med / 1000, p95 / 1000, max / 1000);
	}
}

int main(int argc, char **argv)
{
	int fd;
	long long lat[REPS], dma_lat[REPS];
	char buf[32];
	int i;
	long long t0;
	struct edu_dma_req req;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <device>\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	/* --- IRQ latency: write(8) + read() round-trip --- */
	for (i = 0; i < REPS; i++) {
		t0 = ns_now();
		if (write(fd, "8\n", 2) < 0) { perror("write"); return 1; }
		if (read(fd, buf, sizeof(buf)) < 0) { perror("read"); return 1; }
		lat[i] = ns_now() - t0;
		/* Reset ppos for next read */
		lseek(fd, 0, SEEK_SET);
	}
	print_stats("IRQ latency (write+read 8!)", lat, REPS, 0, 0);

	/* --- DMA throughput: full 4096-byte round-trip --- */
	req.len = EDU_DMA_BUF_SIZE;
	for (i = 0; i < (int)req.len; i++)
		req.data[i] = (uint8_t)(i ^ 0x5A);

	for (i = 0; i < REPS; i++) {
		t0 = ns_now();
		if (ioctl(fd, EDU_IOC_DMA_ROUNDTRIP, &req) < 0) {
			perror("ioctl");
			return 1;
		}
		dma_lat[i] = ns_now() - t0;
	}
	print_stats("DMA throughput (4096 B r-trip)", dma_lat, REPS, 1,
		    EDU_DMA_BUF_SIZE);
	print_stats("DMA latency    (4096 B r-trip)", dma_lat, REPS, 0, 0);

	close(fd);
	return 0;
}
