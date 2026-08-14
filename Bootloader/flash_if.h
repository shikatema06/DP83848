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

//#include "stm32f4xx_hal.h"
#include "main.h"

/* Bootloader 区 */
#define BOOTLOADER_BASE_ADDR        0x08000000U
#define BOOTLOADER_SIZE             (48U * 1024U)      /* 48KB, sector 0-2 */

/* Flag/Config 区（sector 3，可安全擦除，专门存 boot_request / slot_state） */
#define FLAG_SECTOR_START_ADDR      0x0800C000U
#define FLAG_SECTOR_END_ADDR        0x08010000U

#include "boot_request.h"

/* 槽管理标志区（sector 3 末尾）— BOOT_SLOT_ADDR 定义见 boot_request.h（App/Boot 共享） */

/* App 区（Slot A + Slot B + Reserved） */
#define USER_FLASH_FIRST_PAGE_ADDR  0x08010000U        /* App 区起始 */
#define USER_FLASH_END_ADDR         0x080E0000U        /* App 区结束（net_config 起始） */

/* Slot A (sector 4,5,6 = 64+128+128KB = 320KB) */
#define SLOT_A_START_ADDR           0x08010000U
#define SLOT_A_END_ADDR             0x08060000U        /* 不含此地址 */
#define SLOT_A_SIZE                 (SLOT_A_END_ADDR - SLOT_A_START_ADDR)  /* 320KB */

/* Slot B */
#define SLOT_B_START_ADDR           0x08060000U
#define SLOT_B_END_ADDR             0x080C0000U        /* 不含此地址 */
#define SLOT_B_SIZE                 (SLOT_B_END_ADDR - SLOT_B_START_ADDR)  /* 384KB */

/* 预留区 */
#define RESERVED_START_ADDR         0x080C0000U
#define RESERVED_END_ADDR           0x080E0000U

/* net_config 区（禁止擦除） */
#define NET_CONFIG_START_ADDR       0x080E0000U
#define NET_CONFIG_END_ADDR         0x08100000U        /* Flash 末尾 */

void     FLASH_If_Init(void);
uint32_t FLASH_Get_Sector(uint32_t addr);
int8_t   FLASH_If_Erase_Sector(uint32_t EraseStartAddress);
int8_t   FLASH_If_Erase_Range(uint32_t EraseStartAddress, uint32_t EraseEndAddress);
int8_t   FLASH_If_Write_Boot_Request(uint32_t value);
uint32_t FLASH_If_Write(__IO uint32_t *FlashAddress, uint32_t *Data, uint16_t DataLength);
#endif /* __FLASH_IF_BOOT_H */
