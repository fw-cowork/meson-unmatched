// SPDX-License-Identifier: GPL-2.0+
/* DHCP application built directly on the standalone lwIP port. */

#include <console.h>
#include <env.h>
#include <linux/delay.h>
#include <lwip-port.h>
#include <time.h>

#include <lwip/dhcp.h>
#include <lwip/dns.h>
#include <lwip/prot/dhcp.h>

#include "lwip-port-internal.h"

#define LWIP_PORT_DHCP_TIMEOUT_MS       15000

static void set_indexed_env(const char *base, int index, const char *value)
{
	char name[20];

	if (index)
		snprintf(name, sizeof(name), "%s%d", base, index);
	else
		strlcpy(name, base, sizeof(name));
	env_set(name, value);
}

int lwip_port_dhcp(void)
{
	struct lwip_port_session session;
	const ip_addr_t *dns;
	struct dhcp *state;
	ulong start;
	int index;
	int ret;

	ret = lwip_port_open(&session, false);
	if (ret)
		return ret;

	ret = dhcp_start(&session.netif);
	if (ret != ERR_OK) {
		printf("lwipport: dhcp_start failed: %d\n", ret);
		goto out;
	}

	start = get_timer(0);
	while (!dhcp_supplied_address(&session.netif)) {
		if (ctrlc()) {
			puts("\nlwipport: DHCP aborted\n");
			ret = -EINTR;
			goto stop;
		}
		if (get_timer(start) >= LWIP_PORT_DHCP_TIMEOUT_MS) {
			puts("lwipport: DHCP timed out\n");
			ret = -ETIMEDOUT;
			goto stop;
		}
		ret = lwip_port_poll(&session);
		if (ret)
			goto stop;
		mdelay(1);
	}

	state = netif_dhcp_data(&session.netif);
	index = dev_seq(session.dev);
	set_indexed_env("ipaddr", index,
			ip4addr_ntoa(netif_ip4_addr(&session.netif)));
	set_indexed_env("netmask", index,
			ip4addr_ntoa(netif_ip4_netmask(&session.netif)));
	if (!ip4_addr_isany_val(*netif_ip4_gw(&session.netif)))
		set_indexed_env("gatewayip", index,
				ip4addr_ntoa(netif_ip4_gw(&session.netif)));
	if (!ip_addr_isany(&state->server_ip_addr))
		env_set("serverip", ipaddr_ntoa(&state->server_ip_addr));
#if LWIP_DHCP_BOOTP_FILE
	if (state->boot_file_name[0])
		env_set("bootfile", state->boot_file_name);
#endif
	dns = dns_getserver(0);
	if (dns && !ip_addr_isany(dns))
		env_set("dnsip", ipaddr_ntoa(dns));
	dns = dns_getserver(1);
	if (dns && !ip_addr_isany(dns))
		env_set("dnsip2", ipaddr_ntoa(dns));

	printf("lwipport: DHCP bound in %lu ms\n", get_timer(start));
	printf("  ipaddr=%s\n", ip4addr_ntoa(netif_ip4_addr(&session.netif)));
	printf("  netmask=%s\n",
	       ip4addr_ntoa(netif_ip4_netmask(&session.netif)));
	printf("  gatewayip=%s\n", ip4addr_ntoa(netif_ip4_gw(&session.netif)));
	ret = 0;

stop:
	/*
	 * The next lwipport command reuses this address as a static address.
	 * Move out of BOUND before stopping so lwIP closes its DHCP PCB without
	 * sending DHCPRELEASE and prematurely returning the lease to the server.
	 */
	state = netif_dhcp_data(&session.netif);
	if (state && dhcp_supplied_address(&session.netif))
		state->state = DHCP_STATE_INIT;
	dhcp_stop(&session.netif);
	dhcp_cleanup(&session.netif);
out:
	lwip_port_close(&session);
	return ret;
}
