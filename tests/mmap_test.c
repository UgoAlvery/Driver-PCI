/*
 * mmap_test.c
 *
 * Tests mmap() of the DMA buffer exposed by /dev/edu-factN:
 *  1. Map EDU_DMA_BUF_SIZE bytes at offset 0.
 *  2. Write a pattern into the mapped region.
 *  3. Trigger a DMA round-trip via ioctl (the ioctl uses the same buffer).
 *  4. The ioctl copies from the kernel dma_buf → EDU → back; since we
 *     wrote into the mmap'd view of that same buffer, the data should
 *     survive the round-trip unchanged (ioctl copies *from* the buffer
 *     for the outbound leg).
 *
 *     Actually the ioctl does copy_from_user(dma_buf, req.data) first,
 *     so req.data drives the round-trip, not what we wrote via mmap.
 *     What this test really checks:
 *       - mmap() succeeds and maps a readable/writable region.
 *       - After an ioctl round-trip, the kernel-side dma_buf contains
 *         the ioctl data; the mmap'd view reflects that.
 *       - munmap() does not crash the kernel (module ref is released).
 *
 * Usage: mmap_test <device>
 * Exit: 0 on success, 1 on test failure, 2 on syscall error.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../edu_ioctl.h"

int main(int argc, char **argv)
{
	int fd;
	void *map;
	struct edu_dma_req req;
	unsigned int i;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <device>\n", argv[0]);
		return 2;
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror("open");
		return 2;
	}

	/* Map the DMA buffer */
	map = mmap(NULL, EDU_DMA_BUF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		   fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		close(fd);
		return 2;
	}
	printf("OK: mmap succeeded (addr=%p, size=%d)\n", map,
	       EDU_DMA_BUF_SIZE);

	/* Write a distinctive pattern into the mapped region */
	for (i = 0; i < EDU_DMA_BUF_SIZE; i++)
		((uint8_t *)map)[i] = (uint8_t)(i ^ 0xA5);

	/* Do a DMA round-trip via ioctl with known data */
	req.len = 64;
	for (i = 0; i < req.len; i++)
		req.data[i] = (uint8_t)(i + 1);

	if (ioctl(fd, EDU_IOC_DMA_ROUNDTRIP, &req) < 0) {
		perror("ioctl(EDU_IOC_DMA_ROUNDTRIP)");
		munmap(map, EDU_DMA_BUF_SIZE);
		close(fd);
		return 2;
	}

	/* Verify ioctl data integrity */
	for (i = 0; i < req.len; i++) {
		if (req.data[i] != (uint8_t)(i + 1)) {
			fprintf(stderr,
				"FAIL: ioctl data mismatch at byte %u\n", i);
			munmap(map, EDU_DMA_BUF_SIZE);
			close(fd);
			return 1;
		}
	}
	printf("OK: ioctl DMA round-trip data integrity with mmap active\n");

	/*
	 * After the ioctl, the first 64 bytes of dma_buf hold the ioctl data.
	 * The mmap'd view reflects the same physical memory.
	 */
	for (i = 0; i < req.len; i++) {
		if (((uint8_t *)map)[i] != (uint8_t)(i + 1)) {
			fprintf(stderr,
				"FAIL: mmap view mismatch after ioctl at byte %u\n",
				i);
			munmap(map, EDU_DMA_BUF_SIZE);
			close(fd);
			return 1;
		}
	}
	printf("OK: mmap view consistent with ioctl data after round-trip\n");

	/* munmap should not crash — releases module reference */
	if (munmap(map, EDU_DMA_BUF_SIZE) < 0) {
		perror("munmap");
		close(fd);
		return 2;
	}
	printf("OK: munmap clean (module reference released)\n");

	close(fd);
	printf("mmap_test: all checks passed\n");
	return 0;
}
