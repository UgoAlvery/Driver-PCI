#ifndef EDU_MMIO_H
#define EDU_MMIO_H

#include <linux/types.h>
#include <linux/io.h>

#define REG_FACTORIAL 0x08
#define REG_STATUS 0x20

void edu_mmio_set_base(void __iomem *base);
void edu_mmio_write(uint32_t val);
uint32_t edu_mmio_read_result(void);

#endif