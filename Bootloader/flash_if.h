/**
 * @file    Bootloader/flash_if.h
 * @brief   Flash erase/write interface for IAP Bootloader.
 *
 *          Adapted from STM32Cube_FW_F4 LwIP_IAP flash_if.h.
 *          Application area: Sectors 4–10 (0x08010000 – 0x080DFFFF).
 *          Sector 11 (0x080E0000) is reserved for net_config and MUST NOT be erased.
 */

#ifndef __FLASH_IF_BOOT_H
#define __FLASH_IF_BOOT_H

#include "main.h"

/* ---- Application Flash boundaries (Sectors 4–10, 832 KB total) ---- */
#define USER_FLASH_FIRST_PAGE_ADDRESS  0x08010000U
#define USER_FLASH_END_ADDRESS         0x080E0000U  /* First byte AFTER app area */

/* ---- boot_valid flag location (Sector 3 last word, at end of Bootloader's 64KB) ---- */
#define BOOT_VALID_ADDR                0x0800FFFCU
#define BOOT_VALID_MAGIC               0xAA55AA55U

void     FLASH_If_Init(void);
uint32_t FLASH_Get_Sector(uint32_t addr);
int8_t   FLASH_If_Erase_Sector(uint32_t EraseStartAddress);
uint32_t FLASH_If_Write(__IO uint32_t *FlashAddress, uint32_t *Data, uint16_t DataLength);

#endif /* __FLASH_IF_BOOT_H */
