CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2

all: dma_roundtrip_test

dma_roundtrip_test: dma_roundtrip_test.c ../edu_ioctl.h
	$(CC) $(CFLAGS) -o $@ dma_roundtrip_test.c

clean:
	rm -f dma_roundtrip_test
