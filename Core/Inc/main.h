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



/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
