/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Public API of the standalone lwIP/U-Boot example.
 *
 * The command layer deliberately does not expose lwIP types.  All lwIP
 * headers stay below this boundary, which makes the port easier to replace
 * or test independently from U-Boot's command parser.
 */
#ifndef __UBOOT_LWIP_PORT_H
#define __UBOOT_LWIP_PORT_H

#include <linux/types.h>

int lwip_port_info(void);
int lwip_port_dhcp(void);
int lwip_port_ping(const char *host);
int lwip_port_tftp(ulong addr, const char *server, const char *filename);
int lwip_port_http(ulong addr, const char *url);

#endif
