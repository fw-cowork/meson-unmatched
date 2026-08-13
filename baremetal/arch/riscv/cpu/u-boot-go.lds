/* SPDX-License-Identifier: MIT */

OUTPUT_ARCH(riscv)
ENTRY(_start)

IMAGE_BASE = 0x84000000;
IMAGE_SIZE = 64K;

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

	/DISCARD/ : {
		*(.comment)
		*(.note .note.*)
		*(.eh_frame .eh_frame.*)
		*(.riscv.attributes)
	}

	ASSERT(_start == ORIGIN(image), "_start must be the first image byte")
	ASSERT(__image_end <= ORIGIN(image) + LENGTH(image),
	       "bare-metal image exceeds 64 KiB")
}
