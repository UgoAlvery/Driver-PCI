#include <linux/io.h>
#include <linux/types.h>

#include "edu_mmio.h"

/* 
 * On écrit la valeur dans le registre du device pour lancer le calcul de factorielle.
 */
void edu_mmio_write(void __iomem *base, uint32_t val)
{
	iowrite32(val, base + REG_FACTORIAL);
}

/*
 * On lit le résultat du calcul une fois que l'interruption nous a signalé que c'est fini.
 */
uint32_t edu_mmio_read_result(void __iomem *base)
{
	return ioread32(base + REG_FACTORIAL);
}

/*
 * On active la génération d'IRQ à la fin du calcul de factorielle.
 * Sans ce bit, le device calcule silencieusement mais ne lève jamais
 * d'interruption : quiconque attend sur la waitqueue reste bloqué
 * indéfiniment.
 */
void edu_mmio_enable_irq(void __iomem *base)
{
	iowrite32(STATUS_IRQFACT, base + REG_STATUS);
}

/*
 * On lit le registre de statut des interruptions pour savoir laquelle
 * (ou lesquelles) ont été levées, puis on acquitte en réécrivant la même
 * valeur dans le registre d'acquittement. C'est obligatoire depuis l'ISR :
 * une interruption non acquittée laisse la ligne INTx active.
 */
uint32_t edu_mmio_ack_irq(void __iomem *base)
{
	uint32_t status = ioread32(base + REG_INTR_STATUS);

	iowrite32(status, base + REG_INTR_ACK);
	return status;
}

/*
 * On programme les 4 registres DMA et on lance le transfert.
 *
 * REG_DMA_SRC/DST acceptent des accès 32 ou 64 bits (cf. spec EDU), mais on
 * écrit ici en 32 bits volontairement : le driver force dma_set_mask() à 28
 * bits (256 Mio, cf. edu_pci.c), donc toute adresse DMA que le kernel nous
 * remet tient forcément sur 32 bits. On évite ainsi readq/writeq, dont le
 * support MMIO 64 bits n'est pas garanti sur toutes les architectures.
 */
void edu_mmio_dma_start(void __iomem *base, dma_addr_t src, dma_addr_t dst,
			uint32_t cnt, bool to_ram)
{
	uint32_t cmd = DMA_CMD_START | DMA_CMD_IRQ;

	if (to_ram)
		cmd |= DMA_CMD_DIR_TO_RAM;

	iowrite32((uint32_t)src, base + REG_DMA_SRC);
	iowrite32((uint32_t)dst, base + REG_DMA_DST);
	iowrite32(cnt, base + REG_DMA_CNT);
	iowrite32(cmd, base + REG_DMA_CMD);
}