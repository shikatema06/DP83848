/**
 * @file    Bootloader/lwip_boot.h
 * @brief   Minimal LwIP network init for IAP Bootloader.
 */

#ifndef __LWIP_BOOT_H
#define __LWIP_BOOT_H

#include "lwip/netif.h"

extern struct netif gnetif;

void Boot_LWIP_Init(void);
void Boot_LWIP_Process(void);

#endif /* __LWIP_BOOT_H */
