/* SPDX-License-Identifier: MIT */

	.section .text.entry, "ax", @progbits
	.align 2
	.global _start
	.type _start, @function
_start:
	/* Stop using U-Boot's call stack before entering application C code. */
	la sp, __stack_top

	/* The raw BIN omits the NOLOAD .bss section, so initialize it here. */
	la t0, __bss_start
	la t1, __bss_end
.Lbss_clear:
	bgeu t0, t1, .Lbss_done
	sd zero, 0(t0)
	addi t0, t0, 8
	j .Lbss_clear
.Lbss_done:
	/* Preserve U-Boot's argc/argv in a0/a1 and enter the application. */
	call baremetal_main

	/* A standalone application must never resume the suspended U-Boot call. */
.Lhang:
	j .Lhang
	.size _start, . - _start

	.section .note.GNU-stack, "", @progbits
