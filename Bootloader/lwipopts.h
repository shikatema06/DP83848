/**
 * @file    Bootloader/lwipopts.h
 * @brief   LwIP configuration for IAP Bootloader — UDP only, minimal footprint.
 *
 *          This file is placed in the Bootloader/ include path so it overrides
 *          LWIP/Target/lwipopts.h when compiling the bootloader target.
 */

#ifndef __LWIPOPTS_BOOT_H__
#define __LWIPOPTS_BOOT_H__

#include "main.h"

/* ---------- System ---------- */
#define NO_SYS                  1
#define SYS_LIGHTWEIGHT_PROT    0
#define MEM_ALIGNMENT           4
#define LWIP_ETHERNET           1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_ICMP               1      /* ping debugging */

/* ---------- Memory ---------- */
#define MEM_SIZE                (6 * 1024)
#define MEMP_NUM_PBUF           12
#define MEMP_NUM_UDP_PCB        6
#define MEMP_NUM_TCP_PCB        0
#define PBUF_POOL_SIZE          10
#define PBUF_POOL_BUFSIZE       512

/* ---------- UDP only ---------- */
#define LWIP_UDP                1
#define LWIP_TCP                0
#define LWIP_RAW                0
#define LWIP_DHCP               0
#define LWIP_DNS                0
#define LWIP_IGMP               0
#define LWIP_NETCONN            0
#define LWIP_SOCKET             0

/* ---------- IPv4 only ---------- */
#define LWIP_IPV4               1
#define LWIP_IPV6               0
#define LWIP_ARP                1

/* ---------- Strip unused ---------- */
#define PPP_SUPPORT             0
#define LWIP_HAVE_LOOPIF        0
#define LWIP_NETIF_API          0
#define LWIP_STATS              0
#define LWIP_SNMP               0
#define LWIP_AUTOIP             0
#define LWIP_NETIF_HOSTNAME     1

/* ---------- Hardware checksum ---------- */
#define CHECKSUM_BY_HARDWARE    1
#define CHECKSUM_GEN_IP         0
#define CHECKSUM_GEN_UDP        0
#define CHECKSUM_GEN_ICMP       0
#define CHECKSUM_CHECK_IP       0
#define CHECKSUM_CHECK_UDP      0
#define CHECKSUM_CHECK_ICMP     0

#endif /* __LWIPOPTS_BOOT_H__ */
