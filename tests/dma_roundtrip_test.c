/*
 * dma_roundtrip_test.c
 *
 * Envoie un motif de données reconnaissable au driver via
 * EDU_IOC_DMA_ROUNDTRIP et vérifie qu'il revient identique après
 * l'aller-retour RAM->EDU->RAM. Un motif non trivial (pas des zéros)
 * est essentiel : un buffer resté inchangé masquerait un transfert qui
 * n'a en réalité rien fait.
 *
 * Usage: dma_roundtrip_test <device>   (ex: /dev/edu-fact0)
 * Exit: 0 si les données correspondent, 1 en cas de corruption,
 *       2 en cas d'erreur d'usage ou d'appel système.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../edu_ioctl.h"

#define TEST_LEN 256

int main(int argc, char **argv)
{
	struct edu_dma_req req;
	unsigned char expected[TEST_LEN];
	int fd;
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

	req.len = TEST_LEN;
	for (i = 0; i < req.len; i++)
		req.data[i] = (unsigned char)(i * 7 + 3);
	memcpy(expected, req.data, req.len);

	if (ioctl(fd, EDU_IOC_DMA_ROUNDTRIP, &req) < 0) {
		perror("ioctl(EDU_IOC_DMA_ROUNDTRIP)");
		close(fd);
		return 2;
	}

	close(fd);

	if (memcmp(expected, req.data, req.len) != 0) {
		fprintf(stderr,
			"DMA round-trip mismatch: data corrupted over %u bytes\n",
			req.len);
		return 1;
	}

	printf("DMA round-trip OK (%u bytes)\n", req.len);
	return 0;
}
