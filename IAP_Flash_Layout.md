# IAP Flash 布局图（单一事实源）

> 本文档是所有 Flash 地址宏、链接脚本配置、擦写保护范围的**唯一权威来源**。
> 代码中的地址宏必须与此文档一致，修改分区时必须先更新本文档。

## 1. STM32F407VG Flash 总览

- 芯片：STM32F407VG
- Flash 容量：1MB（单 bank，无硬件双 bank 支持）
- Sector 数量：12 个（sector 0-11）
- Sector 布局：前 4 个 sector 各 16KB，**1 个 sector 4 为 64KB**，后 7 个 sector 各 128KB

> ⚠️ 修订（2026-08-06）：Bootloader 占用 **sector 0-2（48KB）**，sector 3（16KB）改为**槽管理标志区**，
> 不再属于 Bootloader 代码区。下文分区图与宏定义均以 48KB Bootloader + sector 3 标志区为准。

## 2. Sector 详细地址表

```
Sector 0:  0x08000000 - 0x08003FFF  ( 16KB)  ← Bootloader
Sector 1:  0x08004000 - 0x08007FFF  ( 16KB)  ← Bootloader
Sector 2:  0x08008000 - 0x0800BFFF  ( 16KB)  ← Bootloader
Sector 3:  0x0800C000 - 0x0800FFFF  ( 16KB)  ← 槽管理标志区（boot_request / boot_slot，禁止当 Bootloader 代码）
------------------------------------------------------ 0x08010000
Sector 4:  0x08010000 - 0x0801FFFF  ( 64KB)  ← Slot A
Sector 5:  0x08020000 - 0x0803FFFF  (128KB)  ← Slot A
Sector 6:  0x08040000 - 0x0805FFFF  (128KB)  ← Slot A
------------------------------------------------------ 0x08060000 (Slot A 结束在 0x0805FFFF)
Sector 7:  0x08060000 - 0x0807FFFF  (128KB)  ← Slot B
Sector 8:  0x08080000 - 0x0809FFFF  (128KB)  ← Slot B
Sector 9:  0x080A0000 - 0x080BFFFF  (128KB)  ← Slot B
------------------------------------------------------ 0x080C0000 (Slot B 结束在 0x080BFFFF)
Sector 10: 0x080C0000 - 0x080DFFFF  (128KB)  ← 预留（未来扩展/日志区）
------------------------------------------------------ 0x080E0000
Sector 11: 0x080E0000 - 0x080FFFFF  (128KB)  ← net_config（IP配置，禁止擦除）
```

## 3. IAP 分区图

```
0x08000000 ┌─────────────────────────────────┐
           │                                 │
           │       Bootloader (48KB)         │  sector 0-2
           │       代码 + 中断向量表          │
           │                                 │
0x0800C000 ├─────────────────────────────────┤  ← 槽管理标志区（sector 3）
           │   （空闲/预留，不存放 Bootloader）│
0x0800FFF8 ├─────────────────────────────────┤  ← 标志区末尾
           │ boot_request    (4B)            │  0x0800FFF8  升级请求标志
0x0800FFFC │ boot_slot_t     (N B)           │  0x0800FFFC  槽管理结构体
0x08010000 ┼─────────────────────────────────┤  ← App 区起始
           │                                 │
           │       Slot A (320KB)            │  sector 4-6
           │       镜像 A（运行槽/备用槽）    │  0x08010000 - 0x0805FFFF
           │                                 │
0x08060000 ├─────────────────────────────────┤
           │                                 │
           │       Slot B (384KB)            │  sector 7-9
           │       镜像 B（运行槽/备用槽）    │  0x08060000 - 0x080BFFFF
           │                                 │
0x080C0000 ├─────────────────────────────────┤  ← 预留区
           │       Reserved (128KB)          │  sector 10
           │       （未来扩展/日志区）        │  0x080C0000 - 0x080DFFFF
0x080E0000 ├─────────────────────────────────┤  ← 禁止擦除线
           │                                 │
           │       net_config (128KB)        │  sector 11
           │       IP配置/网络参数           │  0x080E0000 - 0x080FFFFF
           │                                 │
0x080FFFFF └─────────────────────────────────┘
```

## 4. 关键地址宏定义（对应 flash_if.h）

```c
/* Bootloader 区 */
#define BOOTLOADER_BASE_ADDR        0x08000000U
#define BOOTLOADER_SIZE             (48U * 1024U)      /* 48KB, sector 0-2 */

/* 槽管理标志区（sector 3 末尾） */
#define BOOT_REQUEST_ADDR           0x0800FFF8U        /* 升级请求标志 */
#define BOOT_REQUEST_NORMAL         0xFFFFFFFFU        /* 正常启动（Flash 擦除后默认值） */
#define BOOT_REQUEST_UPGRADE        0x55AA55AAU        /* 请求留在 Bootloader 升级 */

#define BOOT_SLOT_ADDR              0x0800FFFCU        /* 槽管理结构体（原 boot_valid 位置） */

/* App 区（Slot A + Slot B + Reserved） */
#define USER_FLASH_FIRST_PAGE_ADDR  0x08010000U        /* App 区起始 */
#define USER_FLASH_END_ADDR         0x080E0000U        /* App 区结束（net_config 起始） */

/* Slot A */
#define SLOT_A_START_ADDR           0x08010000U
#define SLOT_A_END_ADDR             0x08060000U        /* 不含此地址 */
#define SLOT_A_SIZE                 (SLOT_A_END_ADDR - SLOT_A_START_ADDR)  /* 320KB (sector 4,5,6) */

/* Slot B */
#define SLOT_B_START_ADDR           0x08060000U
#define SLOT_B_END_ADDR             0x080C0000U        /* 不含此地址 */
#define SLOT_B_SIZE                 (SLOT_B_END_ADDR - SLOT_B_START_ADDR)  /* 384KB */

/* 预留区 */
#define RESERVED_START_ADDR         0x080C0000U
#define RESERVED_END_ADDR           0x080E0000U

/* net_config 区（禁止擦除） */
#define NET_CONFIG_START_ADDR       0x080E0000U
#define NET_CONFIG_END_ADDR         0x08100000U        /* Flash 末尾 */
```

## 5. 链接脚本配置

### Bootloader 链接脚本（STM32F407XX_BOOT.ld）— 保持不变

```ld
FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 48K    /* sector 0-2 */
```

### App 链接脚本（STM32F407XX_APP.ld）— 需修正

**当前（有 bug）**：
```ld
FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1024K   /* 与 Bootloader 重叠！ */
```

**修正为（默认编译到 Slot A）**：
```ld
FLASH (rx) : ORIGIN = 0x08010000, LENGTH = 320K    /* Slot A, sector 4-6 */
```

> 注意：LENGTH 设为 384K 而非 384K 的精确值（0x60000），是为了让链接器
> 在 App 体积超过单槽容量时能报错，防止写穿到下一个槽。
>
> 编译到 Slot B 时改为 ORIGIN = 0x08060000。可用 CMake 宏切换。

## 6. 擦写保护规则

| 区域 | 允许擦除 | 允许写入 | 说明 |
|------|---------|---------|------|
| Bootloader (sector 0-2) | ❌ | ❌ | 运行中不能自擦 |
| 槽管理标志区 (sector 3, 0x0800C000-0x0800FFFF) | ⚠️ 仅擦 sector 3 | ✅ 仅标志位 | 存 boot_request / boot_slot，需先擦整个 sector 3 再写，谨慎操作 |
| Slot A (sector 4-6) | ✅ | ✅ | 升级时擦写备用槽 |
| Slot B (sector 7-9) | ✅ | ✅ | 升级时擦写备用槽 |
| Reserved (sector 10) | ⚠️ 暂不 | ⚠️ 暂不 | 预留，当前 IAP 不触碰 |
| net_config (sector 11) | ❌ 永远 | ❌ 永远 | IP 配置，擦除会导致网络失效 |

**flash_if.c 中的保护实现**：
```c
/* 所有擦写操作的 guard 条件 */
if (addr >= 0x080E0000U) return ERROR;  /* 禁止触碰 sector 11 */
if (addr < 0x08010000U)  return ERROR;  /* 禁止触碰 Bootloader 区（sector 0-2） */
```

## 7. 槽状态机

```
                    ┌──────────────────────────────────────────┐
                    │                                          │
                    ▼                                          │
┌───────┐  首次烧录   ┌───────┐  备用槽收完+CRC通过  ┌─────┐    │
│ EMPTY │ ─────────► │ VALID │ ──────────────────► │ NEW │    │
└───────┘            └───────┘                     └─────┘    │
                         ▲                           │        │
                         │ 提交翻转 active_slot      │        │
                         │ (NEW→VALID, 旧VALID→OLD)  │        │
                         │                           │        │
                         │         CRC失败/断电      │        │
                         │           ┌───────────────┘        │
                         │           ▼                        │
                         │      ┌──────────┐                  │
                         │      │ INVALID  │                  │
                         │      └──────────┘                  │
                         │           │                        │
                         │  重新升级 │                        │
                         │           ▼                        │
                         │      ┌──────────┐                  │
                         │      │  EMPTY   │                  │
                         │      └──────────┘                  │
                         │                                       │
                         │  回滚(新槽无效)                       │
                    ┌────┴─────┐                                │
                    │   OLD    │ ───────────────────────────────┘
                    └──────────┘
```

### 状态说明

| 状态 | 值 | 含义 |
|------|-----|------|
| EMPTY | 0x00 | 从未写入，Flash 擦除后默认值 |
| VALID | 0x01 | CRC 通过，可启动 |
| NEW | 0x02 | 刚写完待提交（临时状态） |
| INVALID | 0x03 | CRC 失败或损坏，不可启动 |
| OLD | 0x04 | 被新版本取代，可回滚 |

### 双阶段提交状态

| 状态 | 值 | 含义 |
|------|-----|------|
| COMMIT_IDLE | 0x00 | 空闲，无升级进行 |
| COMMIT_ERASING | 0x01 | 正在擦除备用槽 |
| COMMIT_PENDING | 0x02 | 数据写完+CRC通过，待翻转 active_slot |
| COMMIT_DONE | 0x03 | 提交完成，可复位 |

## 8. 升级流程时序

```
时间轴 ──────────────────────────────────────────────────────►

[App 运行中]
    │
    │ 收到 CMD_ENTER_BOOTLOADER
    │
    ▼
[App 写 boot_request = BOOT_REQUEST_UPGRADE 到 0x0800FFF8]
    │
    ▼
[NVIC_SystemReset()]  ← 硬件复位，状态100%干净
    │
    ▼
[Bootloader 启动]
    │
    ├─ 读 boot_request == BOOT_REQUEST_UPGRADE ?
    │     ├─ 是 → 初始化网络栈 → 等待固件
    │     └─ 否 → 10s 超时跳 App
    │
    ▼
[收到固件流（HTTP/TFTP）]
    │
    ├─ iap_rx_begin(备用槽地址, 总长度)
    │     └─ 写 commit_state = ERASING
    │     └─ 擦除备用槽
    │
    ├─ iap_rx_write(offset, data, len)  × N 次
    │     └─ 逐块写入 Flash
    │     └─ CRC32 累加
    │
    ├─ iap_rx_end()
    │     └─ CRC32 比对
    │     └─ 通过 → 写 commit_state = COMMIT_PENDING
    │     └─ 翻转 active_slot，新槽 VALID，旧槽 OLD
    │     └─ 写 commit_state = COMMIT_DONE
    │
    ▼
[清除 boot_request = NORMAL]
    │
    ▼
[NVIC_SystemReset()]  ← 硬件复位
    │
    ▼
[Bootloader 启动]
    │
    ├─ 读 boot_request == BOOT_REQUEST_NORMAL
    ├─ 读 active_slot → 新槽
    ├─ 校验新槽 CRC → 通过
    ├─ 10s 超时
    │
    ▼
[跳转新槽 App]  ← 升级完成！
```

## 9. 回滚场景

### 场景 A：升级中断电

```
状态：commit_state == ERASING（擦除中断电）
重启后：备用槽未擦干净 → 标记 INVALID → 继续用旧 VALID 槽

状态：commit_state == COMMIT_PENDING（提交中断电）
重启后：数据已写完但 active_slot 未翻转 → 继续用旧 VALID 槽
        → 备用槽标记为 NEW（可下次重试）
```

### 场景 B：新固件 CRC 失败

```
状态：iap_rx_end() CRC 比对失败
处理：备用槽标记 INVALID → 保留旧 VALID 槽
      → 回到升级等待，可重新上传
```

### 场景 C：新固件启动后崩溃

```
状态：App 启动后 HardFault / 看门狗超时
处理：App 在崩溃前（或 Bootloader 检测到连续崩溃次数超限）
      → 新槽标记 INVALID → 旧 OLD 槽恢复为 VALID
      → 复位后 Bootloader 跳旧槽
```

## 10. 旧版变砖根因对照表

| 旧版 TFTP_IAP 的 bug | 本文档的防护措施 |
|---------------------|----------------|
| 链接脚本占满 1MB，与 Bootloader 重叠 | §5 链接脚本 ORIGIN 对齐槽地址，LENGTH 限制单槽 |
| 擦除硬编码 sector 5-11，越界擦 net_config | §6 擦写保护规则，flash_if guard `addr >= 0x080E0000` |
| 擦除参数被忽略，起始 sector 写死 | flash_if 用 `FLASH_Get_Sector(addr)` 动态计算 |
| 软跳转状态污染 | §8 所有切换走 NVIC_SystemReset() 硬复位 |
| 跳转前无 CRC 校验 | §7 槽状态机，必须 CRC 通过才 VALID |
| 无 A/B 备份，升级失败即砖 | §3 A/B 双槽，升级写备用槽，运行槽不动 |

## 11. 安全验证与救砖指南（重要）

> 目标：**保证"App 在 0x08000000 直接跑"的安全基线永不丢失**，
> 任何 IAP 改动都不影响回归开发，且具备救砖能力。

### 11.1 安全基线

- 开发调试的主力方式仍是：**App 直接烧到 0x08000000 运行**（无需 Bootloader）
- 做法：把 `STM32F407XX_APP.ld` 的 FLASH ORIGIN 改回 `0x08000000` 即可回归
- 这个基线是"保底"，任何时候出问题，回到这里就能恢复

### 11.2 Bootloader 兼容旧 App 位置（设计决策）

Bootloader 启动决策需支持**跳到 0x08000000 的旧 App**：

```
读槽管理区
├─ Slot A/B 有 VALID 槽 → 跳到对应槽
├─ Slot A/B 都无效，但 0x08000000 有可启动的旧 App → 跳到 0x08000000
└─ 都没有 → 留 Bootloader 等升级
```

作用：
- 开发过渡期，Bootloader 能跳到旧 App，不破坏现有功能
- Slot A/B 都损坏时，可作为最后一个软件救砖通道（跳回旧 App）

### 11.3 分步验证流程（每步验证、可回退，绝不一步到位）

| 步骤 | 操作 | 擦除范围 | 验证点 |
|------|------|---------|--------|
| 0 | 确认 App 在 0x08000000 正常联网 | 全片重烧 | 安全基线 |
| 1 | 烧 Bootloader（0x08000000），App 仍在 0x08000000 | 只擦 0x08000000-0x0800FFFF | Bootloader 10s 跳转后 App 仍联网 |
| 2 | App 编译到 Slot A（0x08010000）烧入 | 只擦 0x08010000-0x0805FFFF | App 从 Slot A 联网（VTOR 正确） |
| 3 | App 编译到 Slot B（0x08060000）烧入 | 只擦 0x08060000-0x080BFFFF | App 从 Slot B 联网 |
| 4 | HTTP/TFTP 上传新固件到备用槽 | 只擦备用槽 | 校验/提交/复位后新槽联网 |
| 5 | 再上传损坏固件 | 只擦备用槽 | 回滚到旧 VALID 槽 |

### 11.4 救砖手段排序

1. **A/B 双备份 + 兼容旧 App**（软件层）— 升级写备用槽，运行槽不动，天然不砖
2. **ST-Link SWD 连接**（最常用）— 用 STM32CubeProgrammer 连接芯片，全片擦除再烧即可。通常**不需要 BOOT0**，只要 ST-Link 连上 SWD 就能救
3. **BOOT0 跳线**（硬件保底）— BOOT0 拉高（接 3.3V）上电，芯片从 System Memory 启动，Flash 不执行，可全片擦除重烧。本板 BOOT0 不便操作，作为最后手段

### 11.5 ST-Link SWD 救砖步骤（无需 BOOT0）

1. ST-Link 连上板子的 SWD 口（SWDIO/SWCLK/GND/3V3）
2. 打开 **STM32CubeProgrammer**
3. 选择 ST-LINK，点击 **Connect**
4. 如果芯片能识别（Device ID 显示 0x413 等）→ 点 **Erase All** 全片擦除
5. 烧录干净固件
6. 如果芯片识别不了 → 才需要 BOOT0 拉高救砖

### 11.6 BOOT0 拉高救砖步骤（最后手段）

1. **断电**
2. 把 **BOOT0 接 3.3V**（本板不便操作，可用杜邦线/镊子短接到 3.3V）
3. **上电**（芯片从 System Memory 启动，Flash 不执行）
4. STM32CubeProgrammer 连接 → **Erase All** → 烧干净固件
5. 断电，BOOT0 接回 GND（BOOT0=0）
6. 重新上电恢复

> 注意：BOOT1 引脚通常默认拉低即可，只需操作 BOOT0。
