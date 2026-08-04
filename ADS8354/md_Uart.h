#ifndef MD_UART_H
#define MD_UART_H

#include "stm32f4xx_hal.h"

//int16_t  Adc_avg[3] = {0} ;
//int8_t  Adcavg_en[3] = {0} ;

#define EnableLED1Pin   GPIO_PIN_8
#define EnableLED2Pin   GPIO_PIN_7
#define  LED1_ON    HAL_GPIO_WritePin(GPIOE, EnableLED1Pin, GPIO_PIN_RESET)
#define  LED1_OFF   HAL_GPIO_WritePin(GPIOE, EnableLED1Pin, GPIO_PIN_SET)

#define  LED2_ON    HAL_GPIO_WritePin(GPIOE, EnableLED2Pin, GPIO_PIN_RESET)
#define  LED2_OFF   HAL_GPIO_WritePin(GPIOE, EnableLED2Pin, GPIO_PIN_SET)

#define EnableUart0Pin   GPIO_PIN_11
#define  UART_RevEnable    HAL_GPIO_WritePin(GPIOA, EnableUart0Pin, GPIO_PIN_RESET)//接收使能
#define  UART_SendEnable   HAL_GPIO_WritePin(GPIOA, EnableUart0Pin, GPIO_PIN_SET)//发送使能

void UART_RxIdleCallback(UART_HandleTypeDef *huart);

void md_Uart1SendcmpCallback();

void md_Uart2SendcmpCallback();

void md_Uart3SendcmpCallback();

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) ;
#endif
