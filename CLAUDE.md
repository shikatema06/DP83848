# DP83848 — STM32F407 以太网数据采集系统

## 项目概述

基于 STM32F407IGT6 的嵌入式联网数据采集系统，通过 DP83848 以太网 PHY 实现网络通信，支持 TCP 命令交互和 SPI ADC 数据流传输。

### 硬件平台

- **MCU:** STM32F407IGT6 (Cortex-M4F, 168 MHz)
- **PHY:** DP83848 (MII 接口, PHY 地址 0x01)
- **Flash:** 1 MB (0x08000000–0x080FFFFF)
- **SRAM:** 128 KB + 64 KB CCMRAM
- **外部时钟:** HSE 25 MHz (PH0/PH1), LSE 32.768 kHz

### 软件栈

- **HAL:** STM32Cube FW_F4 V1.28.3
- **TCP/IP:** LwIP 2.1.2 (no RTOS, RAW API)
- **工具链:** GCC ARM Embedded, CMake + Ninja

---

## 内存布局

| 地址范围 | 大小 | 内容 |
|---|---|---|
| 0x08000000–0x0800FFFF | 64 KB | Bootloader (Sectors 0–3) |
| 0x0800FFFC | 4 B | `boot_valid` 魔数 |
| 0x08010000–0x080DFFFF | 832 KB | 应用程序 (Sectors 4–10) |
| 0x080E0000–0x080FFFFF | 128 KB | `net_config` IP 配置表 (Sector 11) |

**启动流程:** Bootloader 先运行 → 检测 `boot_valid` 标志 + 60秒 TFTP 超时 → 跳转到应用。

---

## 构建与烧录

```bash
# 配置
cmake -B build/Debug -DCMAKE_BUILD_TYPE=Debug

# 构建应用 (DP83848.elf)
cmake --build build/Debug -j 8

# 构建引导加载程序 (DP83848_BOOT.elf)
cmake --build build/Debug --target DP83848_BOOT -j 8

# 烧录应用 (STM32CubeProgrammer)
STM32_Programmer_CLI -c port=SWD -w "build/Debug/DP83848.elf" -v -rst

# 烧录引导加载程序
STM32_Programmer_CLI -c port=SWD -w "build/Debug/DP83848_BOOT.elf" -v
```

### 链接脚本

- `STM32F407XX_APP.ld` — 应用链接脚本 (origin: 0x08010000)
- `STM32F407XX_BOOT.ld` — 引导加载程序链接脚本 (origin: 0x08000000)
- `STM32F407XX_FLASH.ld` — 默认 CubeMX 单镜像 (不使用)

---

## 外设配置

| 外设 | 实例 | 用途 | 关键参数 |
|---|---|---|---|
| **ETH** | — | MII 以太网 MAC | 17 引脚, AF11, 全双工主模式 |
| **SPI** | SPI1/2/3 | ADC 数据采集 | 主模式, 8-bit, 软 NSS, 42/21/21 Mbps |
| **USART** | USART1/2/3 | RS485 调试控制台 | 115200-8-N-1, DMA RX+TX |
| **TIM4** | — | HAL 时基 (替代 SysTick) | — |
| **TIM7** | — | SPI ADC 采样定时 | 200 µs 周期 |

---

## 应用层架构

### TCP 服务器 (端口 1030)

- **文本协议:** `help`, `status`, `ver`, `uptime`, `hello`
- **二进制协议:** 8 字节帧 `0x02 0x01 CMD ... 0xDD 0xAA`
  - 支持命令: START/STOP_STREAM, READ_VERSION, LIST/SELECT/RESTORE_IP, ENTER_BOOTLOADER
- **ADC 数据流:** TIM7 触发 → 每 5 次采样平均 → 1 ms 一个数据点 → 80 点组合为 498 字节帧发送

### 网络配置 (`net_config`)

- 8 组预设 IP 地址 (172.16.x.x/24)，存储在 Flash Sector 11
- CRC32 校验保护，启动后 30 秒配置窗口
- 支持运行时切换 IP (0x0A/0x0B 命令)

### IAP 引导加载程序

- TFTP 服务器 (UDP 69) 接收新固件 WRQ
- 惰性逐扇区擦除 + CRC32 校验
- 写入 `boot_valid` 魔数后跳转

---

## 关键文件

| 文件 | 职责 |
|---|---|
| `Core/Src/main.c` | 入口, VTOR 重定向, 主循环 |
| `Core/Src/tcp_echoserver.c` | TCP 服务器 + ADC 数据流 + 命令处理 |
| `Core/Src/net_config.c` | Flash IP 配置管理 |
| `LWIP/Target/ethernetif.c` | 底层 ETH 驱动 + PHY 适配 |
| `Drivers/BSP/Components/dp83848/` | DP83848 PHY 驱动 (寄存器级) |
| `Bootloader/` | 完整 IAP 引导加载程序 |

---

## 代码规范

- 语言: C11, 裸机 (无 RTOS)
- 命名: HAL 风格 (`MX_` 前缀 = CubeMX 生成, `tcp_echoserver_` 前缀 = 用户)
- PHY 复位: PE7 低-高脉冲 (PD3 已废弃，GPIO 保留以避免 CubeMX 覆盖)
- 注释: 中英文混合，关键逻辑有中文说明
- CubeMX `USER CODE BEGIN/END` 区域受保护，用户代码放在区域内

---

## 调试与测试

- TCP 控制: `telnet 172.16.x.x 1030` — 发送 `help` 查看命令列表
- 串口控制台: USART1/2/3, 115200-8-N-1, RS485 半双工
- ADC 数据: `stream start` 命令启用 → 设备持续发送 498 字节数据帧
- 固件更新: PC 端 TFTP 客户端 → 连接 UDP 69 → 上传新固件 `.bin`