/**
 * @file    Bootloader/bootloader_main.c
 * @brief   Minimal jump-only Bootloader (Milestone 1).
 *
 *          Purpose: prove "Bootloader → App" jump is 100% reliable before
 *          re-adding IAP features. No lwIP, no TFTP, no slot/meta/CRC logic.
 *
 *          Flow:
 *            - Clock + GPIO + USART1 init.
 *            - Print "BOOT" on USART1, toggle PE7 (diagnostic).
 *            - After a short delay, jump to App at fixed address 0x08010000.
 *            - App is responsible for __enable_irq() and its own VTOR.
 */

#include "main.h"
#include "gpio.h"
#include "usart.h"
#include "boot_request.h"
#include "flash_if.h"
#include "iap_slot.h"
#include "lwip_boot.h"
#include "tftp_server.h"
#include <stdint.h>

typedef void (*pFunction)(void);

/* ---- Forward declarations ---- */
static void SystemClock_Config(void);
void Error_Handler(void);

/* ---- Time base: SysTick feeds HAL_GetTick/HAL_Delay ---- */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/**
 * @brief  Jump to Application at given address.
 *
 *          Clean handoff: disable interrupts, clear NVIC, stop SysTick,
 *          set VTOR + MSP, then call App Reset_Handler. The App must
 *          re-enable interrupts itself (both the minimal test App and the
 *          real App main() already do __enable_irq()).
 */
static void jump_to_app(uint32_t app_addr)
{
    uint32_t app_sp = *(volatile uint32_t *)app_addr;       /* vector[0] = initial SP */
    uint32_t app_pc = *(volatile uint32_t *)(app_addr + 4U);/* vector[1] = Reset_Handler */
    pFunction JumpToApp;
    uint32_t i;

    /* STM32F407VG has 192KB SRAM (0x20000000 - 0x20030000) */
    if (app_sp >= 0x20000000U && app_sp <= 0x20030000U) {

        /* 1. Disable all interrupts */
        __disable_irq();

        /* 2. Clear all NVIC enable bits (ICER0..7, 32-bit each) */
        for (i = 0; i < 8U; i++) {
            NVIC->ICER[i] = 0xFFFFFFFFU;
        }
        /* 3. Clear all NVIC pending bits (ICPR0..7) */
        for (i = 0; i < 8U; i++) {
            NVIC->ICPR[i] = 0xFFFFFFFFU;
        }

        /* 4. Stop SysTick */
        SysTick->CTRL = 0U;

        /* 5. Set VTOR and sync */
        SCB->VTOR = app_addr;
        __DSB();
        __ISB();

        /* 6. Set MSP */
        __set_MSP(app_sp);

        /* 7. Jump to App Reset_Handler */
        JumpToApp = (pFunction)app_pc;
        JumpToApp();
    }
    /* If SP is invalid, fall through and hang */
    Error_Handler();
}

/**
 * @brief  Bootloader entry point.
 *
 *          Boot strategy (Milestone 3b):
 *            - Read boot_request at 0x0800FFF8.
 *            - BOOT_REQUEST_UPGRADE (0x55AA55AA) → stay in IAP mode.
 *            - otherwise → iap_slot decides the active slot; jump to it if
 *              valid, else stay in IAP printing "NO APP".
 */
int main(void)
{
    uint32_t boot_request;
    boot_slot_id_t active;
    uint32_t app_addr;

    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    FLASH_If_Init();          /* 解锁 Flash：iap_slot_init / meta_commit 需要 */
    MX_USART1_UART_Init();

    /* RS485 方向：TX 前拉高 RE（PA11），发完拉低 */
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_SET);
    HAL_UART_Transmit(&huart1, (uint8_t *)"BOOT\r\n", 6, 100);
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_RESET);

    /* 诊断：Bootloader 跑到这里 → PE7 翻转 */
    HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_7);

    boot_request = *(volatile uint32_t *)BOOT_REQUEST_ADDR;

    if (boot_request == BOOT_REQUEST_UPGRADE) {
        /* IAP 模式：起 lwIP（Entry IP 172.16.1.69）+ TFTP 服务器，永久等待。
         * TFTP 传输完成时 tftp_server 内部会写 spare 槽 + iap_slot_commit
         * + NVIC_SystemReset()，复位后 Bootloader 跳到新固件。 */
        HAL_UART_Transmit(&huart1, (uint8_t *)"BOOT IAP\r\n", 10, 100);
        HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_RESET);

        Boot_LWIP_Init();
        IAP_tftpd_init();

        while (1) {
            Boot_LWIP_Process();
        }
    }

    /* NORMAL：用 iap_slot 状态机决定跳转目标 */
    iap_slot_init();          /* 修复状态机（dirty boot / 卡在 ERASING 等） */
    active = iap_slot_get_active();

    if (active != SLOT_ID_NONE && iap_slot_validate(active) == SLOT_IMG_VALID) {
        app_addr = iap_slot_start_addr(active);
        HAL_Delay(500);       /* 短暂停留让串口可见 */
        jump_to_app(app_addr);
    }

    /* 无有效 App：停 IAP，打印 NO APP */
    while (1) {
        HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_7);
        HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_SET);
        HAL_UART_Transmit(&huart1, (uint8_t *)"NO APP\r\n", 8, 100);
        HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_RESET);
        HAL_Delay(300);
    }
}

/**
 * @brief  System Clock Configuration — identical to Application.
 *         HSE 25MHz → PLL → 168 MHz SYSCLK.
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 25;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}
