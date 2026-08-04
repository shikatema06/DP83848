/**
  ******************************************************************************
  * @file    usart_test.h
  * @brief   串口功能测试模块头文件
  ******************************************************************************
  */

#ifndef __USART_TEST_H
#define __USART_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/* 测试配置 */
#define USART_TEST_BUF_SIZE     256
#define USART_TEST_TIMEOUT      1000    /* ms */

/* 测试结果枚举 */
typedef enum {
    TEST_PASS = 0,
    TEST_FAIL = 1,
    TEST_RUNNING = 2
} TestResult_t;

/* 测试统计结构 */
typedef struct {
    uint32_t tx_count;          /* 发送字节数 */
    uint32_t rx_count;          /* 接收字节数 */
    uint32_t error_count;       /* 错误计数 */
    uint32_t timeout_count;     /* 超时计数 */
} USART_TestStats_t;

/* 函数声明 */
void USART_Test_Init(void);
void USART_Test_RunAll(void);

/* 各阶段测试函数 */
TestResult_t USART_Test_Phase1_Basic_TX(UART_HandleTypeDef *huart, const char *test_name);
TestResult_t USART_Test_Phase2_Basic_RX(UART_HandleTypeDef *huart, const char *test_name);
TestResult_t USART_Test_Phase3_DMA_Test(UART_HandleTypeDef *huart, const char *test_name);
TestResult_t USART_Test_Phase4_RS485_FullDuplex(UART_HandleTypeDef *huart, const char *test_name);
TestResult_t USART_Test_Phase5_Stability(UART_HandleTypeDef *huart, uint32_t duration_ms, const char *test_name);

/* 辅助函数 */
void USART_Test_PrintStats(UART_HandleTypeDef *huart);
void USART_Test_ClearStats(void);
const char* USART_Test_ResultToString(TestResult_t result);

/* 全局统计 */
extern USART_TestStats_t usart_test_stats;

#ifdef __cplusplus
}
#endif

#endif /* __USART_TEST_H */
