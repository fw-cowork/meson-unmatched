/* SPDX-License-Identifier: MIT */
#ifndef BAREMETAL_IO_H
#define BAREMETAL_IO_H

#include <baremetal/types.h>

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

#endif
