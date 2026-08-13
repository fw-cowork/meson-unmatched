/* SPDX-License-Identifier: MIT */
#ifndef SOC_FU740_H
#define SOC_FU740_H

#include <baremetal/types.h>

#define FU740_PWM0_BASE 0x10020000UL
#define FU740_PWM1_BASE 0x10021000UL

/* Return the peripheral clock rate configured by the running boot firmware. */
bm_ulong fu740_pclk_rate(void);

#endif
