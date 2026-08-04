# DP83848 V3 vs 旧版原理图 CubeMX 配置差异与修改指南

## 结论速览

当前 `DP83848.ioc` 配置对应**旧版原理图（yuanlitu1.pdf）**。新版原理图（`THREE PROBES_CONTROL_PHYDP8384_V3.pdf`）在 MCU 侧做了以下精简/改动：

1. **PHY 复位脚从 PD3 改到 PF11**
2. **MII 数据位 RXD2/RXD3 从 PH6/PH7 改到 PB0/PB1**
3. **删除 4 条可选 MII 线到 MCU 的连接**：MII_INT、MII_CRS、MII_RX_ER、MII_COL
4. **关闭 PA8 MCO1 输出**（PHY 改由 X3 25MHz 晶振独立供电，默认不焊 R137）

ETH 核心收发线（TXD0/1/2/3、RXD0/1、TX_EN、RX_DV、RX_CLK、TX_CLK、MDC、MDIO）引脚不变。

---

## 1. 引脚差异对照表

| 功能 | 旧版原理图 / 当前 .ioc | 新版 V3 原理图 | CubeMX 操作 |
|---|---|---|---|
| ETH_MII_TX_EN | PG11 | PG11 | 保持 |
| ETH_MII_TXD0 | PG13 | PG13 | 保持 |
| ETH_MII_TXD1 | PG14 | PG14 | 保持 |
| ETH_MII_TXD2 | PC2 | PC2 | 保持 |
| ETH_MII_TXD3 | PB8 | PB8 | 保持 |
| ETH_MII_TX_CLK | PC3 | PC3 | 保持 |
| ETH_MII_RX_CLK | PA1 | PA1 | 保持 |
| ETH_MII_RX_DV | PA7 | PA7 | 保持 |
| ETH_MII_RXD0 | PC4 | PC4 | 保持 |
| ETH_MII_RXD1 | PC5 | PC5 | 保持 |
| **ETH_MII_RXD2** | **PH6** | **PB0** | **改到 PB0** |
| **ETH_MII_RXD3** | **PH7** | **PB1** | **改到 PB1** |
| ETH_MII_MDC | PC1 | PC1 | 保持 |
| ETH_MII_MDIO | PA2 | PA2 | 保持 |
| **ETH_MII_CRS** | **PH2** | **NC（不接 MCU）** | **删除 ETH_CRS，PH2 置 GPIO_Input 或 NC** |
| **ETH_MII_COL** | **PH3** | **NC（不接 MCU）** | **删除 ETH_COL，PH3 置 GPIO_Input 或 NC** |
| **ETH_MII_RX_ER** | **PI10** | **NC（不接 MCU）** | **删除 ETH_RX_ER，PI10 置 GPIO_Input 或 NC** |
| **MII_INT** | **PB12（GPIO_Output）** | **NC（不接 MCU）** | **PB12 改为 GPIO_Input 或释放为 NC** |
| **PHY RESET** | **PD3（代码手动配置）** | **PF11** | **PF11 配置为 GPIO_Output，Label = RESET_PHY** |
| **RCC_MCO_1** | **PA8** | **NC** | **关闭 MCO1，PA8 改为 GPIO_Input 或 NC** |

> 注：V3 中 MII_INT/CRS/RX_ER/COL 在 PHY 侧仍通过电阻接到 DP83848 引脚，但这些引脚只作为 strap/中断用途，不再连到 MCU。

---

## 2. CubeMX 修改步骤

在 STM32CubeMX 中打开 `DP83848.ioc`，按下面顺序操作：

### 2.1 ETH 引脚调整

1. 进入 **Pinout view**。
2. 找到 **PB0**：点击 → 选择 `ETH_RXD2`（AF11）。
3. 找到 **PB1**：点击 → 选择 `ETH_RXD3`（AF11）。
4. 找到 **PH6**：当前为 `ETH_RXD2`，点击 → 选择 `Reset_State` 或 `GPIO_Input`。
5. 找到 **PH7**：当前为 `ETH_RXD3`，点击 → 选择 `Reset_State` 或 `GPIO_Input`。
6. 找到 **PH2**：当前为 `ETH_CRS`，点击 → 选择 `Reset_State` 或 `GPIO_Input`。
7. 找到 **PH3**：当前为 `ETH_COL`，点击 → 选择 `Reset_State` 或 `GPIO_Input`。
8. 找到 **PI10**：当前为 `ETH_RX_ER`，点击 → 选择 `Reset_State` 或 `GPIO_Input`。

### 2.2 关闭 MCO1

1. 进入 **Pinout view** → 点击 **PA8**。
2. 当前为 `RCC_MCO_1`，改为 `Reset_State` 或 `GPIO_Input`。
3. 左侧 **RCC** → **Clock Configuration** → 关闭 **MCO1**（设为 `Disable`）。

### 2.3 MII_INT 释放

1. 点击 **PB12**，当前为 `GPIO_Output`（Label = MII_INT）。
2. 改为 `Reset_State` 或 `GPIO_Input`；如不再需要，删除 Label。

### 2.4 添加 PHY 复位脚 PF11

1. 点击 **PF11** → 选择 `GPIO_Output`。
2. 在右侧 **GPIO** 设置中，给 PF11 添加 **GPIO Label**：`RESET_PHY`。
3. 建议配置：
   - **GPIO output level**: `High`（上电不复位）
   - **GPIO mode**: `Output Push Pull`
   - **GPIO Pull-up/Pull-down**: `No pull-up and no pull-down`
   - **Maximum output speed**: `Low`（复位线不需要高速）

### 2.5 重新生成代码

1. **Project Manager** → **Project** → 确认 Toolchain 仍为 **CMake**。
2. 点击 **GENERATE CODE**。
3. 生成后检查：
   - `Core/Src/gpio.c` 中应出现 `PF11` 初始化（`GPIO_InitStruct.Pin = RESET_PHY_Pin`）。
   - `Core/Src/main.c` / `Core/Src/gpio.c` 中不应再初始化 PD3（旧复位脚）。

---

## 3. 代码层面必须同步修改

CubeMX 重新生成只能改 GPIO 初始化。**ETH 驱动里的复位引脚定义和 PHY 地址等仍需手动改代码**：

### 3.1 修改 PHY 复位引脚（ethernetif.c / gpio.c）

旧版代码使用 **PD3** 作为 PHY 复位，V3 改为 **PF11**。需确认/修改以下位置：

- `Core/Src/gpio.c` 中 `MX_GPIO_PhyReset()`：把 `PHY_RST_GPIO_Port` / `PHY_RST_Pin` 从 PD3 改为 PF11（若已在 CubeMX 中配好 Label，生成后宏名会变为 `RESET_PHY_GPIO_Port` / `RESET_PHY_Pin`）。
- `LWIP/Target/ethernetif.c` 中 `low_level_init()` 调用的复位函数：确认它操作的是 PF11。
- 删除或注释掉任何直接写 `GPIO_PIN_3` / `GPIOD` 复位 PHY 的代码。

### 3.2 删除 MII_INT 相关代码

- 旧版 `PB12` 作为 `MII_INT` 输出。V3 不接 MCU，删除 `gpio.c` / `main.c` 中操作 `MII_INT` 的代码。

### 3.3 MCO 相关代码

- 若代码中手动配置过 `HAL_RCC_MCOConfig(RCC_MCO1, ...)` 或 PA8，V3 不需要，可删除。
- 若使用旧版且 R137 上件，保留；V3 默认不焊 R137，必须删除。

### 3.4 ETH 模式保持 MII

- 保持 `ETH.MediaInterface=HAL_ETH_MII_MODE`（V3 默认 MII，R142 上件）。
- PHY 地址保持 **1**（V3 PHY 地址配置表仍为 PHYAD[4:0]=00001）。

---

## 4. 修改后验证清单

| 检查项 | 预期结果 |
|---|---|
| `gpio.c` 中 PF11 初始化 | 有 `RESET_PHY_GPIO_Port` / `RESET_PHY_Pin` 初始化 |
| `gpio.c` 中无 PD3 复位配置 | PD3 不再作为 PHY 复位 |
| `ethernetif.c` 复位调用 | 操作 PF11，不再操作 PD3 |
| ETH 引脚列表 | PA1/2/7、PB0/1/8、PC1/2/3/4/5、PG11/13/14 |
| PH2/PH3/PH6/PH7/PI10 | 不再配置为 ETH，设为 GPIO_Input 或 NC |
| PA8 | 不再作为 MCO1 |
| PB12 | 不再作为 MII_INT |
| 编译通过 | `cmake --build --preset Debug` 无错误 |
| 烧录后 ping | PC 同网段 ping `172.16.1.69` 通 |

---

## 5. 风险提醒

- **不要直接 hand-edit `.ioc` 文件**。CubeMX 内部有引脚冲突检查和 AF 映射校验，手改容易破坏项目。建议在 CubeMX GUI 中按上表修改。
- 如果当前代码中对 PD3 的引用很多（例如 BOOTLOADER 也用了 PD3），改到 PF11 后要全局检查。
- V3 删除 MII_INT 后，PHY 中断不可用。若以后需要 link-up 中断，可用轮询 `ethernet_link_check_state()`，或重新把 PWR_DOWN/INT 引脚连到某个 GPIO。
