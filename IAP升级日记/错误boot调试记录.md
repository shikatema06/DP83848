# 错误 Boot 调试记录（旧 Bootloader 被推翻前）

> 2026-08-11 ~ 2026-08-12 · 旧 Bootloader（lwIP + TFTP + A/B slot）调试全程
> 目的：记录踩过的每一个坑和根因，避免重蹈覆辙。**最终决定推翻重写**（见 `新boot调试记录.md`）

## 背景

旧 Bootloader 架构：HAL → 时钟 → GPIO → `FLASH_If_Init` → `iap_slot_init` → 读 boot_request/active slot → `should_jump` 逻辑 → `Boot_LWIP_Init` → `IAP_tftpd_init` → **5 秒倒计时** → `jump_to_app`。

症状：Bootloader 模式 ping 通（lwIP 在跑），但 **App 永远不跳 / 跳过去"死"**，TCP 1030 永远 False，串口无输出。折腾一天无法收敛 → 用户决定推翻。

## 根因清单（按发现顺序）

### ① 链接脚本 `-T` 被重复拼接 → `.data`/`.bss` 不初始化（最早的 ping 不通根因）

**现象**：Bootloader 编译日志有 3 条 `redeclaration of memory region RAM/CCMRAM/FLASH` 警告；`.data`/`.bss` 没被正确初始化，所有 HAL 句柄（`heth`、`RxAllocStatus`）是随机值 → ETH RX 不进 DMA → ARP/ICMP 全无 → ping 失败。

**根因**：`cmake/gcc-arm-none-eabi.cmake` 全局 `-T STM32F407XX_APP.ld`，Bootloader/CMakeLists 又 `-T STM32F407XX_BOOT.ld`，GNU ld 把两份脚本拼一起，`_sdata/_edata/_sbss/_ebss` 全被 BOOT.ld 重新赋成 `0x20008668`。

**修复**：删掉工具链全局 `-T`，各目标用 `target_link_options` 单独指定。

### ② Bootloader `__disable_irq()` 后跳转 → App 中断全屏蔽 → HAL_Delay 死循环（"App 死了"的真因）

**现象**：App 烧进去了、向量表正确、meta 也对，但 ping 不通、串口无输出、TCP 1030 False。

**诊断**：OpenOCD **不复位挂起** → `pc=0x08012212`（`HAL_GetTick` 末尾），`primask=0x01`。

**根因**：`jump_to_app()` 里 `__disable_irq()` 后跳转，App 启动后 `primask` 仍是 1 → SysTick 中断被屏蔽 → `uwTick` 永远是 0 → `HAL_Delay` 的 `while((HAL_GetTick()-tickstart) < wait)` 死循环。整个 App 卡在第一个 HAL 延时里，`main()` 的打印/网络初始化永远到不了。

**修复**：App `main()` 在 `SCB->VTOR = ...` 之后加 `__enable_irq()`。这条之后在真 App 上验证生效（TCP 1030 第一次 True）。

**教训**：跳转后**谁负责开中断必须明确**——Bootloader 关，App 必须开。这是个协议约定，两边的 `__enable_irq()` 都是必须的。

### ③ App 的 HAL_Delay 依赖 SysTick 中断

`HAL_GetTick()` 只在 SysTick ISR（或 TIM4 的 `HAL_IncTick`）里自增。`primask=1` 时中断不触发 → `uwTick` 恒 0 → 所有基于超时的 HAL 调用（`HAL_Delay`、`HAL_UART_Transmit` 超时、lwIP 的 `sys_now()`）全部死等。这就是"App 看起来死了"的机制。

### ④ Slot A meta 的 `img_len` 损坏 → `iap_slot_validate` 永远返回 INVALID

**现象**：`BOOT_SLOT_ADDR=0x55AA0000`（active=Slot A）、meta magic=0x49415053 都正确，但 App 不跳。

**诊断**：读 `0x0800FFD8` → `img_len = 0x4E1208EC`（≈1.3GB，远超 Slot A 320KB）→ `iap_slot_validate` 第一步 `img_len > slot_size` 直接 `SLOT_IMG_INVALID` → `should_jump=0`。

**修复**：手动写正确 meta（magic=0x49415053, state=0x01, len=0x00016B40, crc=0x7AAA4F3E）。CRC 用固件同款位运算法与 Python `binascii.crc32` 验证一致。

**教训**：meta 被写坏（TFTP 升级中断 / 老代码异常写入）后，没有自我修复路径，Bootloader 永远卡在 IAP 模式。**无损坏恢复机制是致命缺陷。**

### ⑤ `.bin` 不自动生成 → 烧的是旧固件

**现象**：App `.bin` 是 92988B（旧、无 `__enable_irq`），`.elf` 是 92992B（新）。CMake 里没有 objcopy 规则，`.bin` 是手动生成的历史产物。

**根因**：构建系统只出 `.elf`，`.bin` 靠手动 objcopy，容易漏。

**修复**：各目标 CMakeLists 加 POST_BUILD objcopy。**教训：产物路径/时间戳要核对，烧录前必须确认 `.bin` 是最新的。**

### ⑥ 烧录流程擦 sector 3 → 把 `BOOT_SLOT_ADDR` 擦成 `0xFFFFFFFF`

**现象**：手动修 meta 时执行 `-e 3`（擦 sector 3），把 `0x0800FFFC`(BOOT_SLOT_ADDR) 和 `0x0800FFF8`(boot_request) 一起擦掉。只写了 20 字节 meta，**没恢复 BOOT_SLOT_ADDR** → `iap_slot_get_active()` 返回 `SLOT_ID_NONE`。

**影响**：`iap_slot_init` 的 SLOT_ID_NONE 分支理论上会自动发现并 commit（写回 BOOT_SLOT），但实际没成功 → `should_jump=0`。**说明 `iap_slot_init` 的自动发现/commit 链路不可靠。**

**教训**：sector 3 是"Flag/Config 区"，里面同时有 boot_request、两个 slot 的 meta、BOOT_SLOT_ADDR。**擦 sector 3 = 擦掉整个配置区**，要么一次性全部重写，要么别擦。

### ⑦ SLOT_A / SLOT_B meta 地址重叠 4 字节（布局 bug，M3 必须修）

```
SLOT_A_META_ADDR = 0x0800FFD0  (slot_meta_t 20 字节 → 占 0x0800FFD0–0x0800FFE3)
SLOT_B_META_ADDR = 0x0800FFE0  (slot_meta_t 20 字节 → 占 0x0800FFE0–0x0800FFF3)
```

**重叠区 0x0800FFE0–0x0800FFE3 = Slot A 的 commit_state，也是 Slot B 的 magic。** 写 Slot B meta 会覆盖 Slot A 的 commit_state；`meta_commit` 写两个 meta 时互相踩。这是 `iap_slot` 状态机不可靠的根源之一。

### ⑧ 早期代码审查发现的其它 bug（老 tftp_server.c / bootloader_main.c）

- `tftp_server.c` use-after-free（`args` 释放后立刻读 `args->tot_bytes`）
- `tftp_server.c` 完成后 `udp_recv(UDPpcb, NULL, NULL)` 把 port 69 监听也拆了 → 升级失败后没人能再唤醒
- `tftp_server.c` 早 return 路径 pbuf 泄漏
- `bootloader_main.c` 在 `HAL_FLASH_Unlock()` 之前调用 `iap_slot_init()`，dirty boot 时 flash 操作静默失败
- `iap_slot_init()` 写 sector 3 未先解锁 Flash，rollback 失效
- Bootloader IP 用 `net_config_get_active_ip()`，与 App Entry IP 不一致，用户切 IP 后 TFTP 找不到设备

## 调试工具与技巧（避免重蹈覆辙）

### 判断"是 App 还是 Bootloader 在跑"——OpenOCD 不复位挂起

```powershell
cmd /c "C:\ST\openocd\DevEnv\openocd-v0.12.0-i686-w64-mingw32\bin\openocd.exe" -f interface/stlink.cfg -f target/stm32f4x.cfg -c "init" -c "halt" -c "reg" -c "exit"
```

- `pc` 0x0800xxxx → Bootloader；0x0801xxxx / 0x0806xxxx → App
- `primask` 0x01 → 中断被关（App 会卡在 HAL_Delay）；0x00 → 正常
- 用 `arm-none-eabi-addr2line -e <elf> <pc>` 反查源码行

### 工具坑

- `STM32_Programmer_CLI -c port=SWD` **每次连接都会软件复位** → 用 CLI 读 flash 会重启设备，干扰现场观察（读 meta 后 Bootloader 重新起跑）
- OpenOCD `halt` 必须在 `init` 之后，否则 `invalid command name "halt"`
- PowerShell 里 `& "path" -f ...` 会把 `-f` 当 PowerShell 参数解析 → 用 `cmd /c "path" -f ...`
- OpenOCD 不在 PATH，绝对路径 `C:\ST\openocd\DevEnv\openocd-v0.12.0-i686-w64-mingw32\bin\openocd.exe`

## 为什么失败 / 为什么重写

1. **多层状态机纠缠**：`should_jump` + 5 秒倒计时 + `iap_slot` 的 meta/CRC/BOOT_SLOT 互相依赖，一个环节脏数据导致整条链失效，且**没有自愈路径**
2. **sector 3 配置区设计脆弱**：boot_request + 双槽 meta + BOOT_SLOT_ADDR 挤在一个 16KB 扇区，且 SLOT_A/B meta 还重叠 4 字节
3. **调试信号被网络层干扰**：Bootloader 自己带 lwIP，ping 通不能证明 App 活着（TCP 1030 才是 App 信号）
4. **契约不清**：`__disable_irq()` 后谁负责开中断没有约定，App 首版漏了 `__enable_irq()`

**结论**：推翻后从"无网络、无 slot、无条件跳转"的最小版本重建，每层验证通过再加下一层。已见 `新boot调试记录.md`。
