/**
 ******************************************************************************
 * @file      syscalls_iar.c
 * @brief     IAR EWARM 低层 I/O 接口 — 重定向 printf 到 USART1 (RS485)
 * @note      用于替换 syscalls.c（GCC/picolibc 专用，IAR 不兼容）
 *            IAR 编译时将此文件加入工程，排除 syscalls.c
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"

/* IAR DLIB 低层写接口 — printf 最终会调用此函数 */
size_t __write(int handle, const unsigned char *buffer, size_t size)
{
  /* handle 1 = stdout, 2 = stderr */
  if (handle == 1 || handle == 2)
  {
    size_t i;

    /* RS485 发送使能 */
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_SET);
    HAL_Delay(1);

    /* 逐字节发送 */
    for (i = 0; i < size; i++)
    {
      HAL_UART_Transmit(&huart1, (uint8_t *)&buffer[i], 1, HAL_MAX_DELAY);
    }

    /* 等待最后一位送出后切回接收 */
    HAL_Delay(1);
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_RESET);

    return size;
  }
  return 0;
}

/* IAR DLIB 低层读接口 — 供 scanf 等使用（暂未实现） */
size_t __read(int handle, unsigned char *buffer, size_t size)
{
  /* handle 0 = stdin */
  if (handle == 0)
  {
    /* TODO: 实现 USART1 接收 */
    return 0;
  }
  return 0;
}
