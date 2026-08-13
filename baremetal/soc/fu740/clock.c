// SPDX-License-Identifier: MIT

#include <baremetal/io.h>
#include <soc/fu740.h>

#define FU740_HFCLK_HZ                    26000000UL
#define FU740_PRCI_BASE                   0x10000000UL

#define PRCI_HFPCLKPLL_CONFIG             0x50UL
#define PRCI_HFPCLKPLL_SELECT             0x58UL
#define PRCI_HFPCLK_DIVIDER               0x5cUL

#define PRCI_HFPCLKPLL_DIVR_MASK          0x3fU
#define PRCI_HFPCLKPLL_DIVF_SHIFT         6U
#define PRCI_HFPCLKPLL_DIVF_MASK          0x1ffU
#define PRCI_HFPCLKPLL_DIVQ_SHIFT         15U
#define PRCI_HFPCLKPLL_DIVQ_MASK          0x7U
#define PRCI_HFPCLKPLL_BYPASS             (1U << 24)
#define PRCI_HFPCLKPLL_SELECT_HFCLK       (1U << 0)

bm_ulong fu740_pclk_rate(void)
{
	bm_u32 config = bm_read32(FU740_PRCI_BASE + PRCI_HFPCLKPLL_CONFIG);
	bm_ulong clock = FU740_HFCLK_HZ;

	/* Derive the live PCLK from the PLL state left configured by U-Boot. */
	if (!(bm_read32(FU740_PRCI_BASE + PRCI_HFPCLKPLL_SELECT) &
	      PRCI_HFPCLKPLL_SELECT_HFCLK) &&
	    !(config & PRCI_HFPCLKPLL_BYPASS)) {
		bm_ulong divr = (config & PRCI_HFPCLKPLL_DIVR_MASK) + 1;
		bm_ulong divf =
			((config >> PRCI_HFPCLKPLL_DIVF_SHIFT) &
			 PRCI_HFPCLKPLL_DIVF_MASK) + 1;
		bm_u32 divq =
			(config >> PRCI_HFPCLKPLL_DIVQ_SHIFT) &
			PRCI_HFPCLKPLL_DIVQ_MASK;

		clock = clock * 2 * divf / divr;
		clock >>= divq;
	}

	return clock /
		(bm_read32(FU740_PRCI_BASE + PRCI_HFPCLK_DIVIDER) + 2UL);
}
