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

#endif /* __BOOT_REQUEST_H */
