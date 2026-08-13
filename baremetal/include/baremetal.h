/* SPDX-License-Identifier: MIT */
#ifndef BAREMETAL_H
#define BAREMETAL_H

typedef unsigned int bm_u32;
typedef unsigned long bm_ulong;

#define BM_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

static inline bm_u32 bm_read32(bm_ulong address)
{
	return *(volatile bm_u32 *)address;
}

static inline void bm_write32(bm_ulong address, bm_u32 value)
{
	*(volatile bm_u32 *)address = value;
}

static inline void bm_io_fence(void)
{
	__asm__ volatile("fence iorw, iorw" ::: "memory");
}

int bm_streq(const char *left, const char *right);
bm_ulong baremetal_main(int argc, char *const argv[]);

#endif
