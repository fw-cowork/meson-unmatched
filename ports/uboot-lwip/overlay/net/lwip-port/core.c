// SPDX-License-Identifier: GPL-2.0+
/*
 * U-Boot device-model Ethernet adaptation for lwIP's NO_SYS mode.
 *
 * There is no RX thread in U-Boot.  Protocol commands therefore call
 * lwip_port_poll(), which drains the NIC, feeds lwIP, advances protocol
 * timers, and yields to U-Boot's cooperative scheduler.
 */

#include <console.h>
#include <env.h>
#include <lwip-port.h>
#include <lwip-port-version.h>
#include <malloc.h>
#include <net-common.h>
#include <time.h>
#include <timer.h>
#include <u-boot/schedule.h>

#include <lwip/dns.h>
#include <lwip/etharp.h>
#include <lwip/init.h>
#include <lwip/pbuf.h>
#include <lwip/timeouts.h>

#include "lwip-port-internal.h"

#define LWIP_PORT_DNS_TIMEOUT_MS        10000

static bool lwip_initialized;

static void env_name(char *buffer, size_t size, const char *base, int index)
{
	if (index)
		snprintf(buffer, size, "%s%d", base, index);
	else
		strlcpy(buffer, base, size);
}

static void read_env_ipv4(struct udevice *dev, ip4_addr_t *ip,
			  ip4_addr_t *mask, ip4_addr_t *gateway)
{
	char name[20];
	const char *value;
	int index = dev_seq(dev);

	ip4_addr_set_zero(ip);
	ip4_addr_set_zero(mask);
	ip4_addr_set_zero(gateway);

	env_name(name, sizeof(name), "ipaddr", index);
	value = env_get(name);
	if (value)
		ip4addr_aton(value, ip);

	env_name(name, sizeof(name), "netmask", index);
	value = env_get(name);
	if (value)
		ip4addr_aton(value, mask);

	env_name(name, sizeof(name), "gatewayip", index);
	value = env_get(name);
	if (value)
		ip4addr_aton(value, gateway);
}

static void configure_dns_from_env(void)
{
	ip_addr_t server;
	const char *value;
	int index;

	for (index = 0; index < DNS_MAX_SERVERS; index++) {
		value = env_get(index ? "dnsip2" : "dnsip");
		if (value && ipaddr_aton(value, &server))
			dns_setserver(index, &server);
	}
}

static err_t lwip_port_linkoutput(struct netif *netif, struct pbuf *p)
{
	struct udevice *dev = netif->state;
	void *packet = p->payload;
	void *copy = NULL;
	int ret;

	/* A chained pbuf is not contiguous, and some NICs require PKTALIGN. */
	if (p->next || (ulong)p->payload % PKTALIGN) {
		copy = memalign(PKTALIGN, p->tot_len);
		if (!copy)
			return ERR_MEM;
		if (pbuf_copy_partial(p, copy, p->tot_len, 0) != p->tot_len) {
			free(copy);
			return ERR_BUF;
		}
		packet = copy;
	}

	ret = eth_get_ops(dev)->send(dev, packet, p->tot_len);
	free(copy);

	return ret < 0 ? ERR_IF : ERR_OK;
}

static err_t lwip_port_netif_init(struct netif *netif)
{
	netif->name[0] = 'u';
	netif->name[1] = 'b';
	netif->output = etharp_output;
	netif->linkoutput = lwip_port_linkoutput;
	netif->mtu = 1500;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
		       NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;

	return ERR_OK;
}

int lwip_port_open(struct lwip_port_session *session, bool use_env_address)
{
	uchar mac[ARP_HLEN];
	ip4_addr_t ip, mask, gateway;
	struct udevice *dev;
	int ret;

	memset(session, 0, sizeof(*session));

	/* net_init() initializes the packet buffers expected by U-Boot drivers. */
	ret = net_init();
	if (ret)
		return ret;

	eth_halt();
	eth_set_current();
	ret = eth_init();
	if (ret < 0)
		return ret;

	dev = eth_get_dev();
	if (!dev) {
		eth_halt();
		return -ENODEV;
	}
	if (!eth_env_get_enetaddr_by_index("eth", dev_seq(dev), mac) ||
	    !is_valid_ethaddr(mac)) {
		printf("lwipport: invalid MAC address for %s\n", dev->name);
		eth_halt();
		return -EINVAL;
	}

	if (!lwip_initialized) {
		lwip_init();
		lwip_initialized = true;
	}

	ip4_addr_set_zero(&ip);
	ip4_addr_set_zero(&mask);
	ip4_addr_set_zero(&gateway);
	if (use_env_address)
		read_env_ipv4(dev, &ip, &mask, &gateway);

	session->dev = dev;
	memcpy(session->netif.hwaddr, mac, sizeof(mac));
	session->netif.hwaddr_len = sizeof(mac);
	if (!netif_add(&session->netif, &ip, &mask, &gateway, dev,
		       lwip_port_netif_init, netif_input)) {
		eth_halt();
		return -ENODEV;
	}

	netif_set_default(&session->netif);
	netif_set_up(&session->netif);
	netif_set_link_up(&session->netif);
	configure_dns_from_env();
	session->open = true;

	printf("lwipport: %s, MAC %pM, IP %s\n", dev->name, mac,
	       ip4addr_ntoa(netif_ip4_addr(&session->netif)));
	return 0;
}

void lwip_port_close(struct lwip_port_session *session)
{
	if (!session->open)
		return;

	netif_set_link_down(&session->netif);
	netif_set_down(&session->netif);
	netif_remove(&session->netif);
	eth_halt();
	memset(session, 0, sizeof(*session));
}

int lwip_port_poll(struct lwip_port_session *session)
{
	struct eth_ops *ops;
	struct pbuf *p;
	uchar *packet = NULL;
	int flags = ETH_RECV_CHECK_DEVICE;
	int ret = 0;
	int count;

	if (!session->open || !eth_is_active(session->dev))
		return -ENODEV;

	sys_check_timeouts();
	schedule();
	ops = eth_get_ops(session->dev);

	for (count = 0; count < ETH_PACKETS_BATCH_RECV; count++) {
		ret = ops->recv(session->dev, flags, &packet);
		flags = 0;
		if (ret > 0) {
			p = pbuf_alloc(PBUF_RAW, ret, PBUF_POOL);
			if (!p) {
				printf("lwipport: RX drop, pbuf pool exhausted\n");
			} else if (pbuf_take(p, packet, ret) != ERR_OK) {
				pbuf_free(p);
			} else {
				/* netif_input() consumes the pbuf on all Ethernet paths. */
				session->netif.input(p, &session->netif);
			}
		}
		if (ret >= 0 && ops->free_pkt)
			ops->free_pkt(session->dev, packet, ret);
		if (ret <= 0)
			break;
	}

	return ret == -EAGAIN ? 0 : min(ret, 0);
}

struct dns_query {
	ip_addr_t address;
	char name[DNS_MAX_NAME_LENGTH + 1];
	bool done;
	bool found;
};

static struct dns_query dns_query_state;

static void dns_found(const char *name, const ip_addr_t *address, void *arg)
{
	struct dns_query *query = arg;

	if (strcmp(name, query->name))
		return;
	if (address) {
		query->address = *address;
		query->found = true;
	}
	query->done = true;
}

int lwip_port_resolve(struct lwip_port_session *session, const char *name,
		      ip_addr_t *address)
{
	struct dns_query *query = &dns_query_state;
	ulong start;
	err_t err;

	if (ipaddr_aton(name, address))
		return 0;

	memset(query, 0, sizeof(*query));
	strlcpy(query->name, name, sizeof(query->name));
	err = dns_gethostbyname(name, address, dns_found, query);
	if (err == ERR_OK)
		return 0;
	if (err != ERR_INPROGRESS)
		return -EHOSTUNREACH;

	start = get_timer(0);
	while (!query->done && get_timer(start) < LWIP_PORT_DNS_TIMEOUT_MS) {
		if (ctrlc())
			return -EINTR;
		if (lwip_port_poll(session) < 0)
			return -EIO;
	}
	if (!query->done || !query->found)
		return -ETIMEDOUT;

	*address = query->address;
	return 0;
}

int lwip_port_info(void)
{
	printf("standalone lwIP port\n");
	printf("  lwIP version: %s\n", LWIP_VERSION_STRING);
	printf("  source commit: %s\n", LWIP_PORT_SOURCE_REV);
	printf("  execution: NO_SYS=1, polled U-Boot DM_ETH\n");
	printf("  protocols: IPv4 ARP ICMP DHCP DNS UDP TCP TFTP HTTP\n");
	return 0;
}

u32_t sys_now(void)
{
	return get_timer(0);
}
