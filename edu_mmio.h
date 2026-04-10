#ifndef EDU_MMIO_H
#define EDU_MMIO_H

#include <linux/types.h>
#include <linux/io.h>

/* EDU device register offsets (see QEMU edu device spec) */
#define REG_FACTORIAL 0x08
#define REG_STATUS 0x20

/* STATUS register bits */
#define STATUS_BUSY 0x01 /* factorial computation in progress */
#define STATUS_IRQ_REQ 0x80 /* request interrupt on factorial completion */

void edu_mmio_write(void __iomem *base, uint32_t val);
uint32_t edu_mmio_read_result(void __iomem *base);

#endif