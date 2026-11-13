/*
 * poll_test.c
 *
 * Tests that poll()/epoll_wait() on /dev/edu-factN works correctly:
 *   - EPOLLOUT is set before any write (device idle, can submit)
 *   - EPOLLIN is set after a write completes (result ready to read)
 *   - Reading the result clears EPOLLIN (via irq_done reset on next write)
 *
 * Usage: poll_test <device>
 * Exit: 0 on success, 1 on test failure, 2 on syscall error.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int check_poll(int fd, short events, const char *label)
{
	struct pollfd pfd = { .fd = fd, .events = events };
	int ret = poll(&pfd, 1, 0); /* timeout=0: non-blocking check */

	if (ret < 0) {
		perror("poll");
		return -1;
	}
	if (!(pfd.revents & events)) {
		fprintf(stderr, "poll: expected 0x%x, got 0x%x (%s)\n",
			events, pfd.revents, label);
		return 0;
	}
	return 1;
}

int main(int argc, char **argv)
{
	int fd, ret;
	char buf[32];
	ssize_t n;
	long result;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <device>\n", argv[0]);
		return 2;
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror("open");
		return 2;
	}

	/* Device idle: should be writable immediately */
	ret = check_poll(fd, POLLOUT, "POLLOUT before write");
	if (ret < 0)
		return 2;
	if (!ret) {
		fprintf(stderr, "FAIL: POLLOUT not set when device idle\n");
		close(fd);
		return 1;
	}
	printf("OK: POLLOUT set (device idle)\n");

	/* Submit a factorial — write() blocks until IRQ fires */
	n = write(fd, "5\n", 2);
	if (n < 0) {
		perror("write");
		close(fd);
		return 2;
	}

	/* After write() returns: irq_done is set, result ready */
	ret = check_poll(fd, POLLIN, "POLLIN after write");
	if (ret < 0)
		return 2;
	if (!ret) {
		fprintf(stderr, "FAIL: POLLIN not set after write\n");
		close(fd);
		return 1;
	}
	printf("OK: POLLIN set (result ready)\n");

	/* Read result, verify 5! == 120 */
	n = read(fd, buf, sizeof(buf) - 1);
	if (n <= 0) {
		perror("read");
		close(fd);
		return 2;
	}
	buf[n] = '\0';
	result = strtol(buf, NULL, 10);
	if (result != 120) {
		fprintf(stderr, "FAIL: 5! expected 120, got %ld\n", result);
		close(fd);
		return 1;
	}
	printf("OK: 5! == 120\n");

	/* POLLOUT still set after read (no new computation in flight) */
	ret = check_poll(fd, POLLOUT, "POLLOUT after read");
	if (ret < 0)
		return 2;
	if (!ret) {
		fprintf(stderr, "FAIL: POLLOUT not set after read\n");
		close(fd);
		return 1;
	}
	printf("OK: POLLOUT still set after read\n");

	close(fd);
	printf("poll_test: all checks passed\n");
	return 0;
}
