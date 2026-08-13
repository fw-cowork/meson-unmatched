/* SPDX-License-Identifier: MIT */
#ifndef BAREMETAL_TYPES_H
#define BAREMETAL_TYPES_H

typedef unsigned int bm_u32;
typedef unsigned long bm_ulong;

/* The U-Boot go ABI and all MMIO helpers in this tree target RV64. */
_Static_assert(sizeof(bm_u32) == 4, "bm_u32 must be 32 bits");
_Static_assert(sizeof(bm_ulong) == 8, "bm_ulong must be 64 bits");

#define BM_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#endif
