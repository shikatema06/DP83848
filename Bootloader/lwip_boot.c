/**
 * @file    Bootloader/lwip_boot.c
 * @brief   Minimal LwIP stack initialisation for the IAP Bootloader.
 *
 *          Reuses net_config.c to obtain the same IP as the Application,
 *          so the field PC does not need to change its network settings.
 */

#include "lwip_boot.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"
#include "ethernetif.h"
#include "net_config.h"
#include "dp83848.h"
#include <string.h>

struct netif gnetif;

static uint32_t EthernetLinkTimer;

/* ---- Link state helpers (same logic as App's lwip.c) ---- */
static void ethernet_link_status_updated(struct netif *netif)
{
    /* No-op: Bootloader doesn't need LED / LCD feedback */
    (void)netif;
}

static void Ethernet_Link_Periodic_Handle(struct netif *netif)
{
    if (HAL_GetTick() - EthernetLinkTimer >= 100U) {
        EthernetLinkTimer = HAL_GetTick();
        ethernet_link_check_state(netif);
    }
}

void Boot_LWIP_Init(void)
{
    uint8_t  ip[4], mask[4];
    ip4_addr_t ipaddr, netmask, gw;

    net_config_init();

    /* Use the same active IP as the Application */
    if (net_config_get_active_ip(ip, mask) != 0) {
        /* Fallback (should never happen after net_config_init) */
        ip[0] = 172; ip[1] = 16; ip[2] = 1; ip[3] = 69;
        mask[0] = 255; mask[1] = 255; mask[2] = 255; mask[3] = 0;
    }

    IP4_ADDR(&ipaddr,  ip[0],  ip[1],  ip[2],  ip[3]);
    IP4_ADDR(&netmask, mask[0], mask[1], mask[2], mask[3]);
    IP4_ADDR(&gw, 0, 0, 0, 0);

    lwip_init();

    netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &ethernet_input);
    netif_set_default(&gnetif);
    netif_set_up(&gnetif);

    netif_set_link_callback(&gnetif, ethernet_link_status_updated);
}

void Boot_LWIP_Process(void)
{
    ethernetif_input(&gnetif);
    sys_check_timeouts();
    Ethernet_Link_Periodic_Handle(&gnetif);
}
