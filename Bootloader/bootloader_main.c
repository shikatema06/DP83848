/**
 * @file    Bootloader/bootloader_main.c
 * @brief   IAP Bootloader entry point.
 *
 *          On reset:
 *            - If boot_valid == 0xAA55AA55 at 0x0800FFFC →
 *                verify App SP, then jump to App (0x08010000).
 *            - Otherwise → init ETH + LwIP + TFTP server and wait for firmware.
 */

#include "main.h"
#include "gpio.h"
#include "flash_if.h"
#include "lwip_boot.h"
#include "tftp_server.h"

/* ---- App entry in Flash ---- */
#define APP_START_ADDR  0x08010000U

/* ---- Fault-capture buffer in RAM (0x2001E000, below main stack) ---- */
#define FAULT_CAPTURE_ADDR  0x2001E000U
typedef struct {
    volatile uint32_t cpu_fault_type;     /* +0:  3=HardFault, 4=MemManage, 5=BusFault, 6=UsageFault */
    volatile uint32_t stacked_r0;         /* +4 */
    volatile uint32_t stacked_r1;         /* +8 */
    volatile uint32_t stacked_r2;         /* +12 */
    volatile uint32_t stacked_r3;         /* +16 */
    volatile uint32_t stacked_r12;        /* +20 */
    volatile uint32_t stacked_lr;         /* +24: LR at fault */
    volatile uint32_t stacked_pc;         /* +28: PC at fault */
    volatile uint32_t stacked_xpsr;       /* +32: xPSR at fault */
    volatile uint32_t cfsr;               /* +36: Configurable Fault Status Register (0xE000ED28) */
    volatile uint32_t hfsr;               /* +40: HardFault Status Register (0xE000ED2C) */
    volatile uint32_t mmfar;              /* +44: MemManage Fault Address Register */
    volatile uint32_t bfar;               /* +48: BusFault Address Register */
    volatile uint32_t boot_valid_value;   /* +52: boot_valid read at entry */
    volatile uint32_t entry_lr;           /* +56: EXC_RETURN value (indicates MSP/PSP) */
} fault_capture_t;

typedef void (*pFunction)(void);

/* ---- Forward declarations ---- */
static void SystemClock_Config(void);
void Error_Handler(void);

/* ---- Fault handlers for Bootloader ---- */
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);

/**
 * @brief  SysTick ISR — calls HAL tick increment.
 */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/**
 * @brief  Jump to Application at 0x08010000.
 */
static void jump_to_app(void)
{
    uint32_t app_sp, app_pc;
    pFunction JumpToApp;

    app_sp = *(volatile uint32_t *)APP_START_ADDR;
    app_pc = *(volatile uint32_t *)(APP_START_ADDR + 4U);

    if (app_sp >= 0x20000000U && app_sp <= 0x20020000U) {
        SCB->VTOR = APP_START_ADDR;
        __set_MSP(app_sp);
        JumpToApp = (pFunction)app_pc;
        JumpToApp();
    }
    /* If SP is invalid, return — caller continues IAP mode */
}

/**
 * @brief  Main entry point.
 *
 *          Boot strategy:
 *            - Always enter IAP mode first.
 *            - If boot_valid == MAGIC (App exists) → start 60-second timeout.
 *              After 60s with no TFTP activity, jump to App.
 *            - If boot_valid != MAGIC → wait forever in IAP mode.
 */
int main(void)
{
    uint32_t boot_valid;
    uint32_t iap_start_tick;
    int       app_exists;

    /* ---- Read boot_valid flag ---- */
    boot_valid = *(volatile uint32_t *)BOOT_VALID_ADDR;
    app_exists = (boot_valid == BOOT_VALID_MAGIC);

    /* ================================================================ */
    /*  IAP (Bootloader) mode                                           */
    /* ================================================================ */

    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();

    Boot_LWIP_Init();
    IAP_tftpd_init();

    iap_start_tick = HAL_GetTick();

    while (1) {
        Boot_LWIP_Process();

        if (app_exists) {
            /* If TFTP transfer started, let it finish — don't timeout */
            if (IAP_tftpd_has_activity())
                continue;

            /* 60-second timeout → jump to App */
            if (HAL_GetTick() - iap_start_tick >= 60000U) {
                jump_to_app();
                /* If jump_to_app returns (invalid SP), stay in IAP forever */
                app_exists = 0;
            }
        }
    }
}

/**
 * @brief  System Clock Configuration — identical to Application.
 *         HSE → PLL → 168 MHz SYSCLK.
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
    while (1) {}
}

/* -------------------------------------------------------------------------- */
/*  Fault handlers — capture state to FAULT_CAPTURE_ADDR for post-mortem      */
/* -------------------------------------------------------------------------- */

static void __attribute__((used)) capture_fault(uint32_t *stack_frame, uint32_t fault_type, uint32_t exc_return)
{
    fault_capture_t *f = (fault_capture_t *)FAULT_CAPTURE_ADDR;

    f->cpu_fault_type = fault_type;
    f->stacked_r0     = stack_frame[0];
    f->stacked_r1     = stack_frame[1];
    f->stacked_r2     = stack_frame[2];
    f->stacked_r3     = stack_frame[3];
    f->stacked_r12    = stack_frame[4];
    f->stacked_lr     = stack_frame[5];
    f->stacked_pc     = stack_frame[6];
    f->stacked_xpsr   = stack_frame[7];
    f->cfsr           = (*((volatile uint32_t *)0xE000ED28));
    f->hfsr           = (*((volatile uint32_t *)0xE000ED2C));
    f->mmfar          = (*((volatile uint32_t *)0xE000ED34));
    f->bfar           = (*((volatile uint32_t *)0xE000ED38));
    f->entry_lr       = exc_return;

    __disable_irq();
    while (1) {}
}

void NMI_Handler(void)
{
    __disable_irq();
    while (1) {}
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile (
        "mrs  r0, msp           \n"  /* r0 = stack frame pointer */
        "mov  r1, #3            \n"  /* r1 = fault_type (HardFault) */
        "mov  r2, lr            \n"  /* r2 = EXC_RETURN */
        "b    capture_fault     \n"
    );
}

__attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile (
        "mrs  r0, msp           \n"
        "mov  r1, #4            \n"
        "mov  r2, lr            \n"
        "b    capture_fault     \n"
    );
}

__attribute__((naked)) void BusFault_Handler(void)
{
    __asm volatile (
        "mrs  r0, msp           \n"
        "mov  r1, #5            \n"
        "mov  r2, lr            \n"
        "b    capture_fault     \n"
    );
}

__attribute__((naked)) void UsageFault_Handler(void)
{
    __asm volatile (
        "mrs  r0, msp           \n"
        "mov  r1, #6            \n"
        "mov  r2, lr            \n"
        "b    capture_fault     \n"
    );
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    while (1) {}
}
#endif
