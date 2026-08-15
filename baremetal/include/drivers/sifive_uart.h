/* SPDX-License-Identifier: MIT */
#ifndef DRIVERS_SIFIVE_UART_H
#define DRIVERS_SIFIVE_UART_H

#include <baremetal/types.h>

enum sifive_uart_result {
	SIFIVE_UART_OK = 0,
	SIFIVE_UART_INVALID_ARGUMENT = -1,
	SIFIVE_UART_NO_DATA = -2,
};

/* Configure an 8-N-1 UART. Existing U-Boot go applications need not call it. */
int sifive_uart_configure(bm_ulong base, bm_ulong input_hz, bm_ulong baud);

/* Blocking polled output and input; no interrupt state is changed. */
void sifive_uart_putc(bm_ulong base, char character);
int sifive_uart_getc(bm_ulong base);
int sifive_uart_getc_nonblocking(bm_ulong base);

/* Console helpers use CRLF line endings and fixed-width RV64 hexadecimal. */
void sifive_uart_puts(bm_ulong base, const char *text);
void sifive_uart_put_hex_ulong(bm_ulong base, bm_ulong value);

#endif
