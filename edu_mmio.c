#include <linux/io.h>
#include <linux/types.h>
 
#include "edu_mmio.h"

void edu_mmio_write(void __iomem *base, uint32_t val)
{
    iowrite32(val, base + REG_FACTORIAL);
}

uint32_t edu_mmio_read_result(void __iomem *base)
{
    return ioread32(base + REG_FACTORIAL);
}

void edu_mmio_ack_irq(void __iomem *base)
{
    uint32_t status = ioread32(base + REG_IRQ_STATUS);
    iowrite32(status, base + REG_IRQ_ACK);
}
