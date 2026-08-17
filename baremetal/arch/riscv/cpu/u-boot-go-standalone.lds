/* SPDX-License-Identifier: MIT */

OUTPUT_ARCH(riscv)
ENTRY(_start)

IMAGE_BASE = 0x84000000;
IMAGE_SIZE = 64K;
STACK_SIZE = 16K;

MEMORY
{
	image (rwx) : ORIGIN = IMAGE_BASE, LENGTH = IMAGE_SIZE
}

SECTIONS
{
	. = ORIGIN(image);
	__image_start = .;

	.text : ALIGN(4) {
		KEEP(*(.text.entry))
		*(.text .text.*)
	} > image

	.rodata : ALIGN(8) {
		*(.rodata .rodata.*)
		*(.srodata .srodata.*)
	} > image

	.data : ALIGN(8) {
		*(.data .data.*)
		*(.sdata .sdata.*)
	} > image

	.bss (NOLOAD) : ALIGN(8) {
		__bss_start = .;
		*(.bss .bss.*)
		*(.sbss .sbss.*)
		*(COMMON)
		. = ALIGN(8);
		__bss_end = .;
	} > image

	__image_end = .;

	/* The private stack occupies the top 16 KiB and grows downward. */
	__stack_top = ORIGIN(image) + LENGTH(image);
	__stack_bottom = __stack_top - STACK_SIZE;

	/DISCARD/ : {
		*(.comment)
		*(.note .note.*)
		*(.eh_frame .eh_frame.*)
		*(.riscv.attributes)
	}

	ASSERT(_start == ORIGIN(image), "_start must be the first image byte")
	ASSERT((__stack_top & 15) == 0, "standalone stack must be 16-byte aligned")
	ASSERT(__image_end <= __stack_bottom,
	       "bare-metal image overlaps the 16 KiB standalone stack")
}
