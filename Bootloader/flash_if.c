/**
 * @file    Bootloader/flash_if.c
 * @brief   Flash erase/write for IAP — per-sector lazy erase.
 *
 *          Adapted from STM32Cube_FW_F4 LwIP_IAP flash_if.c.
 *          Key change: erases one sector at a time (on first write into it)
 *          instead of bulk-erasing all sectors before the first DATA block.
 */

#include "flash_if.h"

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
    if (EraseStartAddress >= USER_FLASH_END_ADDRESS)
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
 * @brief  Write word-aligned data to Flash (caller ensures address is erased).
 * @retval 0 = success, 1 = program error, 2 = verify error
 */
uint32_t FLASH_If_Write(__IO uint32_t *FlashAddress, uint32_t *Data, uint16_t DataLength)
{
    uint32_t i;

    for (i = 0; (i < DataLength) && (*FlashAddress <= (USER_FLASH_END_ADDRESS - 4U)); i++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, *FlashAddress, Data[i]) != HAL_OK)
            return 1;

        if (*(__IO uint32_t *)*FlashAddress != Data[i])
            return 2;

        *FlashAddress += 4U;
    }
    return 0;
}
