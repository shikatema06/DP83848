/**
 * @file    Bootloader/flash_if.c
 * @brief   Flash erase/write for IAP — per-sector lazy erase.
 *
 *          Adapted from STM32Cube_FW_F4 LwIP_IAP flash_if.c.
 *          Key change: erases one sector at a time (on first write into it)
 *          instead of bulk-erasing all sectors before the first DATA block.
 */

#include "flash_if.h"
#include "stm32f4xx_hal_def.h"
#include <stdint.h>

/* ---- STM32F407 sector address lookup ---- */
uint32_t FLASH_Get_Sector(uint32_t addr)
{
    if (addr < 0x08004000U) return FLASH_SECTOR_0;
    if (addr < 0x08008000U) return FLASH_SECTOR_1;
    if (addr < 0x0800C000U) return FLASH_SECTOR_2;
    if (addr < 0x08010000U) return FLASH_SECTOR_3;
    if (addr < 0x08020000U) return FLASH_SECTOR_4;
    if (addr < 0x08040000U) return FLASH_SECTOR_5;
    if (addr < 0x08060000U) return FLASH_SECTOR_6;
    if (addr < 0x08080000U) return FLASH_SECTOR_7;
    if (addr < 0x080A0000U) return FLASH_SECTOR_8;
    if (addr < 0x080C0000U) return FLASH_SECTOR_9;
    if (addr < 0x080E0000U) return FLASH_SECTOR_10;
    return FLASH_SECTOR_11;
}

/* ---- Sector size for range erase (F407 1MB layout) ---- */
static uint32_t FLASH_Get_Sector_Size(uint32_t sector)
{
    switch (sector)
    {
        case FLASH_SECTOR_0:
        case FLASH_SECTOR_1:
        case FLASH_SECTOR_2:
        case FLASH_SECTOR_3:
            return 16U * 1024U;
        case FLASH_SECTOR_4:
            return 64U * 1024U;
        case FLASH_SECTOR_5:
        case FLASH_SECTOR_6:
        case FLASH_SECTOR_7:
        case FLASH_SECTOR_8:
        case FLASH_SECTOR_9:
        case FLASH_SECTOR_10:
        case FLASH_SECTOR_11:
            return 128U * 1024U;
        default:
            return 0U;
    }
}

void FLASH_If_Init(void)
{
    HAL_FLASH_Unlock();
}

/**
 * @brief  Erase a single Flash sector containing EraseStartAddress.
 * @retval 0 = success, -1 = failure
 */
int8_t FLASH_If_Erase_Sector(uint32_t EraseStartAddress)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0;

    /* Guard: never touch sector 11 (net_config) or beyond */
    if (EraseStartAddress >= NET_CONFIG_START_ADDR)
        return -1;

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.Sector       = FLASH_Get_Sector(EraseStartAddress);
    erase.NbSectors    = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
        return -1;

    return 0;
}

/**
 * @brief  Erase all Flash sectors overlapping [EraseStartAddress, EraseEndAddress).
 * @param  EraseStartAddress  first address of the range (inclusive)
 * @param  EraseEndAddress    first address after the range (exclusive)
 * @retval 0 = success, -1 = failure
 *
 * @note   Range must stay inside App/Reserved area:
 *         [USER_FLASH_FIRST_PAGE_ADDR, NET_CONFIG_START_ADDR).
 *         sector 11 (net_config) is never erased.
 */
int8_t FLASH_If_Erase_Range(uint32_t EraseStartAddress, uint32_t EraseEndAddress)
{
    /* Guard: must be inside App/Reserved area */
    if (EraseStartAddress < USER_FLASH_FIRST_PAGE_ADDR)
        return -1;

    if (EraseEndAddress > NET_CONFIG_START_ADDR)
        return -1;

    /* Empty range: nothing to do */
    if (EraseStartAddress >= EraseEndAddress)
        return 0;

    uint32_t addr = EraseStartAddress;
    while (addr < EraseEndAddress)
    {
        uint32_t sector      = FLASH_Get_Sector(addr);
        uint32_t sector_size = FLASH_Get_Sector_Size(sector);

        if (sector_size == 0U)
            return -1;

        if (FLASH_If_Erase_Sector(addr) != 0)
            return -1;

        addr += sector_size;
    }

    return 0;
}

/**
 * @brief  Write word-aligned data to Flash (caller ensures address is erased).
 * @retval 0 = success, 1 = program error, 2 = verify error
 */
uint32_t FLASH_If_Write(__IO uint32_t *FlashAddress, uint32_t *Data, uint16_t DataLength)
{
    uint32_t i;

    for (i = 0; (i < DataLength) && (*FlashAddress <= (USER_FLASH_END_ADDR - 4U)); i++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, *FlashAddress, Data[i]) != HAL_OK)
            return 1;

        if (*(__IO uint32_t *)*FlashAddress != Data[i])
            return 2;

        *FlashAddress += 4U;
    }
    return 0;
}


/**
 * @brief  Write the boot request flag at BOOT_REQUEST_ADDR.
 * @param  value  BOOT_REQUEST_UPGRADE or BOOT_REQUEST_NORMAL
 * @retval 0 = success, -1 = failure
 *
 * @note   方案 3：不清除 boot_request。
 *         App 调用此函数写 UPGRADE，然后 NVIC_SystemReset()。
 *         Bootloader 启动后读 boot_request，但最终是否进入升级模式
 *         由 slot_state 决定。NORMAL 分支在此方案中不使用。
 */
 
int8_t FLASH_If_Write_Boot_Request(uint32_t value)
{
    /* Only accept legal values */
    if (value != BOOT_REQUEST_NORMAL && value != BOOT_REQUEST_UPGRADE)
        return -1;

    uint32_t current = *(volatile uint32_t *)BOOT_REQUEST_ADDR;

    /* Already the requested value */
    if (current == value)
        return 0;

    /* UPGRADE: erased (0xFFFFFFFF) -> 0x55AA55AA. Pure 1->0, safe. */
    if (value == BOOT_REQUEST_UPGRADE)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              BOOT_REQUEST_ADDR,
                              BOOT_REQUEST_UPGRADE) != HAL_OK)
            return -1;

        return 0;
    }

    /* 方案 3：不清除 boot_request，由 slot_state 决定启动行为。
    * 此分支不使用，直接返回失败。
    */

    if (value == BOOT_REQUEST_NORMAL) 
    {
        /* if(FLASH_If_Erase_Sector(FLAG_SECTOR_START_ADDR)!=HAL_OK)

            return -1;

        return 0; */
        return -1;
    }
    return -1;
    
}
