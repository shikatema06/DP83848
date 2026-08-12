# 新 Boot 调试记录

> 2026-08-12 起 · Bootloader 推翻重写，从"最小跳转"渐进加功能
> 对应计划：`C:\Users\hhqk\.claude\plans\c-st-project-dp83848-stm32f4x7-eth-iap-v-quirky-heron.md`

## 背景与决策

旧 Bootloader（lwIP + TFTP + A/B slot + 5 秒倒计时）折腾一天定位不到 App 不跳转的根因（见 `错误boot调试记录.md`）。**用户决定推翻重来**：

- **App 首跳地址固定 0x08010000（Slot A）**，复用现有 `STM32F407XX_APP.ld`
- **第一个验证用极简测试 App**（新写），完全隔离 App 自身问题
- 第一版**不碰 slot/meta/CRC/网络逻辑**，只保证"跳转"100% 可靠
- 保留 `flash_if.c/h`、`iap_slot.c/h` 在磁盘（不参与构建），后续里程碑再接入

## 内存布局（M1/M2）

```
0x08000000  Bootloader（48KB，M1 实际只占 8800B）
0x08010000  App（M1=test_app 9000B，M2=真 App 92992B）
```

## 里程碑 1：极简 Bootloader + 极简测试 App（完成 ✅）

### 改动文件

| 文件 | 动作 |
|---|---|
| `Bootloader/bootloader_main.c` | **整文件重写**：去掉 lwIP/TFTP/iap_slot/fault-capture/5 秒倒计时，只留 时钟→GPIO→USART1→500ms 延时→`jump_to_app(0x08010000)` |
| `Bootloader/CMakeLists.txt` | 重写：去掉 LwIP_Boot/tftp/iap_slot/flash_if/net_config/ethernetif，只编 bootloader_main + gpio + usart + syscalls/sysmem + startup，链 STM32_Drivers，加 POST_BUILD objcopy 出 .bin |
| `TestApp/main.c` | **新建**：极简 App，VTOR + `__enable_irq()` + 时钟 + USART1 持续打印 `APP OK` + PE7 200ms 翻转 |
| `TestApp/CMakeLists.txt` | **新建**：链 STM32F407XX_APP.ld（0x08010000）|
| 顶层 `CMakeLists.txt` | 加 `add_subdirectory(TestApp)` |

### 极简 `jump_to_app`（跳转协议，以后各里程碑复用）

```c
app_sp = *(volatile uint32_t *)app_addr;        /* vector[0] = SP */
app_pc = *(volatile uint32_t *)(app_addr + 4U); /* vector[1] = Reset_Handler */
if (app_sp 在 0x20000000-0x20030000 范围) {
    __disable_irq();
    for (i=0;i<8U;i++) NVIC->ICER[i] = 0xFFFFFFFFU;  /* 清使能 */
    for (i=0;i<8U;i++) NVIC->ICPR[i] = 0xFFFFFFFFU;  /* 清挂起 */
    SysTick->CTRL = 0U;                                /* 停 SysTick */
    SCB->VTOR = app_addr;  __DSB(); __ISB();
    __set_MSP(app_sp);
    ((pFunction)app_pc)();
}
/* App 必须自己 __enable_irq() —— test_app 和真 App main() 都有 */
```

### 验证结果（全部通过）

| 信号 | 结果 |
|---|---|
| OpenOCD 挂起（不复位） | `pc = 0x08011478`（test_app 的 HAL_Delay 循环，App 区）|
| primask | `0x00`（中断已开，`__enable_irq()` 生效）|
| USART1 | 持续 `APP OK\r\n` |
| PE7 | 200ms 闪烁 |

**结论：Bootloader → App 跳转 100% 可靠。**

## 里程碑 2：换真 App 92KB（完成 ✅）

- 重新构建 `DP83848` 目标（已有 `__enable_irq()`，链接 0x08010000），`.bin` 92992B
- `main()` 反汇编确认：`str VTOR` → `cpsie i` → `HAL_Init` → ... → `HAL_UART_Transmit("BOOT OK")`
- 烧 0x08010000，复位

### 验证结果（全部通过）

| 信号 | 结果 |
|---|---|
| `Test-NetConnection 172.16.1.69 -Port 1030` | **`TcpTestSucceeded : True`**（整个调试周期第一次！）|
| ping | 通（App 网络栈工作）|
| 串口 | `BOOT` → `BOOT OK` |

**结论：极简 Bootloader → 真 App → 网络全通。** 之前的 `__enable_irq()` 缺失（HAL_Delay 死循环）在真 App 上验证已修复。

## 里程碑 3：渐进加功能（已完成 ✅，每步保持"跳转可靠"）

### 3a：boot_request 检测（完成 ✅）

- `Bootloader/bootloader_main.c`：读 `0x0800FFF8`，`0x55AA55AA`(UPGRADE) → 停 IAP；否则 → iap_slot 跳 App
- App 端 `tcp_echoserver.c` 的 `CMD_ENTER_BOOTLOADER (0x0C)` **不依赖 flash_if**——直接用 `HAL_FLASH_Program` 写 `0x0800FFF8` + `NVIC_SystemReset`
- 验证：发 `02 01 0C 00 00 00 DD AA` → 设备停在 IAP 持续打印 `BOOT IAP`

### 3b：iap_slot A/B 槽（完成 ✅）

- Bootloader `main()`：`FLASH_If_Init()` → `iap_slot_init()` → `iap_slot_get_active()` → `validate` → 跳 active 槽地址；无有效 App 打印 `NO APP`
- **修正了 3b 之前的一个误判**：`slot_meta_t` 实际是 **16 字节**（`magic, img_len, img_crc32, img_state(1B), commit_state(1B), reserved(2B)`），不是 20 字节 → `SLOT_A_META_ADDR=0x0800FFD0` 与 `SLOT_B_META_ADDR=0x0800FFE0` **不重叠**，无布局 bug
- 首次初始化：sector 3 擦净后用 CLI 写 16 字节 Slot A meta（`49415053 00016B40 8395FBF3 00000001`）+ `BOOT_SLOT=0x55AA0000`
- 验证：Bootloader 校验 Slot A → 跳 App → TCP 1030 通

### 3c：lwIP + TFTP 升级（完成 ✅）

- 恢复 `Bootloader/CMakeLists.txt`：LwIP_Boot（UDP-only 子集）+ `tftp_server.c` + `lwip_boot.c` + `net_config.c` + `ethernetif.c`
- `bootloader_main.c` UPGRADE 分支：`Boot_LWIP_Init()`（Entry IP 172.16.1.69）+ `IAP_tftpd_init()` + 永久轮询 `Boot_LWIP_Process()`（TFTP 完成自动复位）
- **镜像膨胀**：48932 B / 48KB = **99.55%**，后续加功能需留意
- `TestApp/CMakeLists.txt` 加 `test_app_B`（test_app 链接 Slot B `0x08060000`），作为"新固件"验证 spare 槽升级
- 新建 `tools/tftp_upgrade.py`：标准库 TFTP 客户端，自动算 CRC32 + 附加 4 字节 trailer + WRQ 分块上传
- **端到端验证通过**：TFTP 上传 test_app_B（9040 B / 18 块）→ 写 spare(Slot B) → CRC 校验 → `iap_slot_mark_new` + `iap_slot_commit` → `NVIC_SystemReset` → Bootloader 跳 Slot B → 串口 `APP OK`

**里程碑 3 全部完成。IAP 升级链路全通：**
`ENTER_BOOTLOADER → IAP → TFTP上传 → 写spare槽 → CRC校验 → commit → BOOT_SLOT翻转 → 复位 → 跳新固件`

## 关键技术笔记

- **USART1 = PA9(TX)/PA10(RX) AF7**；`HAL_UART_MspInit` 在 `Core/Src/usart.c` 内（含引脚配置）
- **RS485 方向脚 = PA11（USART1_RE_Pin）**：TX 前拉高 RE、发完拉低。当前极性验证可用；若某些板无输出，翻一下电平
- **时钟延时精度**：`HAL_Init` 先按 HSI 16MHz 配 SysTick，`SystemClock_Config` 后跳 168MHz，`HAL_Delay` 会偏快约 10 倍——对跳转测试无影响，后续要精确延时需在时钟配置后重调 `HAL_InitTick`
- **构建系统**：`stm32cubemx` = INTERFACE 库（include/宏），`STM32_Drivers` = OBJECT 库（HAL），工具链 `-ffunction-sections` + `--gc-sections` → 未引用函数自动丢弃，Bootloader 从 46KB 降到 8800B
- **`.bin` 生成**：各目标 CMakeLists 加 POST_BUILD objcopy（M1 之前 App 的 .bin 是手动 objcopy 的旧产物，曾导致烧了旧固件）
- **Bootloader 产物路径**：`build/Debug/Bootloader/DP83848_BOOT.bin`（根目录的 51508B 旧 .bin 是历史遗留，忽略）

## 烧录命令速查

```powershell
STM32_Programmer_CLI -c port=SWD -w "C:\ST_project\DP83848\build\Debug\Bootloader\DP83848_BOOT.bin" 0x08000000 -v
STM32_Programmer_CLI -c port=SWD -w "C:\ST_project\DP83848\build\Debug\TestApp\test_app.bin" 0x08010000 -v
STM32_Programmer_CLI -c port=SWD -w "C:\ST_project\DP83848\build\Debug\DP83848.bin" 0x08010000 -v
STM32_Programmer_CLI -c port=SWD -rst
```
