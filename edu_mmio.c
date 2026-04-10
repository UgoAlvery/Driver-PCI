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