#ifndef EDU_MMIO_H
#define EDU_MMIO_H

#include <linux/types.h>
#include <linux/io.h>

#define REG_FACTORIAL 0x08
#define REG_STATUS 0x20
#define REG_IRQ_STATUS 0x24
#define REG_IRQ_ACK 0x64
#define REG_IRQ_RAISE 0x60

#define STATUS_BUSY 0x01

#define IRQ_FACT_DONE 0x01

void     edu_mmio_write(void __iomem *base, uint32_t val);
uint32_t edu_mmio_read_result(void __iomem *base);
void     edu_mmio_ack_irq(void __iomem *base);

#endif