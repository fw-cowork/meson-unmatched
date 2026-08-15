// SPDX-License-Identifier: GPL-2.0+
/* Command front end for the standalone external lwIP port. */

#include <command.h>
#include <linux/string.h>
#include <lwip-port.h>
#include <vsprintf.h>

static int do_lwipport(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	char *end;
	ulong addr;
	int ret;

	if (argc == 2 && !strcmp(argv[1], "info")) {
		ret = lwip_port_info();
	} else if (argc == 2 && !strcmp(argv[1], "dhcp")) {
		ret = lwip_port_dhcp();
	} else if (argc == 3 && !strcmp(argv[1], "ping")) {
		ret = lwip_port_ping(argv[2]);
	} else if (argc == 5 && !strcmp(argv[1], "tftp")) {
		addr = hextoul(argv[2], &end);
		if (!*argv[2] || *end)
			return CMD_RET_USAGE;
		ret = lwip_port_tftp(addr, argv[3], argv[4]);
	} else if (argc == 4 && !strcmp(argv[1], "http")) {
		addr = hextoul(argv[2], &end);
		if (!*argv[2] || *end)
			return CMD_RET_USAGE;
		ret = lwip_port_http(addr, argv[3]);
	} else {
		return CMD_RET_USAGE;
	}

	return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

U_BOOT_CMD(lwipport, 5, 0, do_lwipport,
	   "exercise the standalone external lwIP port",
	   "info\n"
	   "lwipport dhcp\n"
	   "lwipport ping <host-or-ip>\n"
	   "lwipport tftp <load-address> <server-ip> <filename>\n"
	   "lwipport http <load-address> <http://host[:port]/path>");
