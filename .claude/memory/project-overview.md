---
name: project-overview
description: STM32F407 + DP83848 以太网数据采集系统总体描述
metadata:
  type: project
---

# DP83848 项目概述

基于 STM32F407IGT6 (Cortex-M4F, 168 MHz) 和 DP83848 PHY 的嵌入式联网数据采集系统。

**核心功能:**
- 通过 LwIP 2.1.2 协议栈实现裸机 TCP/IP 以太网通信 (MII 模式)
- TCP 端口 1030 上的命令服务器 (文本 + 二进制协议)
- 三路 SPI ADC 数据采集 (TIM7 200µs 触发，双缓冲流传输)
- 8 组预设 IP 配置表，存储在 Flash Sector 11 带 CRC32 校验
- IAP 引导加载程序 (TFTP/UDP 69 固件更新)

**构建:** CMake + Ninja, GCC ARM, 两个目标: DP83848 (应用) 和 DP83848_BOOT (引导加载程序)

**如何应用:** 参考 [[hardware-config]] 了解引脚和外设，参考 [[build-and-flash]] 了解构建和烧录命令。