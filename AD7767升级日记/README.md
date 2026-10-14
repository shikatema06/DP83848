# AD7767 升级日记 — 目录索引

> 本目录记录 **ADS8354 → AD7767 三探头高精度版本** 的完整改造过程。
> 组织方式参考同工程的 [`../IAP升级日记/`](../IAP升级日记/)。

---

## 文档清单

| 文档 | 性质 | 说明 |
|---|---|---|
| [AD7767改造工作计划_2026-10-09.md](AD7767改造工作计划_2026-10-09.md) | **主文档** | 任务清单、决策台账、风险登记册、阶段复盘表、验收清单 |
| [每日进展日志_2026-10-09.md](每日进展日志_2026-10-09.md) | **日常填写** | 每天收工前追加一条；记卡点、实测数据、决策变更 |
| [烧写与网络联调记录_2026-10-12.md](烧写与网络联调记录_2026-10-12.md) | **专题** | 烧写产物生成链路、Flash 读回校验、ping 不通排障、调试器与烧写脚本的关系、`Flash App Only` 隐患 |

---

## 怎么用

```
开工前  →  打开「改造工作计划」，看当前阶段的任务清单（§4）
工作中  →  完成一项就勾选 [x]；发现新风险登记到 R-xx；需求变更记到决策台账 D-xx
收工前  →  在「每日进展日志」追加一条（模板见该文件顶部）
阶段验收 →  填写「改造工作计划」§8 的对应阶段复盘表
```

### 状态标记约定

| 标记 | 含义 |
|---|---|
| `[ ]` | 未开始 |
| `[~]` | 进行中 |
| `[x]` | 已完成 |
| `[!]` | 阻塞（必须在日志写明卡点） |
| `[-]` | 取消（必须在决策台账写明原因） |

---

## 改造一览（速查）

| 项 | 改造前（基线 v1.0.0） | 改造后（目标 v2.0.0） |
|---|---|---|
| ADC | ADS8354 ×3，16 bit，SPI 寄存器型 | **AD7767-2 ×3，24 bit，DRDY 指示型** |
| MCLK | — | 每片独立 1.024 MHz TCXO（板上 X4/X5/X6） |
| 触发方式 | TIM7 200 µs 轮询读 | **PH12→EXTI12 独享；PA15→EXTI15；PG15 电平门控** |
| 平均 | 5 次平均 → 1 ms/点 | **32 点块平均 → 1 ms/点** |
| 数据点率 | 1000 点/s/通道 | 1000 点/s/通道（不变） |
| 点数/帧 | 80 | 80（不变） |
| 帧长 | 498 字节 | **738 字节** |
| 帧周期 | 80 ms | **80 ms**（不变） |
| 帧率 | 12.5 帧/s | 12.5 帧/s（不变） |
| SPI SCLK | SPI1 42 MHz / SPI2,3 21 MHz | **10.5 MHz**（AD7767 t8 上限约 12 MHz） |
| SPI 模式 | MODE 0（CPOL=0, CPHA=0） | **MODE 1（CPOL=0, CPHA=1）** |
| PHY 复位 | PF11 | **PF12** |
| 温湿度 | 无 | **HDC1010 @ I2C2（PF1=SCL / PF0=SDA）+ PF2=MCU_HDC** |
| 霍尔 | 无 | 仅配 GPIO（PF15 输入 / PG0 输出），不写逻辑 |

---

## 阶段与里程碑

| 阶段 | 内容 | 工时 | 里程碑 |
|---|---|---|---|
| 0 | 地基：**构建系统修复** + lwIP 加固 + 基线 + PHY 复位改 PF12 | 2.5 d | M0 可回滚基线 |
| 1 | **AD7767 单通道点亮**（关键路径） | 4.5 d | M1 读数经示波器验证 |
| 2 | **三通道 EXTI + 32 点平均 + 738B 帧**（关键路径） | 4.0 d | M2 三通道帧上线 |
| 3 | 帧协议切换 + PC 端联调 | 2.5 d | M3 数据链正确 |
| 4 | HDC1010 温湿度（I2C2） | 1.5 d | M4 温湿度真值 |
| 5 | 整合验证 + IAP 回归 + 文档 | 5.0 d | M5 可交付 |
| — | 风险缓冲 20% | 4.0 d | 合计 **24.0 d ≈ 4.8 周** |

**关键路径**：0 → 1 → 2 → 3。**阶段 4 可与阶段 3 并行**。

---

## ⚠️ 在 CubeMX 里 GENERATE CODE 之后必做一步

`cmake/stm32cubemx/CMakeLists.txt` 是 CubeMX 生成文件，CubeMX **只认识它自己生成的内容**，所以每次 GENERATE 都会删掉里面手工追加的 export 段，导致：

```
CMake Error at Bootloader/CMakeLists.txt:61 (add_library):
  No SOURCES given to target: STM32_Drivers_Boot
```
（`app_a/b/c` 也会因为 `MX_Application_Src` / `MX_LINK_LIBS` 为空而失败）

**恢复方法（一条命令，幂等）**：

```powershell
cd DP83848
powershell -ExecutionPolicy Bypass -File tools\restore_after_cubemx.ps1
cmake --preset Debug ; cmake --build build/Debug -j 8
```

> 已实测：脚本可解析、幂等、能从 export 被删的状态（0 行）恢复到 3 行、恢复后 configure 通过。
>
> `bsp_pins.c` 已移到**根 `CMakeLists.txt`**（CubeMX 不重新生成根文件），所以它不需要恢复。
> `i2c.c` / `stm32f4xx_hal_i2c*.c` 由 CubeMX 自动加入，也不需要恢复。

### 已验证：`.ioc` 的手工编辑是有效的

2026-10-10 实测：CubeMX 加载本工程 `.ioc` 后，**12 个新引脚全部被正确生成**进 `Core/Src/gpio.c`
（`MCU_HDC`/`MCU_OUT1`/`SYNC_PD_AD7767_1..3`/`DRDY_AD7767_1..3`/`MII_INT`/`MCU_IN1`/`RESET_PHY`），
I2C2 生成独立 `i2c.c`，`HAL_I2C_MODULE_ENABLED` 自动使能。**无需担心 `.ioc` 在 CubeMX 中失效。**

---

## 构建基线（2026-10-09 实测，改造前）

| 目标 | RAM | FLASH | 门禁 |
|---|---|---|---|
| `DP83848` / `app_a` | 41104 B (31.36%) | 93724 B (28.60%) | App 320 KB |
| `app_b` / `app_c` | 41104 B | 93724 B (23.84%) | Slot B 384 KB |
| `DP83848_BOOT` | 34504 B (26.32%) | **32744 B (66.62%)** | **48 KB 硬门禁** |
| `test_app` / `test_app_B` | 1656 B | 9036 B | — |

> ⚠️ **48 KB 是硬门禁**：Bootloader 只能占 sectors 0–2；sector 3 (`0x0800C000–0x08010000`) 是 flag 扇区，运行期会被擦除，Bootloader 代码落入其中会变砖。
>
> ⚠️ **构建前必查**：`build/Debug/CMakeCache.txt` 中 `CMAKE_HOME_DIRECTORY` 必须指向**本工程**。曾出现过缓存指向 `C:/ST_project/DP83848` 的情况，导致 `cmake --build` "成功"但编译的是另一份源码树。

---

## 相关外部文档

| 文档 | 路径 |
|---|---|
| 技术文档（基线） | [`../技术文档.md`](../技术文档.md) |
| Bug 排查报告 | [`../代码Bug排查报告.md`](../代码Bug排查报告.md) |
| CubeMX 差异指南 | [`../CubeMX_config_diff_V3.md`](../CubeMX_config_diff_V3.md) |
| IAP 设计文档 | [`../IAP_Flash_Layout.md`](../IAP_Flash_Layout.md) |
| IAP 改造日记 | [`../IAP升级日记/`](../IAP升级日记/) |
| 硬件原理图 | [`../../三探头高精度版本_2026.8.17.pdf`](../../三探头高精度版本_2026.8.17.pdf) |
| 引脚配置表 | [`../../gaojingdu_santantou_STM_GPIO.xlsx`](../../gaojingdu_santantou_STM_GPIO.xlsx) |
