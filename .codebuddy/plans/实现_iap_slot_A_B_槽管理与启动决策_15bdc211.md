---
name: 实现 iap_slot A/B 槽管理与启动决策
overview: 实现 Bootloader/iap_slot.h 和 iap_slot.c，定义槽状态与双阶段提交状态机；同步 IAP_Flash_Layout.md；修复 flash_if 遗留编译问题；将 bootloader_main.c、tftp_server.c、tcp_echoserver.c 的旧 BOOT_VALID 逻辑迁移到 iap_slot；最后编译验证。
todos:
  - id: sync-flash-layout
    content: 同步 IAP_Flash_Layout.md 为 48KB Bootloader + sector 3 标志区单一事实源
    status: completed
  - id: fix-flash-if-compile
    content: 修复 flash_if.c 中 USER_FLASH_END_ADDRESS/__IO 编译问题
    status: completed
    dependencies:
      - sync-flash-layout
  - id: define-iap-slot
    content: 创建 iap_slot.h：定义槽状态枚举、boot_slot_t 结构体与 API
    status: completed
    dependencies:
      - sync-flash-layout
  - id: implement-iap-slot
    content: 实现 iap_slot.c：槽读写、CRC、选择、双阶段提交与回滚
    status: completed
    dependencies:
      - define-iap-slot
      - fix-flash-if-compile
  - id: migrate-boot-decision
    content: 重构 bootloader_main.c 用 iap_slot 替换 boot_valid 启动决策
    status: completed
    dependencies:
      - implement-iap-slot
  - id: migrate-tftp-slot
    content: 改造 tftp_server.c 写入备用槽并调用 iap_slot 提交
    status: completed
    dependencies:
      - implement-iap-slot
  - id: migrate-app-entry
    content: 更新 tcp_echoserver.c CMD_ENTER_BOOTLOADER 使用新 boot_request API
    status: completed
    dependencies:
      - implement-iap-slot
  - id: build-and-verify
    content: 使用 [subagent:code-explorer] 扫描旧符号残留并触发 Bootloader 编译验证
    status: completed
    dependencies:
      - migrate-boot-decision
      - migrate-tftp-slot
      - migrate-app-entry
---

## Product Overview

继续推进 DP83848 项目的 IAP 在线升级重写工作，完成 `iap_slot` 槽管理模块，实现 A/B 双备份升级的核心状态机与回滚能力，并把 Bootloader/App 侧的旧版 `boot_valid` 逻辑迁移到新机制。

## Core Features

- 定义并固化槽状态枚举与 `boot_slot_t` 结构体
- 实现槽选择、镜像 CRC 校验、双阶段提交、异常回滚逻辑
- 将 Bootloader 启动决策从旧的 `boot_valid` 迁移到新的 `slot_state`
- 将 TFTP 写入目标从固定 Slot A 改为动态选择备用槽
- 同步 `IAP_Flash_Layout.md` 为单一事实源
- 修复昨日遗留的 `USER_FLASH_END_ADDRESS` / `__IO` 编译问题

## Tech Stack

- C (ARM Cortex-M4, STM32F407VG)
- STM32Cube HAL（未启用 `HAL_CRC_MODULE_ENABLED`）
- CMake 构建系统
- 现有 LwIP TFTP server 作为固件接收通道

## Implementation Approach

在 `Bootloader/` 目录新建 `iap_slot` 模块，负责：

1. 在 `BOOT_SLOT_ADDR`（`0x0800FFFC`，sector 3 末尾）读写 `boot_slot_t` 结构体。
2. 维护槽状态（EMPTY / VALID / NEW / INVALID / OLD）与提交状态（COMMIT_IDLE / ERASING / PENDING / DONE）。
3. 使用与 `net_config.c`、`tftp_server.c` 一致的 Ethernet CRC32 多项式 `0xEDB88320` 对槽内固件做校验。
4. 启动时根据状态选择可启动槽，升级时把新固件写入备用槽，提交失败或新固件异常时回滚到旧槽。

关键决策：

- 继续使用软件 CRC32，因为 `stm32f4xx_hal_conf.h` 中未启用 `HAL_CRC_MODULE_ENABLED`，且避免改动 CubeMX 生成配置。
- `iap_slot` 放在 Bootloader 目录，避免把 Flash 决策暴露给 App。
- 采用双阶段提交（ERASING → PENDING → DONE），任何阶段掉电都可恢复。
- 保留向后兼容：当 A/B 槽均无效但 `0x08000000` 存在旧 App 时，Bootloader 仍可跳转（参考 `IAP_Flash_Layout.md` §11.2）。

性能与可靠性：

- CRC32 为 O(n) 扫描，仅在启动和传输完成时执行，可接受。
- `boot_slot_t` 体积小，集中存放在 sector 3 末尾，擦写次数可控。
- 状态转换尽量幂等、单调，确保复位后可重入。

## Implementation Notes

- 复用 `flash_if.c` 的 `FLASH_If_Erase_Sector()` / `FLASH_If_Write()`，不再新增 Flash 驱动。
- 将 `FLASH_If_Write()` 中的 `USER_FLASH_END_ADDRESS` 修正为 `USER_FLASH_END_ADDR`。
- 确认 `__IO` 由 `stm32f4xx_hal.h` 或 CMSIS 提供；否则替换为 `volatile`。
- 在 `Bootloader/CMakeLists.txt` 中加入 `iap_slot.c`。
- TFTP server：WRQ 开始时调用 `iap_slot_select_inactive()` 获取备用槽基址；传输结束后调用 `iap_slot_commit_finish(image_size, expected_crc)` 完成提交。
- `bootloader_main.c`：调用 `iap_slot_boot_select(&app_addr)` 决定是进入升级等待还是跳转 App。

## Architecture Design

改动集中在 Bootloader 层，结构简洁：

- `flash_if`：提供擦/写原语与地址保护。
- `iap_slot`：基于 `flash_if` 管理 A/B 槽状态与提交。
- `tftp_server`：接收数据，把槽选择与提交委托给 `iap_slot`。
- `bootloader_main`：通过 `iap_slot` 决定启动行为。

## Directory Structure

```
c:/ST_project/DP83848/
├── Bootloader/
│   ├── iap_slot.h              [NEW] 槽状态枚举、boot_slot_t 结构体、公共 API
│   ├── iap_slot.c              [NEW] 槽读写、CRC、选择、提交、回滚实现
│   ├── flash_if.h              [MODIFY] 宏命名一致性、必要前置声明
│   ├── flash_if.c              [MODIFY] 修复 USER_FLASH_END_ADDRESS/__IO 编译问题
│   ├── bootloader_main.c       [MODIFY] 用 iap_slot 替换 boot_valid 启动决策
│   ├── tftp_server.c           [MODIFY] 写入备用槽并调用 iap_slot 提交
│   └── CMakeLists.txt          [MODIFY] 添加 iap_slot.c 到目标源
├── Core/Src/
│   └── tcp_echoserver.c        [MODIFY] CMD_ENTER_BOOTLOADER 使用 FLASH_If_Write_Boot_Request(UPGRADE)
└── IAP_Flash_Layout.md         [MODIFY] 同步为 48KB Bootloader + sector 3 标志区
```

## Key Code Structures

```c
/* iap_slot.h */
typedef enum {
    SLOT_STATE_EMPTY    = 0x00,
    SLOT_STATE_VALID    = 0x01,
    SLOT_STATE_NEW      = 0x02,
    SLOT_STATE_INVALID  = 0x03,
    SLOT_STATE_OLD      = 0x04,
} slot_state_t;

typedef enum {
    COMMIT_STATE_IDLE     = 0x00,
    COMMIT_STATE_ERASING  = 0x01,
    COMMIT_STATE_PENDING  = 0x02,
    COMMIT_STATE_DONE     = 0x03,
} commit_state_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;              /* SLOT_MAGIC */
    uint8_t  active_slot;        /* 0=Slot A, 1=Slot B */
    uint8_t  commit_state;       /* commit_state_t */
    uint16_t reserved;
    uint32_t slot_crc[2];        /* Slot A/B 固件 CRC32 */
    uint32_t slot_size[2];       /* Slot A/B 固件字节数 */
    uint32_t sequence;           /* 单调递增版本号 */
} boot_slot_t;
```

## Agent Extensions

### SubAgent

- **code-explorer**
- Purpose: 在修改前/后快速扫描 `Bootloader/` 与 `Core/` 目录中仍引用旧 `BOOT_VALID_ADDR`、`BOOT_VALID_MAGIC`、`USER_FLASH_FIRST_PAGE_ADDRESS`（旧命名）或 `0x0800FFFC` 硬编码的地方，确保迁移无遗漏。
- Expected outcome: 输出一份包含文件路径、行号、上下文片段的检查清单，用于验证迁移完成度。