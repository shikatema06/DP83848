# 串口功能测试指南

## 📋 测试概述

本测试模块提供对STM32F407三个USART接口（USART1/2/3）的全面测试，包括：
- ✅ 基础发送测试（Blocking模式）
- ✅ 基础接收测试（Interrupt模式）
- ✅ DMA收发测试
- ✅ RS485半双工通信测试
- ✅ 长时间稳定性测试

---

## 🔧 硬件连接要求

### 1. **调试输出**
- **USART1 (PA9 TX, PA10 RX)**: 连接USB转TTL模块到PC
- 波特率: 115200, 8N1
- 用途: 查看测试结果打印

### 2. **RS485测试连接**
```
USART1 (PA9 TX, PA10 RX, PA11 RE/DE) → RS485收发器 → PC USB转RS485
USART2 (PD5 TX, PD6 RX, PD7 RE/DE)   → RS485收发器 → 可选
USART3 (PB10 TX, PB11 RX, PE15 RE/DE)→ RS485收发器 → 可选
```

### 3. **回环测试（Phase 4需要）**
- 短接 USART1 的 TX (PA9) 和 RX (PA10) 引脚
- 用于自发自收测试

---

## 🚀 快速开始

### 步骤1: 启用测试

在 `main.c` 中取消注释测试调用：

```c
/* USER CODE BEGIN 2 */

/* 初始化串口测试模块 */
USART_Test_Init();

/* 运行串口综合测试 */
USART_Test_RunAll();  // ← 取消这行的注释

/* USER CODE END 2 */
```

### 步骤2: 编译并烧录

```bash
# 使用CMake构建
cd build
cmake --build . --config Debug

# 或使用IDE直接编译烧录
```

### 步骤3: 打开串口终端

推荐使用以下工具之一：
- **PuTTY** (Windows)
- **SecureCRT** (Windows)
- **minicom** (Linux)
- **Screen** (macOS/Linux)

配置参数：
```
端口: COMx (根据实际设备)
波特率: 115200
数据位: 8
停止位: 1
校验位: None
流控: None
```

### 步骤4: 复位运行

按下开发板复位按钮，观察测试结果输出。

---

## 📊 测试阶段详解

### Phase 1: 基础发送测试 ✅

**测试内容**:
1. 单字符发送 ('A')
2. 字符串发送 ("Hello from STM32!\r\n")
3. 大数据包发送 (200字节 0x55)

**预期输出**:
```
[TEST] USART1 (RS485) - Basic TX
  [1.1] Single character TX... PASS
  [1.2] String TX... PASS (19 bytes)
  [1.3] Large packet TX (200 bytes)... PASS
  [RESULT] Phase 1 PASSED
```

**验证方法**: 
- PC端串口助手应能收到上述数据
- RS485的RE/DE引脚应在发送时置高，发送完成后置低

---

### Phase 2: 基础接收测试 ⏳

**测试内容**:
- 等待从PC发送10字节数据

**操作步骤**:
1. 当看到提示 `Please send data from PC terminal...` 时
2. 在PC端串口助手输入任意10个字符并发送
3. 观察开发板是否成功接收并显示

**预期输出**:
```
[TEST] USART1 RX Test
  Please send data from PC terminal...
  Waiting for data (timeout: 1000 ms)...
  [2.1] Received 10 bytes: 48 65 6C 6C 6F 20 57 6F 72 6C 
  [RESULT] Phase 2 PASSED
```

**常见问题**:
- ❌ TIMEOUT: 检查RX接线是否正确
- ❌ ERROR: 检查波特率是否匹配

---

### Phase 3: DMA测试 🔄

**测试内容**:
1. DMA方式发送字符串
2. DMA方式接收20字节数据

**操作步骤**:
1. 自动执行DMA发送
2. 当提示发送数据时，从PC发送20字节

**预期输出**:
```
[TEST] USART1 DMA
  [3.1] DMA TX Test... PASS
  [3.2] DMA RX Test (send 20 bytes from PC)... PASS (Received: 12345678901234567890)
  [RESULT] Phase 3 PASSED
```

---

### Phase 4: RS485全双工测试 🔁

**重要**: 需要先短接TX和RX引脚！

**测试内容**:
- 回环测试：发送数据后接收自己的数据
- 验证数据完整性

**操作步骤**:
1. **断电状态下**短接 PA9 (TX) 和 PA10 (RX)
2. 上电运行测试
3. 观察回环数据是否一致

**预期输出**:
```
[TEST] USART1 RS485 (Loopback mode required)
  Note: Connect TX and RX pins for loopback test
  [4.1] Starting async RX... OK
  [4.2] Sending data... OK
  [4.3] Waiting for echo... 
  [4.4] Verifying data... PASS (Data matched)
  [RESULT] Phase 4 PASSED
```

**故障排查**:
- 如果失败，检查：
  - TX/RX是否正确短接
  - RS485收发器供电是否正常
  - RE/DE控制时序是否正确

---

### Phase 5: 稳定性测试 ⏱️

**默认禁用**，需要手动启用：

```c
// 在 USART_Test_RunAll() 中取消注释
USART_Test_Phase5_Stability(&huart1, 60000, "USART1 60s");
```

**测试内容**:
- 连续发送60秒（可自定义时长）
- 每秒约10次发送
- 统计错误率和超时次数

**预期输出**:
```
[TEST] USART1 60s (60000 ms)
  Progress: 100 iterations, 10000 ms elapsed
  Progress: 200 iterations, 20000 ms elapsed
  ...
  [RESULT] Phase 5 PASSED (600 iterations completed)
```

---

## 🎯 单独测试某个串口

如果只想测试特定串口，可以单独调用：

```c
/* 只测试USART1 */
USART_Test_Init();
USART_Test_Phase1_Basic_TX(&huart1, "USART1 Test");
USART_Test_PrintStats(&huart1);
```

---

## 🐛 常见问题与解决

### 问题1: 没有测试输出

**可能原因**:
1. USART1未正确连接到PC
2. 波特率不匹配
3. 测试代码被注释

**解决方法**:
```c
// 确认main.c中已启用测试
USART_Test_RunAll();  // 确保未注释

// 检查usart.c中USART1配置
huart1.Init.BaudRate = 115200;  // 应与终端一致
```

---

### 问题2: RS485方向控制不正常

**症状**: 只能发或只能收

**检查点**:
```c
// 确认GPIO引脚定义正确 (main.h)
#define USART1_RE_Pin GPIO_PIN_11
#define USART1_RE_GPIO_Port GPIOA

// 确认RS485控制函数工作正常
// 在 usart_test.c 中的 RS485_Enable_TX/RX 函数
```

**调试方法**:
用示波器或逻辑分析仪观察PA11引脚：
- 发送时应为高电平
- 接收时应为低电平

---

### 问题3: DMA传输失败

**可能原因**:
1. DMA未正确初始化
2. 内存对齐问题
3. 缓冲区大小不足

**解决方法**:
```c
// 确认DMA已使能 (dma.c)
__HAL_RCC_DMA1_CLK_ENABLE();
__HAL_RCC_DMA2_CLK_ENABLE();

// 确认缓冲区足够大
#define USART_TEST_BUF_SIZE 256  // 至少大于测试数据
```

---

### 问题4: 数据乱码或丢失

**检查清单**:
- [ ] 波特率是否精确匹配（115200）
- [ ] 时钟配置是否正确（HSE + PLL）
- [ ] 是否存在中断冲突
- [ ] RS485总线是否有终端电阻（120Ω）

---

## 📈 性能指标参考

| 测试项目 | 预期结果 | 实际测量 |
|---------|---------|---------|
| 单字节发送延时 | < 1ms | - |
| 200字节发送时间 | ~20ms | - |
| DMA传输CPU占用 | < 5% | - |
| RS485切换时间 | 1-2ms | - |
| 稳定性测试错误率 | 0% | - |

---

## 🔬 高级调试技巧

### 1. 使用逻辑分析仪

抓取以下信号：
- USART1 TX (PA9)
- USART1 RX (PA10)
- USART1 RE/DE (PA11)

观察：
- RE/DE切换时序
- 数据帧格式
- 起始/停止位

### 2. 添加断点调试

```c
// 在关键位置添加断点
TestResult_t USART_Test_Phase1_Basic_TX(...)
{
    // ← 在此处设置断点
    HAL_UART_Transmit(huart, ...);
    // ← 在此处检查返回值
}
```

### 3. 启用HAL库调试

```c
// 在 main.h 或 stm32f4xx_hal_conf.h 中
#define USE_FULL_ASSERT
#define DEBUG

// 然后在代码中添加
assert_param(huart != NULL);
```

---

## 📝 测试报告模板

```
========================================
  串口测试报告
========================================
测试日期: 2026-XX-XX
测试人员: XXX
硬件版本: STM32F407 + DP83848 + ADS8354

【Phase 1】基础发送测试
  USART1: □ 通过 □ 失败
  USART2: □ 通过 □ 失败
  USART3: □ 通过 □ 失败

【Phase 2】基础接收测试
  USART1: □ 通过 □ 失败
  
【Phase 3】DMA测试
  USART1: □ 通过 □ 失败
  
【Phase 4】RS485回环测试
  USART1: □ 通过 □ 失败
  
【Phase 5】稳定性测试 (60s)
  USART1: □ 通过 □ 失败
  迭代次数: _____
  错误次数: _____

【总体结论】
□ 全部通过
□ 部分通过（详见备注）
□ 失败

【备注】
_______________________________________
_______________________________________
========================================
```

---

## 🎓 相关知识

### RS485半双工原理
- **DE (Driver Enable)**: 高电平时使能发送驱动器
- **RE (Receiver Enable)**: 低电平时使能接收器
- 通常DE和RE连接在一起，用一个GPIO控制
- 发送前拉高，发送完成后拉低切换到接收模式

### DMA优势
- 减少CPU负担
- 适合大数据量传输
- 后台自动完成数据传输

### 超时机制
```c
HAL_UART_Transmit(huart, data, len, timeout_ms);
// timeout设为HAL_MAX_DELAY表示无限等待
// 建议设置合理超时避免死锁
```

---

## 📞 技术支持

如遇到问题，请检查：
1. CubeMX配置是否正确导出
2. HAL库版本是否兼容
3. 编译器优化等级（建议-O0用于调试）

祝测试顺利！🚀
