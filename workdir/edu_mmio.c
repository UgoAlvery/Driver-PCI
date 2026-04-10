#include <linux/io.h>
#include <linux/types.h>

#include "edu_mmio.h"

/**
 * edu_mmio_write() - trigger a factorial computation
 * @base: ioremapped BAR0 base address
 * @val:  value to compute the factorial of
 */
void edu_mmio_write(void __iomem *base, uint32_t val)
{
	iowrite32(val, base + REG_FACTORIAL);
}

/**
 * edu_mmio_read_result() - read the factorial result register
 * @base: ioremapped BAR0 base address
 *
 * Should only be called once the device signals completion via IRQ.
 */
uint32_t edu_mmio_read_result(void __iomem *base)
{
	return ioread32(base + REG_FACTORIAL);
}