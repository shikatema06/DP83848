/**
  ******************************************************************************
  * @file    usart_quick_test.c
  * @brief   快速串口测试示例 - 简化版
  * @note    适合快速验证串口基本功能
  ******************************************************************************
  */

#include "main.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/* 快速测试函数声明 */
void QuickTest_USART1_TX(void);
void QuickTest_USART1_RX_Echo(void);
void QuickTest_All_UART_Send(const char *msg);

/**
  * @brief  快速测试1: USART1发送测试
  * @note   在PC端串口助手应能看到输出
  */
void QuickTest_USART1_TX(void)
{
    const char *test_msg = "\r\n=== Quick TX Test ===\r\n";
    const char *hello = "Hello from STM32F407!\r\n";
    
    /* 使能RS485发送 */
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_SET);
    HAL_Delay(1);
    
    /* 发送测试消息 */
    HAL_UART_Transmit(&huart1, (uint8_t*)test_msg, strlen(test_msg), 100);
    HAL_Delay(10);
    
    HAL_UART_Transmit(&huart1, (uint8_t*)hello, strlen(hello), 100);
    HAL_Delay(10);
    
    /* 发送计数器 */
    for (int i = 1; i <= 5; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Count: %d\r\n", i);
        HAL_UART_Transmit(&huart1, (uint8_t*)buf, strlen(buf), 100);
        HAL_Delay(100);
    }
    
    /* 恢复接收模式 */
    HAL_Delay(1);
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_RESET);
}

/**
  * @brief  快速测试2: USART1回显测试
  * @note   PC发送什么，开发板就返回什么（Echo）
  */
void QuickTest_USART1_RX_Echo(void)
{
    uint8_t rx_data;
    const char *prompt = "\r\n=== Echo Mode ===\r\nType something:\r\n";
    
    /* 发送提示 */
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_UART_Transmit(&huart1, (uint8_t*)prompt, strlen(prompt), 100);
    HAL_Delay(1);
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_RESET);
    
    /* 循环回显 */
    while (1) {
        /* 切换到接收模式 */
        HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_RESET);
        
        /* 接收一个字节 */
        if (HAL_UART_Receive(&huart1, &rx_data, 1, 1000) == HAL_OK) {
            /* 切换到发送模式 */
            HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_SET);
            HAL_Delay(1);
            
            /* 回显 */
            HAL_UART_Transmit(&huart1, &rx_data, 1, 100);
            
            /* 如果是回车，额外发送换行 */
            if (rx_data == '\r') {
                uint8_t newline = '\n';
                HAL_UART_Transmit(&huart1, &newline, 1, 100);
            }
            
            HAL_Delay(1);
            HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_RESET);
        }
    }
}

/**
  * @brief  快速测试3: 所有串口同时发送
  * @param  msg: 要发送的消息
  */
void QuickTest_All_UART_Send(const char *msg)
{
    uint16_t len = strlen(msg);
    
    /* USART1 */
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_UART_Transmit(&huart1, (uint8_t*)msg, len, 100);
    HAL_Delay(1);
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_RESET);
    
    /* USART2 */
    HAL_GPIO_WritePin(USART2_RE_GPIO_Port, USART2_RE_Pin, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, 100);
    HAL_Delay(1);
    HAL_GPIO_WritePin(USART2_RE_GPIO_Port, USART2_RE_Pin, GPIO_PIN_RESET);
    
    /* USART3 */
    HAL_GPIO_WritePin(USART3_RE_GPIO_Port, USART3_RE_Pin, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_UART_Transmit(&huart3, (uint8_t*)msg, len, 100);
    HAL_Delay(1);
    HAL_GPIO_WritePin(USART3_RE_GPIO_Port, USART3_RE_Pin, GPIO_PIN_RESET);
}

/**
  * @brief  使用printf重定向到USART1
  * @note   需要在main.c中注释掉原有的__io_putchar
  */
#ifdef USE_PRINTF_REDIRECT
int __io_putchar(int ch)
{
    /* RS485半双工控制 */
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    HAL_Delay(1);
    HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_RESET);
    return ch;
}
#endif

/**
  * @brief  简单演示程序
  */
void USART_QuickDemo(void)
{
    /* 演示1: 发送测试 */
    QuickTest_USART1_TX();
    
    HAL_Delay(1000);
    
    /* 演示2: 所有串口发送 */
    const char *broadcast = "Broadcast to all UARTs\r\n";
    QuickTest_All_UART_Send(broadcast);
    
    HAL_Delay(1000);
    
    /* 演示3: 进入回显模式（会阻塞） */
    // QuickTest_USART1_RX_Echo();  /* 取消注释以启用 */
}
