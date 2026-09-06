#ifndef EDU_MMIO_H
#define EDU_MMIO_H

#include <linux/types.h>
#include <linux/bitops.h>

#include "edu_common.h"

#define REG_FACTORIAL 0x08
#define REG_STATUS 0x20
#define REG_INTR_STATUS 0x24
#define REG_INTR_RAISE 0x60
#define REG_INTR_ACK 0x64
#define REG_DMA_SRC 0x80
#define REG_DMA_DST 0x88
#define REG_DMA_CNT 0x90
#define REG_DMA_CMD 0x98

/* Bits of REG_STATUS (0x20) */
#define STATUS_COMPUTING BIT(0)
#define STATUS_IRQFACT BIT(7)

/* Bits of REG_DMA_CMD (0x98) */
#define DMA_CMD_START BIT(0)
#define DMA_CMD_DIR_TO_RAM BIT(1) /* 0: RAM->EDU, 1: EDU->RAM */
#define DMA_CMD_IRQ BIT(2) /* raise DMA_IRQ once the transfer completes */

/* Value the device ORs into REG_INTR_STATUS when a factorial computation
 * completes and STATUS_IRQFACT is set (see docs/specs/edu.rst in the QEMU
 * source tree, hw/misc/edu.c: FACT_IRQ).
 */
#define FACT_IRQ BIT(0)

/* Same, but for DMA_CMD_IRQ-flagged DMA transfers (hw/misc/edu.c: DMA_IRQ). */
#define DMA_IRQ BIT(8)

/* Offset, inside the device's own address space, of its 4 KiB internal
 * scratch buffer used as the "other end" of a DMA transfer. Not a register:
 * it is the value to program into REG_DMA_SRC/REG_DMA_DST to target that
 * buffer (hw/misc/edu.c: DMA_START / DMA_SIZE). See edu_common.h for the
 * actual EDU_DMA_BUF_OFFSET/EDU_DMA_BUF_SIZE values, shared with userspace.
 */

void edu_mmio_write(void __iomem *base, uint32_t val);
uint32_t edu_mmio_read_result(void __iomem *base);

/*
 * Enables interrupt generation on factorial completion (sets STATUS_IRQFACT
 * in REG_STATUS). Without this, the device silently computes the factorial
 * but never raises an IRQ, and any driver blocked in wait_event_interruptible()
 * on completion hangs forever.
 */
void edu_mmio_enable_irq(void __iomem *base);

/*
 * Reads REG_INTR_STATUS to find out which interrupt(s) fired, then
 * acknowledges them by writing the same value back to REG_INTR_ACK. This
 * has to happen in the ISR: on real (and emulated) EDU hardware, an
 * unacknowledged interrupt keeps the INTx line asserted.
 * Returns the value that was read (and acknowledged), so the caller can
 * test which condition(s) caused this interrupt.
 */
uint32_t edu_mmio_ack_irq(void __iomem *base);

/*
 * Programme les 4 registres DMA (source, destination, taille, commande) et
 * lance le transfert. N'attend pas la fin : c'est à l'appelant de patienter
 * sur l'IRQ (DMA_CMD_IRQ est toujours positionné par cette fonction).
 * to_ram indique le sens : true = EDU->RAM, false = RAM->EDU.
 */
void edu_mmio_dma_start(void __iomem *base, dma_addr_t src, dma_addr_t dst,
			uint32_t cnt, bool to_ram);

#endif
