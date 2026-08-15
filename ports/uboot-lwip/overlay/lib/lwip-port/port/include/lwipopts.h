/* SPDX-License-Identifier: GPL-2.0+ */
/* Configuration for the standalone, single-threaded U-Boot lwIP port. */
#ifndef __UBOOT_STANDALONE_LWIPOPTS_H
#define __UBOOT_STANDALONE_LWIPOPTS_H

#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0

#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define ARP_TABLE_SIZE                  4
#define ARP_QUEUEING                    1
#define LWIP_ETHERNET                   1

#define LWIP_ICMP                       1
#define LWIP_RAW                        1
#define LWIP_DHCP                       1
#define LWIP_DHCP_BOOTP_FILE            1
#define LWIP_DHCP_DOES_ACD_CHECK        0
#define LWIP_AUTOIP                     0
#define LWIP_DNS                        1
#define DNS_TABLE_SIZE                  2

#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_CALLBACK_API               1
#define LWIP_ALTCP                      1
#define TCP_MSS                         1460
#define TCP_WND                         (16 * TCP_MSS)
#define TCP_SND_BUF                     (2 * TCP_MSS)
#define LWIP_WND_SCALE                  1
#define TCP_RCV_SCALE                   2

#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0
#define LWIP_STATS                      0
#define LWIP_IGMP                       0
#define LWIP_SNMP                       0
#define PPP_SUPPORT                     0
#define LWIP_HAVE_LOOPIF                0
#define LWIP_NETIF_LOOPBACK             0
#define LWIP_LISTEN_BACKLOG             0

/* U-Boot owns the allocator and has no RTOS heap lock in this port. */
#define MEM_ALIGNMENT                   8
#define MEM_LIBC_MALLOC                 1
#define MEMP_MEM_MALLOC                 1
#define MEMP_MEM_INIT                   1
#define PBUF_POOL_SIZE                  16
#define PBUF_LINK_HLEN                  14
#define PBUF_POOL_BUFSIZE               LWIP_MEM_ALIGN_SIZE(TCP_MSS + 40 + PBUF_LINK_HLEN)
#define MEMP_NUM_TCP_SEG                32

#define TFTP_MAX_FILENAME_LEN           128
#define TFTP_TIMEOUT_MSECS              1000
#define TFTP_MAX_RETRIES                10

#define IP_DEFAULT_TTL                  64
#define IP_REASSEMBLY                   1
#define IP_FRAG                         1
#define IP_REASS_MAXAGE                 3
#define IP_REASS_MAX_PBUFS              4

#define LWIP_TIMEVAL_PRIVATE            1
#define LWIP_NOASSERT                   1

#endif
