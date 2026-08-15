// SPDX-License-Identifier: MIT

#include <baremetal/io.h>
#include <drivers/sifive_uart.h>

#define SIFIVE_UART_TXFIFO_FULL  (1U << 31)
#define SIFIVE_UART_RXFIFO_EMPTY (1U << 31)
#define SIFIVE_UART_RXFIFO_DATA  0xffU
#define SIFIVE_UART_TXCTRL_TXEN  (1U << 0)
#define SIFIVE_UART_RXCTRL_RXEN  (1U << 0)

struct sifive_uart_regs {
	bm_u32 txfifo;
	bm_u32 rxfifo;
	bm_u32 txctrl;
	bm_u32 rxctrl;
	bm_u32 interrupt_enable;
	bm_u32 interrupt_pending;
	bm_u32 divisor;
};

static volatile struct sifive_uart_regs *sifive_uart_regs(bm_ulong base)
{
	return (volatile struct sifive_uart_regs *)base;
}

int sifive_uart_configure(bm_ulong base, bm_ulong input_hz, bm_ulong baud)
{
	volatile struct sifive_uart_regs *regs;
	bm_ulong divisor;

	if (!base || !input_hz || !baud)
		return SIFIVE_UART_INVALID_ARGUMENT;

	/* f_baud = f_in / (div + 1), rounded up to not exceed baud. */
	divisor = (input_hz + baud - 1) / baud;
	if (divisor)
		divisor--;

	regs = sifive_uart_regs(base);
	regs->divisor = (bm_u32)divisor;
	regs->txctrl = SIFIVE_UART_TXCTRL_TXEN;
	regs->rxctrl = SIFIVE_UART_RXCTRL_RXEN;
	regs->interrupt_enable = 0;
	bm_io_fence();

	return SIFIVE_UART_OK;
}

void sifive_uart_putc(bm_ulong base, char character)
{
	volatile struct sifive_uart_regs *regs = sifive_uart_regs(base);

	while (regs->txfifo & SIFIVE_UART_TXFIFO_FULL)
		;
	regs->txfifo = (bm_u32)(unsigned char)character;
	bm_io_fence();
}

int sifive_uart_getc_nonblocking(bm_ulong base)
{
	volatile struct sifive_uart_regs *regs = sifive_uart_regs(base);
	bm_u32 value = regs->rxfifo;

	if (value & SIFIVE_UART_RXFIFO_EMPTY)
		return SIFIVE_UART_NO_DATA;

	return (int)(value & SIFIVE_UART_RXFIFO_DATA);
}

int sifive_uart_getc(bm_ulong base)
{
	int character;

	do {
		character = sifive_uart_getc_nonblocking(base);
	} while (character == SIFIVE_UART_NO_DATA);

	return character;
}

void sifive_uart_puts(bm_ulong base, const char *text)
{
	if (!text)
		return;

	while (*text) {
		if (*text == '\n')
			sifive_uart_putc(base, '\r');
		sifive_uart_putc(base, *text++);
	}
}

void sifive_uart_put_hex_ulong(bm_ulong base, bm_ulong value)
{
	static const char digits[] = "0123456789abcdef";
	int shift;

	sifive_uart_puts(base, "0x");
	for (shift = 60; shift >= 0; shift -= 4)
		sifive_uart_putc(base, digits[(value >> shift) & 0xfUL]);
}
