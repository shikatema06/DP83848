# SOEM 编译与运行实验报告

> **日期：** 2026-06-04
> **环境：** Windows 11 (MINGW64) + STM32F407 + DP83848 项目
> **目标：** 在 Windows 平台编译 SOEM (Simple Open EtherCAT Master) 并运行 simple_ng 示例

---

## 目录

1. [环境准备](#1-环境准备)
2. [SOEM 源码结构分析](#2-soem-源码结构分析)
3. [编译过程](#3-编译过程)
4. [运行测试](#4-运行测试)
5. [关键概念总结](#5-关键概念总结)

---

## 1. 环境准备

### 1.1 硬件环境

| 项目 | 配置 |
|------|------|
| CPU | x86_64 |
| 内存 | 16 GB |
| 网卡 | 需支持 WinPcap/Npcap 驱动 |

### 1.2 软件环境

| 工具 | 版本 | 用途 |
|------|------|------|
| CMake | 3.28.1 | 构建系统生成器 |
| MinGW GCC | 15.2.0 | C 编译器 (x86_64-win32-seh) |
| WinPcap/Npcap | — | Windows 原始以太网帧收发 |
| SOEM | 2.0.0 | 开源 EtherCAT 主站库 |

**操作：检查编译器和构建工具**

```bash
$ cmake --version
cmake version 3.28.1

$ gcc --version
gcc.exe (x86_64-win32-seh-rev0, Built by MinGW-Builds project) 15.2.0
```

**原理说明：**
- SOEM 是纯 C 项目，需要 C11 标准编译器
- MinGW 是 Windows 上的 GCC 移植版，可以编译出原生 Windows exe
- WinPcap/Npcap 提供了用户态程序直接收发以太网帧的能力（绕过 TCP/IP 协议栈），这是 EtherCAT 主站必需的一一环——EtherCAT 不使用 TCP/UDP，而是直接在以太网帧上运行

---

## 2. SOEM 源码结构分析

### 2.1 整体架构

SOEM 采用分层架构设计，从下到上为：

```
┌──────────────────────────────────┐
│         应用层 (samples)          │  ← simple_ng.c, slaveinfo.c
├──────────────────────────────────┤
│        SOEM 核心 API              │  ← ec_main.c, ec_config.c, ec_coe.c ...
├──────────────────────────────────┤
│  OSAL (操作系统抽象层)            │  ← osal/win32/osal.c
├──────────────────────────────────┤
│  OSHW (硬件/网络接口层)          │  ← oshw/win32/oshw.c, nicdrv.c
├──────────────────────────────────┤
│  操作系统 + 网卡驱动              │  ← WinPcap / Windows Socket
└──────────────────────────────────┘
```

**各层职责：**

| 层 | 目录 | 职责 |
|----|------|------|
| **核心库** | `src/` | EtherCAT 协议实现：状态机、CoE、FoE、DC 等 |
| **头文件** | `include/soem/` | 所有公有 API 声明 |
| **OSAL** | `osal/<platform>/` | 封装线程、信号量、定时器、互斥锁 |
| **OSHW** | `oshw/<platform>/` | 封装网络接口初始化、原始帧收发 |

### 2.2 关键文件

| 文件 | 在移植时需要关注？ | 说明 |
|------|:---:|------|
| `src/ec_main.c` | ⚠️ 少量修改 | 主站初始化和主循环逻辑 |
| `src/ec_base.c` | ❌ 不用动 | 以太网帧基础操作 |
| `src/ec_config.c` | ❌ 不用动 | 从站配置和枚举 |
| `src/ec_coe.c` | ❌ 不用动 | CoE 协议实现 |
| `src/ec_dc.c` | ❌ 不用动 | 分布时钟 |
| `osal/win32/osal.c` | **🔄 需重写** | 替换为 FreeRTOS 版本 |
| `oshw/win32/nicdrv.c` | **🔄 需重写** | 替换为 STM32 MAC + DMA 直接收发 |
| `oshw/win32/oshw.c` | **🔄 需重写** | 替换为 STM32 硬件初始化 |

### 2.3 示例程序

| 示例 | 路径 | 学习优先级 |
|------|------|:---------:|
| **simple_ng** | `samples/simple_ng/simple_ng.c` | ⭐ 最高——最简主站示例 |
| slaveinfo | `samples/slaveinfo/` | ⭐ 次之——扫描从站信息 |
| eepromtool | `samples/eepromtool/` | 进阶——EEPROM 操作 |
| firm_update | `samples/firm_update/` | 进阶——固件升级 (FoE) |

---

## 3. 编译过程

### 步骤 1：创建构建目录

**操作：**

```bash
cd /c/Users/hhqk/Downloads/SOEM-master/SOEM-master
mkdir -p build
```

**原因：** CMake 推荐 Out-of-Source 构建（在源码目录外建 build 目录），避免编译产生的中间文件污染源码目录。

### 步骤 2：CMake 配置

**操作：**

```bash
cmake -B build -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSOEM_BUILD_SAMPLES=ON
```

**参数说明：**

| 参数 | 值 | 含义 |
|------|----|------|
| `-B build` | — | 指定构建目录为 build |
| `-G "MinGW Makefiles"` | — | 生成 MinGW 的 Makefile（而非 Visual Studio 方案） |
| `-DCMAKE_BUILD_TYPE=RelWithDebInfo` | — | 发布模式但带调试符号 |
| `-DSOEM_BUILD_SAMPLES=ON` | ON | 编译示例程序（包含 simple_ng） |

**原理说明：**
- CMake 是一个跨平台构建系统生成器，它不直接编译代码，而是根据平台生成对应的构建文件（Makefile、Visual Studio 方案等）
- `-G` 指定生成器类型，MinGW 对应 "MinGW Makefiles"
- CMake 在配置阶段会检测编译器、链接器、头文件和库文件是否存在

### 步骤 3：执行编译

**操作：**

```bash
cd /c/Users/hhqk/Downloads/SOEM-master/SOEM-master
cmake --build build -j 8
```

**实际输出：**

```
[ 3%] Building C object CMakeFiles/soem.dir/src/ec_base.obj
[ 7%] Building C object CMakeFiles/soem.dir/src/ec_coe.obj
[11%] Building C object CMakeFiles/soem.dir/src/ec_config.obj
[14%] Building C object CMakeFiles/soem.dir/src/ec_dc.obj
[18%] Building C object CMakeFiles/soem.dir/src/ec_eoe.obj
[22%] Building C object CMakeFiles/soem.dir/src/ec_foe.obj
[25%] Building C object CMakeFiles/soem.dir/src/ec_main.obj
[29%] Building C object CMakeFiles/soem.dir/src/ec_print.obj
[33%] Building C object CMakeFiles/soem.dir/src/ec_soe.obj
[37%] Building C object CMakeFiles/soem.dir/osal/win32/osal.obj
[40%] Building C object CMakeFiles/soem.dir/oshw/win32/oshw.obj
[44%] Building C object CMakeFiles/soem.dir/oshw/win32/nicdrv.obj
[48%] Linking C static library libsoem.a             ← SOEM 核心库
[48%] Built target soem
[51%] Building C object samples/eepromtool/...
[55%] Building C object samples/ec_sample/...
[59%] Building C object samples/firm_update/...
[62%] Building C object samples/simple_ng/...         ← 最简主站示例
[66%] Building C object samples/slaveinfo/...         ← 从站信息扫描工具
[70%] Building C object samples/eni_test/...
...
[100%] Built target slaveinfo
```

**结果：编译成功，零错误零警告。** 共生成 1 个静态库 + 6 个可执行文件。

| 库 | 作用 |
|----|------|
| **wpcap.lib** | WinPcap 抓包库，提供 pcap_sendpacket() 等函数 |
| **Packet.lib** | WinPcap 底层驱动接口 |
| **ws2_32.lib** | Windows Socket 2.0 API |
| **winmm.lib** | Windows 多媒体定时器（高精度时间） |

---

## 4. 运行测试

### 步骤 4：运行 simple_ng（列出网卡）

**操作：**

```bash
cd /c/Users/hhqk/Downloads/SOEM-master/SOEM-master/build
./samples/simple_ng/simple_ng.exe
```

**实际输出：**

```
Usage: simple_ng IFNAME1
IFNAME1 is the NIC interface name, e.g. 'eth0'

Available adapters:
    - \Device\NPF_{111894AF-...}  (WAN Miniport (Network Monitor))
    - \Device\NPF_{A67E9F5E-...}  (WAN Miniport (IPv6))
    - \Device\NPF_{F8821C39-...}  (WAN Miniport (IP))
    - \Device\NPF_{DA171CBE-...}  (Bluetooth Device (Personal Area Network))
    - \Device\NPF_{2B9294F5-...}  (Intel(R) Wi-Fi 6 AX201 160MHz)
    - ...
    - \Device\NPF_{BF164A9A-...}  (Intel(R) Ethernet Connection (18) I219-V)
```

**说明：**
- simple_ng 需要传入一个 WinPcap 网卡 ID 作为参数
- `ec_find_adapters()` 函数通过 WinPcap 枚举系统中所有可用网卡
- 这台机器上有线网卡为：`Intel(R) Ethernet Connection (18) I219-V`
- 可通过 `simple_ng "\Device\NPF_{BF164A9A-...}"` 指定网卡连接从站

### 步骤 5：运行 slaveinfo

**操作：**

```bash
./samples/slaveinfo/slaveinfo.exe
```

**输出：**

```
SOEM (Simple Open EtherCAT Master)
Slaveinfo
Usage: slaveinfo ifname [options]
Options :
 -sdo : print SDO info
 -map : print mapping

Available adapters:
    ...（与 simple_ng 相同的网卡列表）
```

**说明：** slaveinfo 可以扫描并显示从站的详细信息，包括厂商 ID、产品码、PDO 映射等，是调试 EtherCAT 网络的利器。

---

## 5. 关键概念总结

### 5.1 SOEM 的主站初始化流程

每次 `fieldbus_start()` 调用的背后：

```
① ecx_init()
    ├─ oshw/nicdrv.c: 打开网卡，绑定 WinPcap
    └─ 设置过滤规则（只收 EtherCAT 帧类型 0x88A4）

② ecx_config_init()
    ├─ 广播发送 ALRD（Auto Increment Read）命令
    ├─ 逐站读取从站信息（厂商ID、产品码、类型）
    └─ 填充 context->slavelist[]

③ ecx_config_map_group()
    ├─ 分析各从站的 PDO 映射需求
    ├─ 分配连续的输入/输出内存区域
    └─ 配置从站的 FMMU（现场总线内存管理单元）

④ ecx_configdc()
    └─ 配置分布时钟参数（同步模式）

⑤ 状态切换：Init → Pre-Op → Safe-Op → Op
    └─ 每个阶段从站完成特定的初始化任务
```

### 5.2 EtherCAT 核心概念

| 概念 | 说明 |
|------|------|
| **WKC (Working Counter)** | 工作计数器。每一帧经过一个从站，计数器 +1。用于检测通信是否正常 |
| **PDO (Process Data Object)** | 过程数据对象。周期性的实时数据交换（如输入/输出） |
| **SDO (Service Data Object)** | 服务数据对象。非周期的配置数据访问（类似寄存器读写） |
| **CoE (CANopen over EtherCAT)** | 通过 EtherCAT 传输 CANopen 协议 |
| **DC (Distributed Clock)** | 分布时钟，精确同步所有从站的时间（纳秒级） |
| **FMMU** | 现场总线内存管理单元，将从站本地内存映射到 EtherCAT 帧中的特定位置 |

### 5.4 EtherCAT 帧结构

EtherCAT 数据直接封装在以太网帧中（不使用 IP/UDP/TCP）：

```
┌─────────────────────────────────────────────────────────┐
│ Ethernet Header (14B)                                   │
│  ├─ MAC 目的地址 (6B)  ← 广播或多播                       │
│  ├─ MAC 源地址 (6B)    ← 主站 MAC                         │
│  └─ EtherType (2B)     ← 0x88A4 (EtherCAT 专属类型)       │
├─────────────────────────────────────────────────────────┤
│ EtherCAT Header (2B)                                     │
│  ├─ 长度 (11b)                                           │
│  └─ 保留 + 类型 (5b)                                      │
├─────────────────────────────────────────────────────────┤
│ EtherCAT 数据报 (一个帧可包含多个数据报)                    │
│  ├─ 数据报头 (10B)     ← 命令、索引、地址等                  │
│  ├─ WKC (2B)          ← 工作计数器                        │
│  └─ 数据 (最多 1486B)  ← PDO 数据 / SDO 数据               │
└─────────────────────────────────────────────────────────┘
```

**关键点：** 这是为什么普通的以太网 MAC（如 F407 内置的）可以收发 EtherCAT 帧——它看到的是标准以太网帧，只是 `EtherType=0x88A4`。EtherCAT 协议的处理在数据链路层之上由软件完成。

### 5.5 simple_ng 代码流程详解

`simple_ng.c` 是 SOEM 的最简示例，整个主站的生命周期分为三个阶段：

#### 阶段一：初始化（`fieldbus_start()`）

```
main()
  └─ fieldbus_initialize()        清零 Fieldbus 结构体
  └─ fieldbus_start()             启动主站
      ├─ ecx_init()               打开网卡，建立 WinPcap socket
      ├─ ecx_config_init()        扫描总线，发现从站
      ├─ ecx_config_map_group()   映射 PDO 内存
      ├─ ecx_configdc()           配置分布时钟
      ├─ ecx_statecheck(SAFE_OP)  等待所有从站进入安全运行态
      ├─ fieldbus_roundtrip()     发送一次空数据
      └─ 循环切 OP 状态           → 成功后返回 TRUE
```

#### 阶段二：循环运行（`fieldbus_dump()` + `fieldbus_check_state()`）

```
for (i = 1; i <= 10000; ++i)
{
  fieldbus_dump(&fieldbus)
  │  ├─ ecx_send_processdata()     ← 打包输出数据 → 以太网帧
  │  ├─ ecx_receive_processdata()  ← 等待并接收从站回复
  │  └─ 计算 WKC、打印数据
  │
  └─ fieldbus_check_state(&fieldbus)  [仅当 WKC 异常时]
      ├─ ecx_readstate()           ← 读取所有从站实际状态
      └─ 针对不同错误状态做不同处理
         ├─ ERROR → ACK
         ├─ SAFE_OP → 切回 OP
         ├─ 其他 → reconfig
         └─ lost → recover

  osal_usleep(5000)                 ← 5ms 周期（200Hz）
}
```

#### 阶段三：停止（`fieldbus_stop()`）

```
fieldbus_stop(&fieldbus)
  ├─ 所有从站 → INIT 状态
  └─ ecx_close()                   ← 关闭 socket
```

### 5.6 关键数据结构：`ecx_contextt`

这是 SOEM 的"灵魂"结构体，所有 API 都通过它操作：

```c
typedef struct {
    ec_slavet    *slavelist;    // 从站列表[0..slavecount]
    ec_groupt    *grouplist;    // 分组列表
    ec_eringt    *elist;        // 错误事件列表
    ec_idxstackT *idxstack;     // 帧索引栈
    
    int          slavecount;    // 发现的从站数量
    int          maxslave;      // 最大从站数
    uint64       DCtime;        // 分布时钟时间
    
    // 底层接口（这就是移植时需要实现的）
    ecx_portt    port;          // ← 包含 4 个函数指针
} ecx_contextt;
```

### 5.7 移植关键：`ecx_portt` 结构体

```c
typedef struct {
    // 这些函数指针就是移植时要填的
    ecx_writedatagramt   txdg;       // 发送一个数据报
    ecx_readdatagramt    rxdg;       // 接收一个数据报
    ecx_setupmemoryt     setbuf;     // 设置帧缓冲区
    ecx_adddatagramt     adddg;      // 添加数据报到帧中
} ecx_portt;
```

移植 SOEM 到 STM32 的本质就是：**替换这 4 个函数的实现**。
- Windows 版 → 用 WinPcap 收发包
- STM32版 → 用 F407 MAC + DMA 直接读写以太网帧

### 5.8 Windows 版与 STM32 版架构对比

| 层次 | Windows 版 | STM32 移植版 |
|------|-----------|-------------|
| OSAL | `osal/win32/osal.c` — 用 Windows API | → **FreeRTOS**：`vTaskDelay()`, `xSemaphoreCreate()` |
| NIC | `oshw/win32/nicdrv.c` — 用 WinPcap | → **HAL_ETH**：直接操作 MAC 描述符 |
| OSHW | `oshw/win32/oshw.c` — 枚举网卡 | → 固定 MAC 地址 + PHY 初始化 |
| 构建 | MinGW GCC + WinPcap .lib | → ARM GCC + HAL 库 |

## 6. 总结与下一步计划

### 6.1 本次完成的工作

| 编号 | 任务 | 状态 |
|:---:|------|:---:|
| 1 | SOEM 源码结构分析 | ✅ 完成 |
| 2 | Windows 构建环境搭建 | ✅ 完成 |
| 3 | SOEM 核心库编译 | ✅ 完成 |
| 4 | 示例程序编译 (simple_ng, slaveinfo 等) | ✅ 完成 |
| 5 | simple_ng 程序运行验证 | ✅ 完成 |
| 6 | SOEM 代码流程分析 | ✅ 完成 |

### 6.2 下一阶段计划

| 阶段 | 任务 | 预计时间 | 前置条件 |
|:----:|------|:-------:|:--------:|
| **①** | 安装 TwinCAT，学习使用虚拟从站模式 | 1 周 | — |
| **②** | 在 TwinCAT 上配置虚拟从站，用 SOEM simple_ng 连接验证 | 1 周 | 完成 ① |
| **③** | 结合 STM32F407 的 ETH 驱动，分析 `oshw/win32/nicdrv.c` 的实现 | 1 周 | — |
| **④** | 移植 OSAL 层（osal.c）到 FreeRTOS | 1 周 | 熟悉 FreeRTOS API |
| **⑤** | 移植 NIC 驱动层（nicdrv.c）到 STM32 HAL ETH | 2-3 周 | 完成 ③ ④ |
| **⑥** | 在 F407 板子上跑通 simple_ng 扫描 TwinCAT 从站 | 1 周 | 完成 ⑤ |

### 6.3 涉及的投资估算（参考）

| 项目 | 估算 |
|------|:----:|
| 软件 | 全免费（SOEM: GPLv3, TwinCAT: 免费版, CMake: 免费） |
| 学习时间 | 约 2-3 个月达到可独立移植水平 |
| 硬件（如需额外从站模块） | ¥100-300/个（淘宝 EtherCAT IO 模块） |

---

## 附录

### A. 编译日志（完整）

```
$ cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSOEM_BUILD_SAMPLES=ON
-- Current build type is: RelWithDebInfo
-- Current install path is: C:/.../build/install
-- Building for Windows
-- Configuring done (7.3s)
-- Generating done (0.2s)
-- Build files have been written to: C:/.../SOEM-master/build

$ cmake --build build -j 8
[ 3%] Building C object CMakeFiles/soem.dir/src/ec_base.obj
[ 7%] Building C object CMakeFiles/soem.dir/src/ec_coe.obj
[11%] Building C object CMakeFiles/soem.dir/src/ec_config.obj
[14%] Building C object CMakeFiles/soem.dir/src/ec_dc.obj
[18%] Building C object CMakeFiles/soem.dir/src/ec_eoe.obj
[22%] Building C object CMakeFiles/soem.dir/src/ec_foe.obj
[25%] Building C object CMakeFiles/soem.dir/src/ec_main.obj
[29%] Building C object CMakeFiles/soem.dir/src/ec_print.obj
[33%] Building C object CMakeFiles/soem.dir/src/ec_soe.obj
[37%] Building C object CMakeFiles/soem.dir/osal/win32/osal.obj
[40%] Building C object CMakeFiles/soem.dir/oshw/win32/oshw.obj
[44%] Building C object CMakeFiles/soem.dir/oshw/win32/nicdrv.obj
[48%] Linking C static library libsoem.a
[48%] Built target soem
[51%] Building C object samples/eepromtool/CMakeFiles/eepromtool.dir/eepromtool.obj
[55%] Building C object samples/ec_sample/CMakeFiles/ec_sample.dir/ec_sample.obj
[59%] Building C object samples/firm_update/CMakeFiles/firm_update.dir/firm_update.obj
[62%] Building C object samples/simple_ng/CMakeFiles/simple_ng.dir/simple_ng.obj
[66%] Building C object samples/slaveinfo/CMakeFiles/slaveinfo.dir/slaveinfo.obj
[70%] Building C object samples/eni_test/CMakeFiles/eni_test.dir/eni_test.obj
[74%] Building C object samples/eni_test/CMakeFiles/eni_test.dir/sample-eni.obj
[77%] Linking C executable firm_update.exe
[81%] Linking C executable eni_test.exe
[85%] Linking C executable ec_sample.exe
[88%] Linking C executable simple_ng.exe
[92%] Linking C executable eepromtool.exe
[96%] Linking C executable slaveinfo.exe
[96%] Built target eepromtool
[96%] Built target ec_sample
[96%] Built target firm_update
[96%] Built target simple_ng
[100%] Built target eni_test
[100%] Built target slaveinfo
```

### B. 生成的可执行文件

| 文件 | 大小 | 功能 |
|------|:----:|------|
| `libsoem.a` | ~200 KB | SOEM 静态库 |
| `simple_ng.exe` | 444 KB | 最简 EtherCAT 主站示例 |
| `slaveinfo.exe` | 466 KB | 从站信息扫描 |
| `ec_sample.exe` | 447 KB | 功能更丰富的主站示例 |
| `eepromtool.exe` | — | 从站 EEPROM 读写工具 |
| `firm_update.exe` | — | 固件升级 (FoE) |
| `eni_test.exe` | — | ENI (EtherCAT Network Information) 测试 |
