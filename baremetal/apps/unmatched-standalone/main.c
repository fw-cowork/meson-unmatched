/* SPDX-License-Identifier: MIT */

#include <baremetal/app.h>
#include <baremetal/types.h>
#include <drivers/sifive_uart.h>
#include <soc/fu740.h>

extern char __stack_bottom[];
extern char __stack_top[];

/* Confirm that start-standalone.S cleared NOLOAD data before entering C. */
static volatile bm_ulong bss_probe;

static __attribute__((noreturn)) void halt_forever(void)
{
	for (;;) {
		/* Keep the hart in the payload without relying on trap/WFI setup. */
		__asm__ volatile("" ::: "memory");
	}
}

bm_ulong baremetal_main(int argc, char *const argv[])
{
	const bm_ulong uart = FU740_CONSOLE_UART_BASE;
	bm_ulong stack_pointer;
	bm_ulong stack_bottom = (bm_ulong)__stack_bottom;
	bm_ulong stack_top = (bm_ulong)__stack_top;

	(void)argc;
	(void)argv;

	__asm__ volatile("mv %0, sp" : "=r"(stack_pointer));

	sifive_uart_puts(uart, "unmatched-standalone: private runtime\n  sp = ");
	sifive_uart_put_hex_ulong(uart, stack_pointer);

	if (stack_pointer <= stack_bottom || stack_pointer > stack_top) {
		sifive_uart_puts(uart, "\nprivate stack: FAIL\n");
		halt_forever();
	}
	if (bss_probe != 0) {
		sifive_uart_puts(uart, "\nBSS clear: FAIL\n");
		halt_forever();
	}

	sifive_uart_puts(uart,
		"\nprivate stack: PASS\n"
		"BSS clear: PASS\n"
		"payload owns this hart; reset the board to recover\n");
	halt_forever();
}
