/* SPDX-License-Identifier: MIT */

	.section .text.entry, "ax", @progbits
	.align 2
	.global _start
	.type _start, @function
_start:
	/* Preserve U-Boot's return address before the C call overwrites ra. */
	addi sp, sp, -16
	sd ra, 8(sp)

	/* The raw BIN omits the NOLOAD .bss section, so initialize it here. */
	la t0, __bss_start
	la t1, __bss_end
.Lbss_clear:
	bgeu t0, t1, .Lbss_done
	sd zero, 0(t0)
	addi t0, t0, 8
	j .Lbss_clear
.Lbss_done:
	call baremetal_main

	/* Keep the return value in a0 and resume U-Boot with its original stack. */
	ld ra, 8(sp)
	addi sp, sp, 16
	ret
	.size _start, . - _start

	.section .note.GNU-stack, "", @progbits
