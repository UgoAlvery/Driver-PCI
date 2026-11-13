CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2

all: dma_roundtrip_test poll_test mmap_test

dma_roundtrip_test: dma_roundtrip_test.c ../edu_ioctl.h
	$(CC) $(CFLAGS) -o $@ dma_roundtrip_test.c

poll_test: poll_test.c
	$(CC) $(CFLAGS) -o $@ poll_test.c

mmap_test: mmap_test.c ../edu_ioctl.h
	$(CC) $(CFLAGS) -o $@ mmap_test.c

clean:
	rm -f dma_roundtrip_test poll_test mmap_test
