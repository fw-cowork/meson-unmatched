/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __UBOOT_LWIP_PORT_INTERNAL_H
#define __UBOOT_LWIP_PORT_INTERNAL_H

#include <dm/device.h>
#include <lwip/ip_addr.h>
#include <lwip/netif.h>

struct lwip_port_session {
	struct netif netif;
	struct udevice *dev;
	bool open;
};

int lwip_port_open(struct lwip_port_session *session, bool use_env_address);
void lwip_port_close(struct lwip_port_session *session);
int lwip_port_poll(struct lwip_port_session *session);
int lwip_port_resolve(struct lwip_port_session *session, const char *name,
		      ip_addr_t *address);

#endif
