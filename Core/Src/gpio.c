/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOI_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(MCU_HDC_GPIO_Port, MCU_HDC_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RESET_PHY_GPIO_Port, RESET_PHY_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(MCU_OUT1_GPIO_Port, MCU_OUT1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_7|GPIO_PIN_8|USART3_RE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, SPI2_NSS_Pin|SYNC_PD_AD7767_2_Pin|SPI3_NSS_Pin|SYNC_PD_AD7767_3_Pin
                          |USART2_RE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(USART1_RE_GPIO_Port, USART1_RE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, SPI1_NSS_Pin|SYNC_PD_AD7767_1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : MCU_HDC_Pin RESET_PHY_Pin */
  GPIO_InitStruct.Pin = MCU_HDC_Pin|RESET_PHY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /*Configure GPIO pins : MII_INT_Pin MCU_IN1_Pin */
  GPIO_InitStruct.Pin = MII_INT_Pin|MCU_IN1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /*Configure GPIO pin : BOOT1_Pin */
  GPIO_InitStruct.Pin = BOOT1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BOOT1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MCU_OUT1_Pin */
  GPIO_InitStruct.Pin = MCU_OUT1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MCU_OUT1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PE7 PE8 USART3_RE_Pin */
  GPIO_InitStruct.Pin = GPIO_PIN_7|GPIO_PIN_8|USART3_RE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : DRDY_AD7767_2_Pin */
  GPIO_InitStruct.Pin = DRDY_AD7767_2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(DRDY_AD7767_2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : SPI2_NSS_Pin SPI3_NSS_Pin USART2_RE_Pin */
  GPIO_InitStruct.Pin = SPI2_NSS_Pin|SPI3_NSS_Pin|USART2_RE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : SYNC_PD_AD7767_2_Pin SYNC_PD_AD7767_3_Pin */
  GPIO_InitStruct.Pin = SYNC_PD_AD7767_2_Pin|SYNC_PD_AD7767_3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : USART1_RE_Pin */
  GPIO_InitStruct.Pin = USART1_RE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USART1_RE_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : DRDY_AD7767_3_Pin */
  GPIO_InitStruct.Pin = DRDY_AD7767_3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(DRDY_AD7767_3_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : DRDY_AD7767_1_Pin */
  GPIO_InitStruct.Pin = DRDY_AD7767_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(DRDY_AD7767_1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI1_NSS_Pin */
  GPIO_InitStruct.Pin = SPI1_NSS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SPI1_NSS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SYNC_PD_AD7767_1_Pin */
  GPIO_InitStruct.Pin = SYNC_PD_AD7767_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SYNC_PD_AD7767_1_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */
/**
  * @brief  DP83848 PHY 硬件复位（PF12 LOW→HIGH）
  * @note   必须在 ETH_MDIO 通信之前调用。
  *         2026-10-09: 复位脚由 PF11 迁至 PF12（依据 gaojingdu_santantou_STM_GPIO.xlsx pin 60）
  */
void MX_GPIO_PhyReset(void)
{
  HAL_GPIO_WritePin(RESET_PHY_GPIO_Port, RESET_PHY_Pin, GPIO_PIN_RESET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(RESET_PHY_GPIO_Port, RESET_PHY_Pin, GPIO_PIN_SET);
  /* 复位释放后等待 50ms，保证 DP83848 内部 PLL/寄存器就绪后再做 MDIO 访问 */
  HAL_Delay(50);
}
/* USER CODE END 2 */
