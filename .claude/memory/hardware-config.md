---
name: hardware-config
description: STM32F407IGT6 引脚配置和外设参数
metadata:
  type: reference
---

# 硬件配置

**MCU:** STM32F407IGT6, LQFP176, 168 MHz (PLL: 25 MHz HSE → /25 × 336 /2)
**PHY:** DP83848, MII 接口, PHY 地址 0x01 (定义在 stm32f4xx_hal_conf.h 中)

**时钟树:**
- SYSCLK = 168 MHz, HCLK = 168 MHz, APB1 = 42 MHz, APB2 = 84 MHz
- ETH 时钟 = 168 MHz
- MCO1 (PA8) = 25 MHz HSE → DP83848 时钟输入
- Flash 等待周期: 5 WS (FLASH_LATENCY_5)

**关键引脚:**
- PE7: PHY 复位 (低-高脉冲驱动，PD3 已废弃但 GPIO 保留)
- PB12: MII_INT (PHY 中断)
- PA8: MCO1 (25 MHz 时钟输出给 PHY)
- SPI1: PB3(SCK), PB4(MISO), PB5(MOSI), PB6(NSS)
- USART1/2/3: 全部使用 DMA RX+TX, 带 RS485 RE/DE 控制

**HAL 时基:** TIM4 (替代 SysTick)，因为以太网中断可能占用 SysTick

**如何应用:** 添加新外设时确认引脚无冲突。PHY 复位使用 PE7 (非 PD3)。SPI 使用软 NSS。