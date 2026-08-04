---
name: architecture
description: 软件架构决策、Flash 分区和关键设计模式
metadata:
  type: project
---

# 架构说明

**Flash 双分区设计:**
- Bootloader (0x08000000, 64 KB) + Application (0x08010000, 832 KB)
- boot_valid 魔数在 0x0800FFFC，引导加载程序据此决定是否跳转
- net_config (0x080E0000, 128 KB) 在 Sector 11，引导加载程序的 flash_if 有保护逻辑不擦除此区域

**应用层架构:**
- 裸机超级循环: `MX_LWIP_Process()` + `tcp_echoserver_spi_stream_poll()` + `net_config_poll()`
- TCP 使用 LwIP RAW API (回调驱动: accept/recv/sent/error/poll)
- SPI ADC 流使用双缓冲 (ping-pong) 避免 ISR/前台竞争
- 每 5 次 ADC 采样平均为 1 个数据点 (1 ms)，80 点组成一帧 (498 字节)

**IAP 引导加载程序:**
- 启动后检测 boot_valid: 有效则延时 60 秒等待 TFTP 连接，超时后跳转应用
- 无效则直接进入 TFTP 监听模式
- 使用惰性扇区擦除 (仅首次写入时)，CRC32 校验通过后才标记 boot_valid
- 应用可通过 ENTER_BOOTLOADER 命令 (0x08) 软重启进入引导加载程序

**PHY 驱动设计:**
- 依赖注入模式: `dp83848_IOCtx_t` 包含 Init/DeInit/ReadReg/WriteReg/GetTick 函数指针
- ethernetif.c 通过 HAL MDIO 函数实现具体 IO

**如何应用:** 新功能应遵循环回架构。需注意引导加载程序和应用共享同一 IP 配置 (net_config)。TCP 端口 1030 的二进制协议帧格式不可改变以免破坏兼容性。