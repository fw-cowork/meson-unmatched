// SPDX-License-Identifier: GPL-2.0+
/* ICMP echo implemented with lwIP's raw callback API. */

#include <console.h>
#include <linux/delay.h>
#include <lwip-port.h>
#include <time.h>

#include <lwip/inet_chksum.h>
#include <lwip/prot/icmp.h>
#include <lwip/prot/ip4.h>
#include <lwip/raw.h>

#include "lwip-port-internal.h"

#define LWIP_PORT_PING_ID               0x5542
#define LWIP_PORT_PING_COUNT            4
#define LWIP_PORT_PING_INTERVAL_MS      1000
#define LWIP_PORT_PING_TIMEOUT_MS       5000

struct ping_state {
	ip_addr_t target;
	u16_t sequence;
	u16_t reply_sequence;
	bool replied;
};

static u8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p,
		      const ip_addr_t *source)
{
	struct ping_state *state = arg;
	struct icmp_echo_hdr echo;
	struct ip_hdr ip;
	u16_t offset;
	u16_t sequence;

	if (!ip_addr_cmp(source, &state->target) || p->tot_len < sizeof(ip))
		return 0;
	if (pbuf_copy_partial(p, &ip, sizeof(ip), 0) != sizeof(ip))
		return 0;
	offset = IPH_HL_BYTES(&ip);
	if (offset < IP_HLEN || p->tot_len < offset + sizeof(echo))
		return 0;
	if (pbuf_copy_partial(p, &echo, sizeof(echo), offset) != sizeof(echo))
		return 0;
	sequence = lwip_ntohs(echo.seqno);
	if (ICMPH_TYPE(&echo) != ICMP_ER || ICMPH_CODE(&echo) != 0 ||
	    echo.id != lwip_htons(LWIP_PORT_PING_ID) || !sequence ||
	    sequence > state->sequence)
		return 0;

	state->reply_sequence = sequence;
	state->replied = true;
	printf("lwipport: reply from %s, seq=%u\n", ipaddr_ntoa(source),
	       state->reply_sequence);
	pbuf_free(p);
	return 1;
}

static int ping_send(struct raw_pcb *pcb, struct ping_state *state)
{
	struct icmp_echo_hdr *echo;
	struct pbuf *p;
	err_t err;

	p = pbuf_alloc(PBUF_IP, sizeof(*echo), PBUF_RAM);
	if (!p)
		return -ENOMEM;
	if (p->len != p->tot_len) {
		pbuf_free(p);
		return -ENOMEM;
	}

	echo = p->payload;
	ICMPH_TYPE_SET(echo, ICMP_ECHO);
	ICMPH_CODE_SET(echo, 0);
	echo->chksum = 0;
	echo->id = lwip_htons(LWIP_PORT_PING_ID);
	echo->seqno = lwip_htons(state->sequence);
	echo->chksum = inet_chksum(echo, sizeof(*echo));
	err = raw_sendto(pcb, p, &state->target);
	pbuf_free(p);

	return err == ERR_OK ? 0 : -EIO;
}

int lwip_port_ping(const char *host)
{
	struct lwip_port_session session;
	struct ping_state state = {};
	struct raw_pcb *pcb = NULL;
	ulong next_send = 0;
	ulong start;
	int ret;

	ret = lwip_port_open(&session, true);
	if (ret)
		return ret;
	ret = lwip_port_resolve(&session, host, &state.target);
	if (ret) {
		printf("lwipport: cannot resolve %s (%d)\n", host, ret);
		goto out;
	}

	pcb = raw_new(IP_PROTO_ICMP);
	if (!pcb) {
		ret = -ENOMEM;
		goto out;
	}
	raw_recv(pcb, ping_recv, &state);
	raw_bind_netif(pcb, &session.netif);

	printf("lwipport: ping %s (%s)\n", host, ipaddr_ntoa(&state.target));
	start = get_timer(0);
	while (!state.replied && get_timer(start) < LWIP_PORT_PING_TIMEOUT_MS) {
		if (state.sequence < LWIP_PORT_PING_COUNT &&
		    get_timer(start) >= next_send) {
			state.sequence++;
			ret = ping_send(pcb, &state);
			if (ret)
				break;
			next_send += LWIP_PORT_PING_INTERVAL_MS;
		}
		if (ctrlc()) {
			puts("\nlwipport: ping aborted\n");
			ret = -EINTR;
			break;
		}
		ret = lwip_port_poll(&session);
		if (ret)
			break;
		mdelay(1);
	}

	if (!ret && !state.replied) {
		printf("lwipport: no reply from %s\n", ipaddr_ntoa(&state.target));
		ret = -ETIMEDOUT;
	}
	raw_remove(pcb);
out:
	lwip_port_close(&session);
	return ret;
}
