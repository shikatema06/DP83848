/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define MCU_HDC_Pin GPIO_PIN_2
#define MCU_HDC_GPIO_Port GPIOF
#define MII_INT_Pin GPIO_PIN_3
#define MII_INT_GPIO_Port GPIOF
#define BOOT1_Pin GPIO_PIN_2
#define BOOT1_GPIO_Port GPIOB
#define RESET_PHY_Pin GPIO_PIN_12
#define RESET_PHY_GPIO_Port GPIOF
#define MCU_IN1_Pin GPIO_PIN_15
#define MCU_IN1_GPIO_Port GPIOF
#define MCU_OUT1_Pin GPIO_PIN_0
#define MCU_OUT1_GPIO_Port GPIOG
#define USART3_RE_Pin GPIO_PIN_15
#define USART3_RE_GPIO_Port GPIOE
#define DRDY_AD7767_2_Pin GPIO_PIN_12
#define DRDY_AD7767_2_GPIO_Port GPIOH
#define SPI2_NSS_Pin GPIO_PIN_8
#define SPI2_NSS_GPIO_Port GPIOD
#define SYNC_PD_AD7767_2_Pin GPIO_PIN_9
#define SYNC_PD_AD7767_2_GPIO_Port GPIOD
#define USART1_RE_Pin GPIO_PIN_11
#define USART1_RE_GPIO_Port GPIOA
#define DRDY_AD7767_3_Pin GPIO_PIN_15
#define DRDY_AD7767_3_GPIO_Port GPIOA
#define SPI3_NSS_Pin GPIO_PIN_0
#define SPI3_NSS_GPIO_Port GPIOD
#define SYNC_PD_AD7767_3_Pin GPIO_PIN_1
#define SYNC_PD_AD7767_3_GPIO_Port GPIOD
#define USART2_RE_Pin GPIO_PIN_7
#define USART2_RE_GPIO_Port GPIOD
#define DRDY_AD7767_1_Pin GPIO_PIN_15
#define DRDY_AD7767_1_GPIO_Port GPIOG
#define SPI1_NSS_Pin GPIO_PIN_6
#define SPI1_NSS_GPIO_Port GPIOB
#define SYNC_PD_AD7767_1_Pin GPIO_PIN_7
#define SYNC_PD_AD7767_1_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* ==========================================================================
 * AD7767 三探头高精度版 —— 新增引脚（T1.1，2026-10-09）
 * 依据：gaojingdu_santantou_STM_GPIO.xlsx + 三探头高精度版本_2026.8.17.pdf
 *
 * 说明：这些宏放在 USER CODE 区，CubeMX 重新生成代码不会覆盖。
 *       对应的 GPIO 初始化在 Core/Src/bsp_pins.c 的 BSP_Pins_Init() 中，
 *       不在 CubeMX 生成的 MX_GPIO_Init() 里。
 * ========================================================================== */

/* --- AD7767 数据就绪 DRDY（输入，低有效）---------------------------------- */
#define DRDY_AD7767_1_Pin           GPIO_PIN_15   /* PG15  pin160 */
#define DRDY_AD7767_1_GPIO_Port     GPIOG
#define DRDY_AD7767_2_Pin           GPIO_PIN_12   /* PH12  pin89  */
#define DRDY_AD7767_2_GPIO_Port     GPIOH
#define DRDY_AD7767_3_Pin           GPIO_PIN_15   /* PA15  pin138 (原 JTDI，重配为 GPIO 即释放 JTAG) */
#define DRDY_AD7767_3_GPIO_Port     GPIOA

/* --- AD7767 同步/掉电 SYNC-PD（输出，高=工作，低=掉电）--------------------- */
/* 网络名原为 SYNC/PD_AD7767_n；'/' 不能出现在 C 宏里，故统一写 SYNC_PD_ */
#define SYNC_PD_AD7767_1_Pin        GPIO_PIN_7    /* PB7   pin165 */
#define SYNC_PD_AD7767_1_GPIO_Port  GPIOB
#define SYNC_PD_AD7767_2_Pin        GPIO_PIN_9    /* PD9   pin97  */
#define SYNC_PD_AD7767_2_GPIO_Port  GPIOD
#define SYNC_PD_AD7767_3_Pin        GPIO_PIN_1    /* PD1   pin143 */
#define SYNC_PD_AD7767_3_GPIO_Port  GPIOD

/* --- DP83848 PHY 中断 MII_INT（输入，开漏低有效）--------------------------
 * 2026-10-09: 由 PB12 迁至 PF3（xlsx pin 19）。PB12 在本版硬件上悬空。 */
#define MII_INT_Pin                 GPIO_PIN_3    /* PF3   pin19  */
#define MII_INT_GPIO_Port           GPIOF

/* --- HDC1010 温湿度 DRDYN（输出）----------------------------------------- */
#define MCU_HDC_Pin                 GPIO_PIN_2    /* PF2   pin18  */
#define MCU_HDC_GPIO_Port           GPIOF

/* --- 霍尔接口（经 ACPL-217 光耦隔离）------------------------------------- */
#define MCU_IN1_Pin                 GPIO_PIN_15   /* PF15  pin65  输入 */
#define MCU_IN1_GPIO_Port           GPIOF
#define MCU_OUT1_Pin                GPIO_PIN_0    /* PG0   pin66  输出 */
#define MCU_OUT1_GPIO_Port          GPIOG

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
