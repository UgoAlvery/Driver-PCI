#include <linux/delay.h>

static void __iomem *mmio_base;

void edu_mmio_set_base(void __iomem *base)
{
    mmio_base = base;
}

void edu_mmio_write(uint32_t val)
{
    iowrite32(val, mmio_base + REG_FACTORIAL);
}

uint32_t edu_mmio_read_result(void)
{
    while (ioread32(mmio_base + REG_STATUS) & 0x1)
        cpu_relax();

    return ioread32(mmio_base + REG_FACTORIAL);
}