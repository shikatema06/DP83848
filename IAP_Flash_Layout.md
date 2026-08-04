# IAP Flash 分区布局文档

> 版本：v1.0  |  目标：STM32F407VG (1MB Flash, 单 bank)  |  日期：2026-08-04

## 1. 概述

本文档定义了重写后的 IAP Flash 分区布局，采用**软件模拟 A/B 双备份**方案，
使 Bootloader 能在升级失败时自动回滚到旧固件。

---

## 2. Flash 分区总览

```
STM32F407VG Flash 总容量: 1024 KB (0x08000000 ~ 0x08100000)

┌─────────────────────┬──────────┬───────────────┬──────────┬─────────────────────┐
│      区域             │ 起始地址   │   结束地址      │   大小    │       用途           │
├─────────────────────┼──────────┼───────────────┼──────────┼─────────────────────┤
│  Sector 0            │ 0x08000000│ 0x08003FFF    │ 16 KB    │ Bootloader          │
│  Sector 1            │ 0x08004000│ 0x08007FFF    │ 16 KB    │ Bootloader          │
│  Sector 2            │ 0x08008000│ 0x0800BFFF    │ 16 KB    │ Bootloader          │
│  Sector 3            │ 0x0800C000│ 0x0800FFFF    │ 16 KB    │ Bootloader + SlotMgr│
│                      │           │               │          │ 末尾4B: boot_valid  │
├─────────────────────┼──────────┼───────────────┼──────────┼─────────────────────┤
│  Sector 4            │ 0x08010000│ 0x0801FFFF    │ 64 KB    │ A 槽 (App Slot A)   │
│  Sector 5            │ 0x08020000│ 0x0803FFFF    │ 128 KB   │ A 槽                │
│  Sector 6            │ 0x08040000│ 0x0806FFFF    │ 192 KB   │ A 槽                │
│                      │           │               │          │ A槽总计: 384 KB     │
├─────────────────────┼──────────┼───────────────┼──────────┼─────────────────────┤
│  Sector 7            │ 0x08070000│ 0x0807FFFF    │ 64 KB    │ B 槽 (App Slot B)   │
│  Sector 8            │ 0x08080000│ 0x0809FFFF    │ 128 KB   │ B 槽                │
│  Sector 9            │ 0x080A0000│ 0x080BFFFF    │ 128 KB   │ B 槽                │
│  Sector 10           │ 0x080C0000│ 0x080CFFFF    │ 64 KB    │ B 槽                │
│                      │           │               │          │ B槽总计: 384 KB     │
├─────────────────────┼──────────┼───────────────┼──────────┼─────────────────────┤
│  Sector 10 (后半)     │ 0x080D0000│ 0x080DFFFF    │ 64 KB    │ 预留 (日志/配置扩展)  │
├─────────────────────┼──────────┼───────────────┼──────────┼─────────────────────┤
│  Sector 11           │ 0x080E0000│ 0x080FFFFF    │ 128 KB   │ net_config (保护!)   │
└─────────────────────┴──────────┴───────────────┴──────────┴─────────────────────┘
```

### 2.1 关键地址宏定义

```c
// ---- Bootloader 区 ----
#define BOOTLOADER_BASE              0x08000000U
#define BOOTLOADER_SIZE              0x00010000U   // 64 KB

// ---- 槽管理区 (在 Sector 3 末尾) ----
#define SLOT_MGR_ADDR                0x0800FE00U   // Sector 3 末尾 512B
#define SLOT_MGR_MAGIC               0x49415053U   // "IAPS"
#define BOOT_VALID_ADDR              0x0800FFFCU
#define BOOT_VALID_MAGIC             0xAA55AA55U

// ---- App A 槽 ----
#define SLOT_A_BASE                  0x08010000U
#define SLOT_A_SIZE                  0x00060000U   // 384 KB
#define SLOT_A_END                   (SLOT_A_BASE + SLOT_A_SIZE)

// ---- App B 槽 ----
#define SLOT_B_BASE                  0x08070000U
#define SLOT_B_SIZE                  0x00060000U   // 384 KB
#define SLOT_B_END                   (SLOT_B_BASE + SLOT_B_SIZE)

// ---- 预留区 ----
#define RESERVED_BASE                0x080D0000U
#define RESERVED_SIZE                0x00010000U   // 64 KB (Sector 10 后半)

// ---- net_config (禁止擦除!) ----
#define NET_CFG_BASE                 0x080E0000U
#define NET_CFG_SIZE                 0x00020000U   // 128 KB
```

### 2.2 体积验证

| 项目 | 大小 |
|------|------|
| APP Debug 编译 | ~90 KB Flash |
| APP Release (-Os) 预估 | ~55-70 KB |
| 单槽容量 | 384 KB |
| 单槽使用率 | ~17-24% |
| **安全余量** | **5-7 倍** |

---

## 3. 槽管理数据结构

### 3.1 槽管理器 (Slot Manager)

位于 Sector 3 末尾 `0x0800FE00`，占用 ≤512 字节。

```c
#define SLOT_MAX_SLOTS  2

typedef enum {
    SLOT_STATE_EMPTY   = 0x00,   // 空槽（未烧录）
    SLOT_STATE_VALID   = 0x01,   // 有效固件
    SLOT_STATE_NEW     = 0x02,   // 新固件已写入，待提交
    SLOT_STATE_INVALID = 0x03,   // 无效/损坏
    SLOT_STATE_OLD     = 0x04,   // 旧固件备份（可回滚）
} slot_state_t;

typedef struct {
    uint32_t magic;             // 0x49415053 ("IAPS")
    uint32_t version;           // 管理器版本号（递增）
    uint8_t  active_slot;       // 当前激活槽编号 (0=A, 1=B)
    uint8_t  commit_flag;       // 0=正常, 1=待提交
    uint16_t reserved0;

    struct {
        uint32_t base_addr;     // 槽起始地址
        uint32_t size;          // 槽大小
        uint32_t fw_version;    // 固件版本号
        uint32_t fw_size;       // 固件实际大小 (bytes)
        uint32_t crc32;         // 固件 CRC32 校验值
        slot_state_t state;     // 槽状态
        uint8_t  reserved[11];
    } slots[SLOT_MAX_SLOTS];

    uint32_t crc32;             // 槽管理器自身的 CRC32（保护完整性）
} __attribute__((packed)) slot_mgr_t;
// sizeof(slot_mgr_t) = ~80 bytes，远小于 512B
```

### 3.2 单槽固件头部

每个槽的前 512 字节保留为固件头部（但向量表也在此处）。

```c
// 槽头部与向量表共用前 512 字节
// .isr_vector 从 0x00 开始，固件头部信息嵌入 .isr_vector 未用的保留向量

// 使用 IRQ 保留槽存储头部（ST 规范中 0x10-0x3C 为系统保留）
#define SLOT_HDR_OFFSET_MAGIC      0x10   // 向量表 offset 16: magic
#define SLOT_HDR_OFFSET_VERSION    0x14   // offset 20: 固件版本
#define SLOT_HDR_OFFSET_SIZE       0x18   // offset 24: 固件大小 (byte)
#define SLOT_HDR_OFFSET_CRC        0x1C   // offset 28: 固件 CRC32
#define SLOT_HDR_OFFSET_TIMESTAMP  0x20   // offset 32: 编译时间戳
#define SLOT_HDR_MAGIC             0x46575342  // "BWSF" = Bootloader Warehouse Slot Firmware
```

**说明**：由于 `.isr_vector` 的前 16 个向量 (0x00-0x3C) 中部分为系统保留（出厂预留），
可利用这些保留向量存储固件元信息，不额外占用 Flash 空间。

---

## 4. 槽状态机

```
                    ┌─────────────────────────────────┐
                    │                                 │
                    ▼                                 │
              ┌──────────┐                      ┌──────────┐
   首次烧录    │  EMPTY   │                      │  INVALID  │
  ──────────► │  0x00    │─────────────────────►│  0x03    │
              └────┬─────┘   升级失败/断电/CK失败   └────┬─────┘
                   │                                    │
                   │ 升级成功 + CRC OK                   │ 重新升级
                   ▼                                    │
              ┌──────────┐                              │
              │  NEW     │                              │
              │  0x02    │                              │
              └────┬─────┘                              │
                   │                                    │
                   │ 提交: 翻转 active_slot              │
                   ▼                                    ▼
              ┌──────────┐   被新 VALID 取代     ┌──────────┐
              │  VALID   │──────────────────────►│   OLD    │
              │  0x01    │                       │  0x04    │
              └────┬─────┘                       └────┬─────┘
                   │                                  │
                   │ 新槽崩/看门狗/校验失败              │
                   └──────────────────────────────────┘
                                回滚: OLD → VALID
```

### 4.1 状态转换触发条件

| 当前状态 | 目标状态 | 触发条件 |
|----------|----------|----------|
| EMPTY | NEW | 首次固件接收完成，CRC 校验通过 |
| NEW | VALID | slot_mgr.commit_flag 翻转，active_slot 指向该槽 |
| NEW | INVALID | CRC 校验失败、写入过程中断电、slot_mgr CRC 损坏 |
| VALID | OLD | 新槽成功提交后，旧 active 槽降级 |
| OLD | VALID | 回滚：新槽 INVALID，旧 OLD 槽恢复为 VALID |
| INVALID | EMPTY (逻辑) | 即将被新写入覆盖 |

### 4.2 防断电撕裂：双阶段提交

```
阶段 1: ERASING (写前擦除)
  slot_mgr.commit_flag = 0x01
  擦除目标扇区 (slot_new)
  slot_mgr.commit_flag = 0x02

阶段 2: WRITING (接收写入)
  写入固件数据 + CRC32 累加
  slot_mgr.slots[target].state = SLOT_STATE_NEW
  slot_mgr.slots[target].fw_size = received_size
  slot_mgr.slots[target].crc32 = calc_crc

阶段 3: COMMIT (提交翻转)
  slot_mgr.commit_flag = 0x03
  slot_mgr.slots[target].state = SLOT_STATE_VALID
  slot_mgr.slots[old].state = SLOT_STATE_OLD
  slot_mgr.active_slot = target
  slot_mgr.crc32 = recalc_mgr_crc32()
  slot_mgr.commit_flag = 0x00

断电恢复:
  - commit_flag == 0x01: 擦除未完成 → 重新擦
  - commit_flag == 0x02: 写入未完成 → 目标槽 INVALID，保留旧槽
  - commit_flag == 0x03: 提交中途断电 → 重新检查两槽 CRC，修正状态
  - commit_flag == 0x00: 正常状态
```

---

## 5. Bootloader 启动流程

```
上电/复位
    │
    ▼
读 slot_mgr (0x0800FE00)
    │
    ├─ magic ≠ "IAPS" → 首次启动 → 初始化 slot_mgr → 进入升级等待
    │
    ├─ magic == "IAPS"
    │    │
    │    ├─ 检查 active_slot CRC 是否通过
    │    │    ├─ 通过 → 存在有效 App
    │    │    └─ 失败 → 尝试回滚 (OLD→VALID)
    │    │         ├─ 回滚成功 → 存在有效 App
    │    │         └─ 回滚失败 → 无有效 App，永久等待升级
    │    │
    │    ├─ 存在有效 App:
    │    │    ├─ 初始化 LwIP + ETH
    │    │    ├─ 启动 TFTP Server (UDP 69)
    │    │    ├─ 启动 TCP 升级 Server (TCP 1024)
    │    │    ├─ 启动 HTTP Server (TCP 80)
    │    │    └─ 循环等待 3 秒:
    │    │         ├─ 收到升级请求 → 停止超时，进入接收流程
    │    │         └─ 3 秒无请求 → NVIC_SystemReset() → jump_to_app(active_slot)
    │    │
    │    └─ 不存在有效 App:
    │         └─ 初始化网络，永久等待升级
    │
    └─ 升级接收流程:
         ├─ 选择备用槽 (非 active_slot)
         ├─ iap_rx_begin() → 擦除芯片启动
         ├─ 循环接收数据 → iap_rx_write() → Flash 编程 + CRC 累加
         ├─ 接收完成:
         │    ├─ iap_rx_end() → CRC 比对
         │    ├─ CRC 通过 → 双阶段提交 → NVIC_SystemReset()
         │    └─ CRC 失败 → 目标槽 INVALID → 回等待
         └─ 接收失败/超时:
              └─ iap_rx_abort() → 目标槽 INVALID → 回等待
```

---

## 6. 升级通道对比

| 通道 | 端口 | 协议 | 客户端 | 适用场景 | Bootloader 增量 |
|------|------|------|--------|----------|-----------------|
| **TFTP** | UDP 69 | RFC 1350 | PC TFTP 客户端 | 直连升级，工具链兼容 | ~4 KB |
| **HTTP** | TCP 80 | HTTP/1.0 POST | 任意浏览器 | 远程网页升级，无需装软件 | ~6-8 KB |
| **TCP 私有** | TCP 1024 | 自定义帧 | 上位机软件 | 批量产测、自动化 | ~2 KB |

三者共用统一接收层 `iap_rx`，底层写 Flash 逻辑完全一致。

---

## 7. 链接脚本修正

### 7.1 当前问题

```
STM32F407XX_APP.ld:  FLASH ORIGIN = 0x08000000, LENGTH = 1024K
STM32F407XX_FLASH.ld: FLASH ORIGIN = 0x08000000, LENGTH = 1024K
STM32F407XX_BOOT.ld: FLASH ORIGIN = 0x08000000, LENGTH = 64K
```

Bootloader 和 APP 都从 0x08000000 开始 → **重叠冲突**。

### 7.2 修正方案

创建两个 APP 链接脚本，分别编译 A 槽和 B 槽镜像：

**`STM32F407XX_SLOT_A.ld`** (A 槽):
```ld
MEMORY
{
    RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
    CCMRAM (xrw) : ORIGIN = 0x10000000, LENGTH = 64K
    FLASH  (rx)  : ORIGIN = 0x08010000, LENGTH = 384K
}
```

**`STM32F407XX_SLOT_B.ld`** (B 槽):
```ld
MEMORY
{
    RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
    CCMRAM (xrw) : ORIGIN = 0x10000000, LENGTH = 64K
    FLASH  (rx)  : ORIGIN = 0x08070000, LENGTH = 384K
}
```

**Bootloader `STM32F407XX_BOOT.ld`** 保持:
```ld
MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
}
```

### 7.3 CMake 构建目标

```cmake
# Bootloader 目标
add_executable(DP83848_BOOT ${BOOTLOADER_SOURCES})
target_link_options(DP83848_BOOT PRIVATE -T ${CMAKE_SOURCE_DIR}/Bootloader/STM32F407XX_BOOT.ld)

# APP A 槽目标 (默认)
add_executable(DP83848_A ${APP_SOURCES})
target_link_options(DP83848_A PRIVATE -T ${CMAKE_SOURCE_DIR}/STM32F407XX_SLOT_A.ld)

# APP B 槽目标 (可选，用于验证/调试)
add_executable(DP83848_B ${APP_SOURCES})
target_link_options(DP83848_B PRIVATE -T ${CMAKE_SOURCE_DIR}/STM32F407XX_SLOT_B.ld)
```

---

## 8. 升级固件包格式

### 8.1 .iap 固件包

```
┌────────────┬──────────┬────────────┬───────────────┬──────────┐
│  magic     │ version  │ fw_size    │ target_slot   │ CRC32    │
│  4B        │  4B      │  4B        │  1B           │  4B      │
│ "IAPF"     │ u32      │ u32 (byte) │ 0=A/1=B/FF=AUTO│ u32     │
├────────────┴──────────┴────────────┴───────────────┴──────────┤
│                                                                │
│                    原始 .bin 固件数据                           │
│                    (fw_size bytes)                              │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

包 CRC32 覆盖 : magic~固件数据末尾（17 字节头 + fw_size 字节）。
上位机/网页生成此包后发送给设备，设备收到后先校验包 CRC，再提取 .bin 写入目标槽。

---

## 9. 调试状态编码

### 9.1 LED 闪烁编码

| 状态 | LED 模式 | 说明 |
|------|----------|------|
| 空闲等待升级 | 慢闪 (1Hz) | 200ms ON, 800ms OFF |
| 固件接收中 | 快闪 (5Hz) | 100ms 交替 |
| CRC 校验中 | 双闪 | 50ms ON ×2, 500ms OFF |
| 校验通过 | 常亮 2s | 然后自动复位 |
| 校验失败 | 3 长闪 | 500ms ×3, 然后回等待 |
| 跳转 App | 熄灭 | 过渡状态 |
| App 崩溃 (看门狗) | SOS 模式 | . . . - - - . . . |

### 9.2 UDP 日志广播

Bootloader 启动后，每 500ms 或事件触发时向 `255.255.255.255:5000` 发送状态信息：

```
"IAP:BOOT slotA=VALID slotB=EMPTY active=A"
"IAP:WAIT timeout=30s"
"IAP:RECV HTTP 45% 172.16.1.100"
"IAP:CRC OK size=65536 crc=0xABCD1234"
"IAP:COMMIT slotA→OLD slotB→VALID"
"IAP:JUMP slotB 0x08070000"
"IAP:ERR TFTP block mismatch"
```

上位机/调试工具监听 UDP 5000 端口即可实时看到设备状态。

---

## 10. 附录：Sector 地址速查

```c
// STM32F407VG Flash Sector 地址表
#define ADDR_FLASH_SECTOR_0     ((uint32_t)0x08000000)  // 16 KB
#define ADDR_FLASH_SECTOR_1     ((uint32_t)0x08004000)  // 16 KB
#define ADDR_FLASH_SECTOR_2     ((uint32_t)0x08008000)  // 16 KB
#define ADDR_FLASH_SECTOR_3     ((uint32_t)0x0800C000)  // 16 KB  (Bootloader 末)
#define ADDR_FLASH_SECTOR_4     ((uint32_t)0x08010000)  // 64 KB  (Slot A 开始)
#define ADDR_FLASH_SECTOR_5     ((uint32_t)0x08020000)  // 128 KB (Slot A)
#define ADDR_FLASH_SECTOR_6     ((uint32_t)0x08040000)  // 128 KB (Slot A 结束)
#define ADDR_FLASH_SECTOR_7     ((uint32_t)0x08060000)  // 128 KB (Slot B 开始)
#define ADDR_FLASH_SECTOR_8     ((uint32_t)0x08080000)  // 128 KB (Slot B)
#define ADDR_FLASH_SECTOR_9     ((uint32_t)0x080A0000)  // 128 KB (Slot B)
#define ADDR_FLASH_SECTOR_10    ((uint32_t)0x080C0000)  // 128 KB (Slot B 结束 + 预留)
#define ADDR_FLASH_SECTOR_11    ((uint32_t)0x080E0000)  // 128 KB (net_config)
```

> **警告**: Sector 11 存储 net_config（IP 配置等），任何升级流程**禁止擦除/编程**此区域。
