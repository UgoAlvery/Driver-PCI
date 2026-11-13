#ifndef EDU_CHAR_H
#define EDU_CHAR_H

#include <linux/types.h>
#include "edu_dev.h"
#include "edu_common.h"

int edu_char_global_init(void);
void edu_char_global_exit(void);

int edu_char_init(struct edu_dev *edu);
void edu_char_cleanup(struct edu_dev *edu);

/* Pure input validators — testable without a device (used by KUnit). */

static inline bool edu_factorial_input_valid(long n)
{
	/* 12! = 479001600 fits in uint32_t; 13! = 6227020800 overflows */
	return n >= 0 && n <= 12;
}

static inline bool edu_dma_len_valid(u32 len)
{
	return len >= 1 && len <= EDU_DMA_BUF_SIZE;
}

#endif