// SPDX-License-Identifier: MIT

#include <baremetal/app.h>
#include <baremetal/types.h>
#include <drivers/sifive_uart.h>
#include <soc/fu740.h>

enum mmode_check_result {
	MMODE_CHECK_PASS = 0,
	MMODE_CHECK_INVALID_ARGUMENTS = 2,
};

struct mmode_csr_snapshot {
	bm_ulong mhartid;
	bm_ulong mstatus;
	bm_ulong mtvec;
};

/* Keep the captured values available for a debugger without changing CSRs. */
static volatile struct mmode_csr_snapshot snapshot;

bm_ulong baremetal_main(int argc, char *const argv[])
{
	const bm_ulong uart = FU740_CONSOLE_UART_BASE;
	bm_ulong mhartid;
	bm_ulong mstatus;
	bm_ulong mtvec;

	if (!argv || argc != 1 || !argv[0])
		return MMODE_CHECK_INVALID_ARGUMENTS;

	sifive_uart_puts(uart, "unmatched-mmode-check: probing M-mode CSRs\n");

	/* These reads trap in S-mode; reaching the return proves M-mode access. */
	__asm__ volatile("csrr %0, mhartid" : "=r"(mhartid));
	__asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
	__asm__ volatile("csrr %0, mtvec" : "=r"(mtvec));

	snapshot.mhartid = mhartid;
	snapshot.mstatus = mstatus;
	snapshot.mtvec = mtvec;

	sifive_uart_puts(uart, "  mhartid = ");
	sifive_uart_put_hex_ulong(uart, mhartid);
	sifive_uart_puts(uart, "\n  mstatus = ");
	sifive_uart_put_hex_ulong(uart, mstatus);
	sifive_uart_puts(uart, "\n  mtvec   = ");
	sifive_uart_put_hex_ulong(uart, mtvec);
	sifive_uart_puts(uart, "\nM-mode CSR access: PASS\n");

	return MMODE_CHECK_PASS;
}
