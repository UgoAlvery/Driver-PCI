// SPDX-License-Identifier: GPL-2.0
/*
 * edu_kunit.c — KUnit tests for edu driver pure logic.
 *
 * Tests only compile-time constants and input validators that run without
 * a PCI device. Load edu_kunit.ko on a kernel built with CONFIG_KUNIT=y;
 * results appear in dmesg (or via kunit_tool).
 *
 * Note: the debug kernel in results/2026-11-13-kasan-lockdep/ does not
 * have CONFIG_KUNIT=y. Rebuild the kernel with that option to run these.
 */
#include <kunit/test.h>

#include "edu_char.h"
#include "edu_ioctl.h"

/* ---- factorial input validator ---- */

static void test_factorial_valid_range(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, edu_factorial_input_valid(0));  /* 0! = 1 */
	KUNIT_EXPECT_TRUE(test, edu_factorial_input_valid(1));
	KUNIT_EXPECT_TRUE(test, edu_factorial_input_valid(12)); /* max: fits u32 */
}

static void test_factorial_overflow_guard(struct kunit *test)
{
	/* 13! = 6227020800 > UINT32_MAX */
	KUNIT_EXPECT_FALSE(test, edu_factorial_input_valid(13));
	KUNIT_EXPECT_FALSE(test, edu_factorial_input_valid(100));
}

static void test_factorial_negative_rejected(struct kunit *test)
{
	KUNIT_EXPECT_FALSE(test, edu_factorial_input_valid(-1));
	KUNIT_EXPECT_FALSE(test, edu_factorial_input_valid(LONG_MIN));
}

/* ---- DMA length validator ---- */

static void test_dma_len_valid_range(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, edu_dma_len_valid(1));
	KUNIT_EXPECT_TRUE(test, edu_dma_len_valid(EDU_DMA_BUF_SIZE / 2));
	KUNIT_EXPECT_TRUE(test, edu_dma_len_valid(EDU_DMA_BUF_SIZE));
}

static void test_dma_len_zero_rejected(struct kunit *test)
{
	KUNIT_EXPECT_FALSE(test, edu_dma_len_valid(0));
}

static void test_dma_len_overflow_rejected(struct kunit *test)
{
	KUNIT_EXPECT_FALSE(test, edu_dma_len_valid(EDU_DMA_BUF_SIZE + 1));
	KUNIT_EXPECT_FALSE(test, edu_dma_len_valid(U32_MAX));
}

/* ---- ioctl command number sanity ---- */

static void test_ioctl_roundtrip_magic(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)_IOC_TYPE(EDU_IOC_DMA_ROUNDTRIP), 'e');
}

static void test_ioctl_roundtrip_number(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, _IOC_NR(EDU_IOC_DMA_ROUNDTRIP), 1U);
}

static void test_ioctl_roundtrip_direction(struct kunit *test)
{
	/* _IOWR = read+write from the caller's perspective */
	KUNIT_EXPECT_EQ(test, _IOC_DIR(EDU_IOC_DMA_ROUNDTRIP),
			(unsigned int)(_IOC_READ | _IOC_WRITE));
}

static void test_ioctl_roundtrip_size(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, _IOC_SIZE(EDU_IOC_DMA_ROUNDTRIP),
			(unsigned int)sizeof(struct edu_dma_req));
}

/* ---- hardware constants ---- */

static void test_dma_buf_size(struct kunit *test)
{
	/* EDU device exposes a 4 KiB scratch buffer (edu.rst) */
	KUNIT_EXPECT_EQ(test, EDU_DMA_BUF_SIZE, 4096U);
}

static void test_dma_buf_offset(struct kunit *test)
{
	/* Scratch buffer starts at 0x40000 in device address space */
	KUNIT_EXPECT_EQ(test, EDU_DMA_BUF_OFFSET, 0x40000U);
}

/* ---- test suite ---- */

static struct kunit_case edu_kunit_cases[] = {
	KUNIT_CASE(test_factorial_valid_range),
	KUNIT_CASE(test_factorial_overflow_guard),
	KUNIT_CASE(test_factorial_negative_rejected),
	KUNIT_CASE(test_dma_len_valid_range),
	KUNIT_CASE(test_dma_len_zero_rejected),
	KUNIT_CASE(test_dma_len_overflow_rejected),
	KUNIT_CASE(test_ioctl_roundtrip_magic),
	KUNIT_CASE(test_ioctl_roundtrip_number),
	KUNIT_CASE(test_ioctl_roundtrip_direction),
	KUNIT_CASE(test_ioctl_roundtrip_size),
	KUNIT_CASE(test_dma_buf_size),
	KUNIT_CASE(test_dma_buf_offset),
	{},
};

static struct kunit_suite edu_kunit_suite = {
	.name  = "edu_driver",
	.test_cases = edu_kunit_cases,
};

kunit_test_suite(edu_kunit_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit tests for edu driver pure logic");
