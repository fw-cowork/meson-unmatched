// SPDX-License-Identifier: GPL-2.0+
/* TFTP download using the latest upstream lwIP tftp_client application. */

#include <console.h>
#include <display_options.h>
#include <env.h>
#include <lmb.h>
#include <linux/delay.h>
#include <lwip-port.h>
#include <mapmem.h>
#include <time.h>

#include <lwip/apps/tftp_client.h>
#include <lwip/timeouts.h>

#include "lwip-port-internal.h"

#define LWIP_PORT_TFTP_IDLE_MS          12000

enum transfer_result {
	TRANSFER_ACTIVE,
	TRANSFER_OK,
	TRANSFER_FAILED,
};

struct tftp_state {
	ulong base;
	ulong next;
	ulong size;
	ulong last_progress;
	enum transfer_result result;
};

static int store_data(struct tftp_state *state, const void *data, size_t size)
{
	void *destination;

	if (state->next + size < state->next)
		return -EOVERFLOW;
	if (CONFIG_IS_ENABLED(LMB) && lmb_read_check(state->next, size)) {
		puts("\nlwipport: TFTP would overwrite reserved memory\n");
		return -EPERM;
	}

	destination = map_sysmem(state->next, size);
	memcpy(destination, data, size);
	unmap_sysmem(destination);
	state->next += size;
	state->size += size;
	state->last_progress = get_timer(0);

	return 0;
}

static void *tftp_open_cb(const char *filename, const char *mode, u8_t write)
{
	return NULL;
}

static void tftp_close_cb(void *handle)
{
	struct tftp_state *state = handle;

	if (state->result == TRANSFER_ACTIVE)
		state->result = TRANSFER_OK;
}

static int tftp_read_cb(void *handle, void *buffer, int bytes)
{
	return -1;
}

static int tftp_write_cb(void *handle, struct pbuf *p)
{
	struct tftp_state *state = handle;
	struct pbuf *part;

	for (part = p; part; part = part->next) {
		if (store_data(state, part->payload, part->len)) {
			state->result = TRANSFER_FAILED;
			return -1;
		}
	}
	return 0;
}

static void tftp_error_cb(void *handle, int error, const char *message,
			  int size)
{
	struct tftp_state *state = handle;
	int printable = min(size, 96);

	state->result = TRANSFER_FAILED;
	printf("\nlwipport: TFTP error %d: %.*s\n", error, printable, message);
}

static const struct tftp_context tftp_callbacks = {
	.open = tftp_open_cb,
	.close = tftp_close_cb,
	.read = tftp_read_cb,
	.write = tftp_write_cb,
	.error = tftp_error_cb,
};

int lwip_port_tftp(ulong addr, const char *server, const char *filename)
{
	struct lwip_port_session session;
	struct tftp_state state = {
		.base = addr,
		.next = addr,
		.result = TRANSFER_ACTIVE,
	};
	ip_addr_t server_addr;
	bool initialized = false;
	err_t err;
	int ret;

	if (!addr || !filename[0])
		return -EINVAL;
	ret = lwip_port_open(&session, true);
	if (ret)
		return ret;
	ret = lwip_port_resolve(&session, server, &server_addr);
	if (ret) {
		printf("lwipport: cannot resolve TFTP server %s (%d)\n", server,
		       ret);
		goto out;
	}

	err = tftp_init_client(&tftp_callbacks);
	if (err != ERR_OK) {
		printf("lwipport: tftp_init_client failed: %d\n", err);
		ret = -EIO;
		goto out;
	}
	initialized = true;
	state.last_progress = get_timer(0);

	printf("lwipport: TFTP %s:%s -> 0x%lx\n",
	       ipaddr_ntoa(&server_addr), filename, addr);
	err = tftp_get(&state, &server_addr, TFTP_PORT, filename,
		       TFTP_MODE_OCTET);
	if (err != ERR_OK) {
		printf("lwipport: tftp_get failed: %d\n", err);
		state.result = TRANSFER_FAILED;
	}

	while (state.result == TRANSFER_ACTIVE) {
		if (ctrlc()) {
			puts("\nlwipport: TFTP aborted\n");
			state.result = TRANSFER_FAILED;
			break;
		}
		if (get_timer(state.last_progress) >= LWIP_PORT_TFTP_IDLE_MS) {
			puts("\nlwipport: TFTP idle timeout\n");
			state.result = TRANSFER_FAILED;
			break;
		}
		ret = lwip_port_poll(&session);
		if (ret) {
			state.result = TRANSFER_FAILED;
			break;
		}
		mdelay(1);
	}

	ret = state.result == TRANSFER_OK ? 0 : -EIO;
	if (!ret) {
		env_set_hex("fileaddr", state.base);
		env_set_hex("filesize", state.size);
		printf("lwipport: %lu bytes transferred (", state.size);
		print_size(state.size, ")\n");
	}

out:
	if (initialized) {
		if (state.result == TRANSFER_ACTIVE)
			state.result = TRANSFER_FAILED;
		tftp_cleanup();
	}
	lwip_port_close(&session);
	return ret;
}
