#ifndef EDU_IOCTL_H
#define EDU_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#include "edu_common.h"

/*
 * EDU_IOC_DMA_ROUNDTRIP : envoie "len" octets de "data" vers le buffer
 * scratch interne du device via DMA (RAM->EDU), puis les relit
 * immédiatement dans le sens inverse (EDU->RAM) dans le même buffer.
 *
 * Le driver ne fait qu'exécuter l'aller-retour : c'est à l'appelant de
 * comparer le contenu de "data" avant/après l'appel pour vérifier que le
 * device a restitué exactement ce qui a été envoyé. C'est un choix de
 * design délibéré : le driver ne devine pas ce que l'appelant considère
 * comme une réussite, il expose juste le mécanisme.
 */
struct edu_dma_req {
	__u32 len; /* octets à transférer, 1 <= len <= EDU_DMA_BUF_SIZE */
	__u8 data[EDU_DMA_BUF_SIZE];
};

#define EDU_IOCTL_MAGIC 'e'
#define EDU_IOC_DMA_ROUNDTRIP _IOWR(EDU_IOCTL_MAGIC, 1, struct edu_dma_req)

#endif
