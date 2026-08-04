/**
  ******************************************************************************
  * @file    usart_quick_test.h
  * @brief   快速串口测试示例头文件
  ******************************************************************************
  */

#ifndef __USART_QUICK_TEST_H
#define __USART_QUICK_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 函数声明 */
void QuickTest_USART1_TX(void);
void QuickTest_USART1_RX_Echo(void);
void QuickTest_All_UART_Send(const char *msg);
void USART_QuickDemo(void);

#ifdef __cplusplus
}
#endif

#endif /* __USART_QUICK_TEST_H */
