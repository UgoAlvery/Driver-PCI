#ifndef EDU_COMMON_H
#define EDU_COMMON_H

/*
 * Constantes partagées entre le code noyau (edu_mmio.h) et le code
 * espace utilisateur (edu_ioctl.h, tests/dma_roundtrip_test.c).
 *
 * Ce header ne doit inclure AUCUN header kernel-only (comme
 * <linux/bitops.h>) : c'est précisément ce qui permet à edu_ioctl.h
 * d'être inclus tel quel depuis un programme userspace.
 */

/* Offset, dans l'espace d'adressage du device, de son buffer scratch
 * interne de 4 KiB utilisé comme "autre bout" d'un transfert DMA.
 */
#define EDU_DMA_BUF_OFFSET 0x40000
#define EDU_DMA_BUF_SIZE 4096

#endif
