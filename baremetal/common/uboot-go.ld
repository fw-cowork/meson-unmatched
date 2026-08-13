OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
	. = 0x84000000;
	__image_start = .;

	.text : ALIGN(4) {
		KEEP(*(.text.entry))
		*(.text .text.*)
	}

	.rodata : ALIGN(8) {
		*(.rodata .rodata.*)
		*(.srodata .srodata.*)
	}

	.data : ALIGN(8) {
		*(.data .data.*)
		*(.sdata .sdata.*)
	}

	.bss (NOLOAD) : ALIGN(8) {
		__bss_start = .;
		*(.bss .bss.*)
		*(.sbss .sbss.*)
		*(COMMON)
		. = ALIGN(8);
		__bss_end = .;
	}

	__image_end = .;

	/DISCARD/ : {
		*(.comment)
		*(.note .note.*)
		*(.eh_frame .eh_frame.*)
		*(.riscv.attributes)
	}

	ASSERT(__image_end <= 0x84010000, "bare-metal image exceeds 64 KiB")
}
