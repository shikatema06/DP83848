/**
 * @file    TestApp/main.c
 * @brief   Minimal jump-test application (Milestone 1).
 *
 *          Purpose: prove the Bootloader → App jump is clean. On entry:
 *            - Set VTOR to own vector table.
 *            - Re-enable interrupts (Bootloader disabled them before jump).
 *            - Print "APP OK" on USART1, then blink PE7 forever.
 *
 *          If this prints and blinks, the jump itself is proven good;
 *          swap in the real 92KB App afterwards.
 */

#include "main.h"
#include "gpio.h"
#include "usart.h"

/* ---- Time base: SysTick feeds HAL_GetTick/HAL_Delay ---- */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

static void SystemClock_Config(void);
void Error_Handler(void);

int main(void)
{
    /* App 链接到 0x08010000，VTOR 必须指向自己的向量表。
     * g_pfnVectors 由启动文件导出。 */
    extern uint32_t g_pfnVectors[];
    SCB->VTOR = (uint32_t)g_pfnVectors;

    /* Bootloader 跳转前 __disable_irq() 并停了 SysTick，
     * 这里必须重新开中断，否则 HAL_Delay / 超时全部死循环。 */
    __enable_irq();

    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    /* RS485 方向：TX 前拉高 RE（PA11），发完拉低 */
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_SET);
    HAL_UART_Transmit(&huart1, (uint8_t *)"APP OK\r\n", 8, 100);
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_RESET);

    /* 200ms 翻转 PE7 + 持续打印 —— 肉眼可见 App 在跑，串口持续有输出 */
    while (1) {
        HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_7);

        /* RS485 方向：TX 前拉高 RE（PA11），发完拉低 */
        HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_SET);
        HAL_UART_Transmit(&huart1, (uint8_t *)"APP OK\r\n", 8, 100);
        HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_RESET);

        HAL_Delay(200);
    }
}

/**
 * @brief  System Clock Configuration — HSE 25MHz → PLL → 168 MHz.
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
