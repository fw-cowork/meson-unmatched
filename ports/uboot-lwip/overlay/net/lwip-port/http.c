// SPDX-License-Identifier: GPL-2.0+
/* Plain HTTP downloader using lwIP's callback-based http_client app. */

#include <console.h>
#include <display_options.h>
#include <env.h>
#include <lmb.h>
#include <linux/delay.h>
#include <lwip-port.h>
#include <mapmem.h>
#include <time.h>

#include <lwip/apps/http_client.h>
#include <lwip/altcp.h>

#include "lwip-port-internal.h"

#define LWIP_PORT_HTTP_HOST_MAX         254

struct http_state {
	ulong base;
	ulong next;
	ulong size;
	ulong start;
	bool done;
	bool success;
	bool abort_requested;
};

static struct http_state http_state;
static httpc_connection_t http_settings;

static int http_store(struct http_state *state, const void *data, size_t size)
{
	void *destination;

	if (state->next + size < state->next)
		return -EOVERFLOW;
	if (CONFIG_IS_ENABLED(LMB) && lmb_read_check(state->next, size)) {
		puts("\nlwipport: HTTP would overwrite reserved memory\n");
		return -EPERM;
	}

	destination = map_sysmem(state->next, size);
	memcpy(destination, data, size);
	unmap_sysmem(destination);
	state->next += size;
	state->size += size;
	return 0;
}

static err_t http_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p,
			  err_t err)
{
	struct http_state *state = arg;
	struct pbuf *part;

	if (!p)
		return ERR_OK;
	if (state->abort_requested) {
		pbuf_free(p);
		altcp_abort(pcb);
		state->done = true;
		return ERR_ABRT;
	}

	for (part = p; part; part = part->next) {
		if (http_store(state, part->payload, part->len)) {
			pbuf_free(p);
			altcp_abort(pcb);
			state->done = true;
			return ERR_ABRT;
		}
	}
	altcp_recved(pcb, p->tot_len);
	pbuf_free(p);
	return ERR_OK;
}

static void http_result_cb(void *arg, httpc_result_t result,
			   u32_t content_length, u32_t status, err_t err)
{
	struct http_state *state = arg;

	state->success = result == HTTPC_RESULT_OK && status == 200 &&
			 !state->abort_requested;
	state->done = true;
	if (!state->success)
		printf("\nlwipport: HTTP failed: result=%d status=%u err=%d\n",
		       result, status, err);
}

static err_t http_headers_cb(httpc_state_t *connection, void *arg,
			     struct pbuf *headers, u16_t header_length,
			     u32_t content_length)
{
	struct http_state *state = arg;

	if (state->abort_requested)
		return ERR_ABRT;
	printf("lwipport: HTTP content length: ");
	if (content_length == 0xffffffffU)
		puts("unknown\n");
	else
		printf("%u\n", content_length);
	return ERR_OK;
}

static int parse_http_url(const char *url, char *host, size_t host_size,
			  u16_t *port, const char **path)
{
	const char *authority;
	const char *end;
	const char *colon;
	char *port_end;
	ulong parsed_port;
	size_t length;

	if (strncmp(url, "http://", 7))
		return -EINVAL;
	authority = url + 7;
	end = strchr(authority, '/');
	if (!end)
		end = authority + strlen(authority);
	colon = memchr(authority, ':', end - authority);
	length = (colon ? colon : end) - authority;
	if (!length || length >= host_size)
		return -EINVAL;
	memcpy(host, authority, length);
	host[length] = '\0';

	*port = HTTP_DEFAULT_PORT;
	if (colon) {
		parsed_port = simple_strtoul(colon + 1, &port_end, 10);
		if (port_end != end || !parsed_port || parsed_port > 65535)
			return -EINVAL;
		*port = parsed_port;
	}
	*path = *end ? end : "/";
	return 0;
}

int lwip_port_http(ulong addr, const char *url)
{
	struct lwip_port_session session;
	char host[LWIP_PORT_HTTP_HOST_MAX];
	const char *path;
	u16_t port;
	err_t err;
	int ret;

	if (!addr)
		return -EINVAL;
	ret = parse_http_url(url, host, sizeof(host), &port, &path);
	if (ret) {
		puts("lwipport: only http://host[:port]/path is supported\n");
		return ret;
	}
	ret = lwip_port_open(&session, true);
	if (ret)
		return ret;

	memset(&http_state, 0, sizeof(http_state));
	memset(&http_settings, 0, sizeof(http_settings));
	http_state.base = addr;
	http_state.next = addr;
	http_state.start = get_timer(0);
	http_settings.result_fn = http_result_cb;
	http_settings.headers_done_fn = http_headers_cb;

	printf("lwipport: HTTP %s:%u%s -> 0x%lx\n", host, port, path, addr);
	err = httpc_get_file_dns(host, port, path, &http_settings, http_recv_cb,
				 &http_state, NULL);
	if (err != ERR_OK) {
		printf("lwipport: HTTP request failed to start: %d\n", err);
		ret = -EIO;
		goto out;
	}

	while (!http_state.done) {
		if (ctrlc() && !http_state.abort_requested) {
			puts("\nlwipport: aborting HTTP request\n");
			http_state.abort_requested = true;
		}
		ret = lwip_port_poll(&session);
		if (ret)
			break;
		mdelay(1);
	}

	ret = http_state.success ? 0 : -EIO;
	if (!ret) {
		env_set_hex("fileaddr", http_state.base);
		env_set_hex("filesize", http_state.size);
		printf("lwipport: %lu bytes in %lu ms (", http_state.size,
		       get_timer(http_state.start));
		print_size(http_state.size, ")\n");
	}
out:
	lwip_port_close(&session);
	return ret;
}
