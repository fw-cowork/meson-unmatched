/* SPDX-License-Identifier: GPL-2.0+ */
/* Compiler and libc hooks for the standalone U-Boot lwIP port. */
#ifndef __UBOOT_STANDALONE_LWIP_CC_H
#define __UBOOT_STANDALONE_LWIP_CC_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <rand.h>
#include <vsprintf.h>

#define LWIP_ERRNO_INCLUDE              <errno.h>
#define LWIP_ERRNO_STDINCLUDE           1
#define LWIP_NO_UNISTD_H                1
#define LWIP_TIMEVAL_PRIVATE            1
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS
#define LWIP_ERR_T                      int
#define LWIP_CONST_CAST(target_type, val) ((target_type)((uintptr_t)(val)))

#define LWIP_RAND()                     ((u32_t)rand())
#define atoi(str)                       ((int)dectoul((str), NULL))
#define lwip_strnstr(a, b, c)            strnstr((a), (b), (c))

#define LWIP_PLATFORM_ASSERT(message) do { \
	printf("lwIP assert: %s (%s:%d)\n", (message), __FILE__, __LINE__); \
} while (0)

#endif
