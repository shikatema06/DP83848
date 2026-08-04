---
name: flash-layout
description: Flash 分区细节和保护区域
metadata:
  type: reference
---

# Flash 布局

| 扇区 | 地址 | 大小 | 用途 |
|---|---|---|---|
| 0 | 0x08000000 | 16 KB | Bootloader |
| 1 | 0x08004000 | 16 KB | Bootloader |
| 2 | 0x08008000 | 16 KB | Bootloader |
| 3 | 0x0800C000 | 16 KB | Bootloader + boot_valid@0x0800FFFC |
| 4-10 | 0x08010000–0x080DFFFF | 832 KB | 应用程序 |
| 11 | 0x080E0000–0x080FFFFF | 128 KB | net_config IP 配置表 (受保护) |

**关键约束:**
- 应用程序起始于 0x08010000 (VTOR 重定向到此地址)
- boot_valid 魔数位于 0x0800FFFC，写入 0xB0B0B0B0 表示有效
- 引导加载程序的 flash_if.c 在擦除操作中保护 Sector 11
- net_config 使用 CRC32 校验所有 8 组 IP 配置的完整性

**如何应用:** 所有链接脚本 (`STM32F407XX_APP.ld`/`_BOOT.ld`) 必须与上述布局一致。切勿擦除 0x080E0000 以后的区域除非同时重写 net_config。