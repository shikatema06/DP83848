/**
 * @file    Core/Inc/boot_request.h
 * @brief   Boot request flag shared between Bootloader and Application.
 *
 *          Located in sector 3 (flag/config sector), just before the
 *          slot management word. The App writes BOOT_REQUEST_UPGRADE and
 *          resets; the Bootloader reads it on cold boot to decide whether
 *          to stay in IAP/TFTP mode.
 */

#ifndef __BOOT_REQUEST_H
#define __BOOT_REQUEST_H

#include <stdint.h>

#define BOOT_REQUEST_ADDR           0x0800FFF8U
#define BOOT_REQUEST_NORMAL         0xFFFFFFFFU
#define BOOT_REQUEST_UPGRADE        0x55AA55AAU

/* ---- Active slot 查询（Bootloader iap_slot 写入、App 只读，供 PC 升级工具用） ---- */
/* 单一事实源：布局调整时与 Bootloader/iap_slot.h 同步修改 */
/* 槽 id 数值与 iap_slot.h 枚举一致：0=Slot A, 1=Slot B */
#define BOOT_SLOT_ADDR              0x0800FFFCU   /* sector3 末尾：槽管理字 */
#define BOOT_SLOT_MAGIC             0x55AA0000U   /* 槽管理字高 16 位 */
#define BOOT_SLOT_DECODE(word)      ((word) & 0xFFU)

#endif /* __BOOT_REQUEST_H */
