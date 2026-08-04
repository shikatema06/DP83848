# 第 1 周精确每日计划 — 基础夯实

> **基于:** `twincat-freertos-claude-code-iterative-quill.md`
> **项目:** `c:/ST_project/DP83848_FreeRTOS/DP83848/`
> **硬件:** STM32F407IGT6 + DP83848 PHY (MII), 168MHz, 128K RAM + 64K CCMRAM
> **SOEM:** `c:/ST_project/DP83848_FreeRTOS/SOEM/SOEM-master/`
> **当前状态:** 裸机 LwIP RAW API (NO_SYS=1), 无 FreeRTOS, ETH 轮询模式, 有 TCP echo server

---

## 每日格式说明

每 30 分钟为单位，精确到要打开什么文件、写什么代码、运行什么命令。每个时间段结束时都有明确的 **可验证输出**。

---

## Day 1 — FreeRTOS 任务与调度（4.5h）

**目标:** 下载 FreeRTOS 源码 → 创建第一个任务 → 理解抢占式调度和 vTaskDelay

### ▎时段 1: 项目准备 + 下载 FreeRTOS（30 min）

**步骤 1.1** 打开终端到项目根目录:
```bash
cd /c/ST_project/DP83848_FreeRTOS/DP83848/
```

**步骤 1.2** 下载 FreeRTOS 202212.01 LTS 源码:
```bash
mkdir -p Middlewares/Third_Party/FreeRTOS
# 从官方 GitHub 下载 (如果网络受限, 从已有文件复制)
git -C /tmp clone --branch V10.5.1 --depth 1 \
    https://github.com/FreeRTOS/FreeRTOS-Kernel.git
cp -r /tmp/FreeRTOS-Kernel/* Middlewares/Third_Party/FreeRTOS/
```

或者手动从 `<project>/SOEM/` 同级或已有文件复制。

**步骤 1.3** 确认 FreeRTOS 核心文件存在:
```bash
ls Middlewares/Third_Party/FreeRTOS/tasks.c
ls Middlewares/Third_Party/FreeRTOS/include/FreeRTOS.h
```
✅ **验证:** 能看到 `tasks.c`, `list.c`, `queue.c`, `timers.c` 和 `include/`

### ▎时段 2: 创建 FreeRTOSConfig.h（45 min）

**步骤 2.1** 创建 `Core/Inc/FreeRTOSConfig.h`:

```c
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "stm32f4xx_hal.h"

/*-----------------------------------------------------------
 * 针对 STM32F407 @168MHz 的配置
 *-----------------------------------------------------------*/

/* 基础配置 */
#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      ((unsigned long)168000000)
#define configTICK_RATE_HZ                      ((TickType_t)1000)   /* 1ms tick */
#define configMAX_PRIORITIES                    (5)
#define configMINIMAL_STACK_SIZE                ((unsigned short)128)
#define configTOTAL_HEAP_SIZE                   ((size_t)(20 * 1024)) /* 20KB heap */
#define configMAX_TASK_NAME_LEN                 (16)
#define configUSE_TRACE_FACILITY                1
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

/* 同步原语 */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0
#define configQUEUE_REGISTRY_SIZE               8

/* 任务通知 */
#define configUSE_TASK_NOTIFICATIONS            1

/* 内存管理 */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1

/* 协程 (不用) */
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         0

/* 定时器 */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (2)
#define configTIMER_QUEUE_LENGTH                5
#define configTIMER_TASK_STACK_DEPTH            (configMINIMAL_STACK_SIZE)

/* 中断优先级 */
#define configKERNEL_INTERRUPT_PRIORITY         255
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    5   /* 优先级 >5 的 ISR 可使用 FreeRTOS API */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

/* Cortex-M4 特定 */
#define configENABLE_FPU                        1
#define configENABLE_MPU                        0
#define configENABLE_TRUSTZONE                  0
#define configENABLE_PAUSE                      0
#define configENABLE_ACCESS_CONTROL_LIST        0

/* 可选: 栈溢出检测 */
#define INCLUDE_vTaskStackOverflowCheck         1
#define configCHECK_FOR_STACK_OVERFLOW          2

/* API 包含 */
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xSemaphoreGetMutexHolder        1

/* 外部定时器 */
extern void vPortSetupTimerInterrupt(void);
#define configSETUP_TICK_INTERRUPT()            vPortSetupTimerInterrupt()

#endif /* FREERTOS_CONFIG_H */
```

**步骤 2.2** 创建 `Core/Src/FreeRTOS_tick.c` — SysTick 定时器配置:
FreeRTOS Kernel V10.5.1 的 `port.c` 会自动配置 SysTick，但我们用 TIM4 做 HAL 时基、FreeRTOS 独占 SysTick:
```c
// 这个文件的内容是: FreeRTOS Kernel 的 portable/GCC/ARM_CM4F/port.c 自带 SysTick 配置
// 所以不需要额外文件。我们只需确保 HAL 的时基不是 SysTick。
```

✅ **验证:** 头文件能被 `#include` 测试编译通过

### ▎时段 3: 创建 freertos_tasks.c — 2 个 LED 任务（45 min）

**步骤 3.1** 创建 `Core/Src/freertos_tasks.c`:

```c
/* freertos_tasks.c — FreeRTOS 任务示例 (Day 1 练习) */
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* 任务句柄 */
TaskHandle_t xHighTaskHandle = NULL;
TaskHandle_t xLowTaskHandle = NULL;

/* 高优先级任务: 100ms 翻转一次 */
void vHighPriorityTask(void *pvParameters)
{
    (void)pvParameters;
    uint32_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        /* 翻转一个 GPIO (用空闲的 PC0 或 PB0 接 LED) */
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0);  /* 假设 PB0 有 LED */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
    }
}

/* 低优先级任务: 500ms 翻转一次 */
void vLowPriorityTask(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_1);  /* 假设 PB1 有 LED */
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* 创建两个任务 */
void vApplicationTasksCreate(void)
{
    xTaskCreate(vHighPriorityTask, "High", configMINIMAL_STACK_SIZE, NULL, 2, &xHighTaskHandle);
    xTaskCreate(vLowPriorityTask,  "Low",  configMINIMAL_STACK_SIZE, NULL, 1, &xLowTaskHandle);
}
```

**步骤 3.2** 创建临时头文件 `Core/Inc/freertos_tasks.h`:
```c
#ifndef FREERTOS_TASKS_H
#define FREERTOS_TASKS_H
void vApplicationTasksCreate(void);
#endif
```

### ▎时段 4: 整合进 CMakeLists.txt — 编译通过（60 min）

**步骤 4.1** 编辑 `cmake/stm32cubemx/CMakeLists.txt`:

在 `MX_Include_Dirs` 末尾加入:
```
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/include
```

在 `MX_Application_Src` 末尾加入:
```
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/freertos_tasks.c
```

新增 FreeRTOS 源码列表块:
```cmake
# FreeRTOS kernel sources (list only needed files)
set(FreeRTOS_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/tasks.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/queue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/timers.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/event_groups.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/portable/GCC/ARM_CM4F/port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/portable/MemMang/heap_4.c
)

# Create FreeRTOS OBJECT library
add_library(FreeRTOS OBJECT)
target_sources(FreeRTOS PRIVATE ${FreeRTOS_Src})
target_link_libraries(FreeRTOS PUBLIC stm32cubemx)

# Link to main target
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE FreeRTOS)
```

**步骤 4.2** 在 `stm32cubemx` 的 `target_link_libraries` 链接中添加 `FreeRTOS`:
```cmake
set(MX_LINK_LIBS 
    STM32_Drivers
    ${TOOLCHAIN_LINK_LIBRARIES}
    LwIP
    FreeRTOS
)
```

**步骤 4.3** 修改 `Core/Src/main.c`:

在 `main()` 末尾的 `while(1)` 之前插入:
```c
  /* 创建 FreeRTOS 任务 */
  vApplicationTasksCreate();

  /* 启动调度器 (不会返回) */
  vTaskStartScheduler();

  /* 如果运行到这里, 说明堆空间不足 */
  Error_Handler();
```

并删除原有的 `while(1)` 循环。

**步骤 4.4** 编译测试:
```bash
cd /c/ST_project/DP83848_FreeRTOS/DP83848
cmake --preset default  # 如果配置了 CMakePresets
# 或
cmake -B build -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake
cmake --build build
```

**⚠️ 可能遇到的问题:**
- ❌ `FreeRTOSConfig.h` not found → 确认 `Core/Inc` 在 include 路径中
- ❌ `SVC_Handler`/`PendSV_Handler`/`SysTick_Handler` 冲突 → `stm32f4xx_it.c` 中这三个 Handler 为空，FreeRTOS 的 `port.c` 会使用弱符号覆盖，但 `stm32f4xx_it.c` 中的定义不是 weak → 需要注释掉它们
- ❌ 链接错误 `undefined reference to vTaskStartScheduler` → 检查 FreeRTOS 源文件是否正确包含

### ▎时段 5: 修复中断冲突（30 min）

**步骤 5.1** 在 `stm32f4xx_it.c` 中注释掉以下三个函数（保留空定义但加 `__attribute__((weak))` 或直接删除）:
- `SVC_Handler` — FreeRTOS 的 port.c 用弱符号覆盖
- `PendSV_Handler` — FreeRTOS 的 port.c 用弱符号覆盖  
- `SysTick_Handler` — FreeRTOS 接管

**SysTick 特殊处理:** 由于 `HAL_IncTick()` 原来在 `SysTick_Handler` 中调用，现在 HAL 时基改为 TIM4:
```c
// stm32f4xx_it.c 中
// 删除或注释掉:
// void SysTick_Handler(void) { HAL_IncTick(); }
//
// 而 TIM4_IRQHandler 已经在调用 HAL_TIM_IRQHandler(&htim4)
// → HAL_TIM_PeriodElapsedCallback → HAL_IncTick()
// 所以 HAL 时基不受影响
```

### ▎时段 6: 串口打印验证（45 min）

**步骤 6.1** 创建调试打印任务:
在 `freertos_tasks.c` 中添加:
```c
#include "usart.h"
#include <stdio.h>

void vDebugTask(void *pvParameters)
{
    (void)pvParameters;
    uint32_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        printf("FreeRTOS started, 3 tasks running\r\n");
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(2000));
    }
}

// 在 vApplicationTasksCreate 中增加:
// xTaskCreate(vDebugTask, "Debug", configMINIMAL_STACK_SIZE*2, NULL, 0, NULL);
```

**步骤 6.2** 实现 `__io_putchar` 串口输出（如果还没有）:
在 `main.c` 的 `USER CODE BEGIN 0` 区域:
```c
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
```

**步骤 6.3** 确保 `-u _printf_float` 或不用浮点 `printf`。

### ▎Day 1 结束检查清单 ✅
- [ ] FreeRTOS 源码已下载到 `Middlewares/Third_Party/FreeRTOS/`
- [ ] `FreeRTOSConfig.h` 已创建
- [ ] `freertos_tasks.c` 有 3 个任务 (High/Low/Debug)
- [ ] CMake 编译通过, `.elf` 生成
- [ ] 烧录后串口输出: `FreeRTOS started, 3 tasks running`
- [ ] LED 以 100ms / 500ms 交替闪烁

---

## Day 2 — FreeRTOS 同步与通信（4.5h）

**目标:** 掌握信号量、任务通知、队列 — 为 ETH 中断→任务同步做准备

### ▎时段 1: 信号量基础（45 min）

**步骤 1.1** 在 `freertos_tasks.c` 中添加:
```c
#include "semphr.h"

SemaphoreHandle_t xBinarySemaphore = NULL;
```

**步骤 1.2** 在 `vApplicationTasksCreate()` 中创建信号量:
```c
xBinarySemaphore = xSemaphoreCreateBinary();
```

**步骤 1.3** 模拟 ISR 给任务: 在 `stm32f4xx_it.c` 的 `TIM7_IRQHandler` 中:
```c
extern SemaphoreHandle_t xBinarySemaphore;
void TIM7_IRQHandler(void)
{
    /* 清除中断标志 (由 HAL 处理时自动清除) */
    HAL_TIM_IRQHandler(&htim7);
    
    /* 给信号量 (从 ISR) */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xBinarySemaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

**步骤 1.4** 创建等待信号量的任务:
```c
void vSemaphoreTask(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        if (xSemaphoreTake(xBinarySemaphore, portMAX_DELAY) == pdTRUE)
        {
            /* TIM7 每 200µs 触发一次 → 翻转 GPIO */
            HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0);
        }
    }
}
```

### ▎时段 2: 计数信号量（30 min）

替代方案 — 用计数信号量跟踪缓冲区占用:
```c
#define RINGBUF_SIZE 16
SemaphoreHandle_t xCountSem;

// 初始化: xCountSem = xSemaphoreCreateCounting(RINGBUF_SIZE, 0);
// ISR 中: xSemaphoreGiveFromISR(xCountSem, ...)  // 通知"有数据"  
// 任务中: xSemaphoreTake(xCountSem, portMAX_DELAY) // 等待数据
```

### ▎时段 3: 任务通知 — ETH 中断最优方案（60 min）

**步骤 3.1** 理解为什么任务通知比信号量快:
- 信号量: 需要创建、维护 SemaphoreHandle_t，ISR→Task 需要 2 次 API 调用
- 任务通知: 直接向 TaskHandle_t 发 32-bit 值，无需额外对象，快 ~30%

**步骤 3.2** 实现任务通知接收任务:
```c
TaskHandle_t xEcatTaskHandle = NULL;

void vEcatProcessingTask(void *pvParameters)
{
    uint32_t ulNotificationValue;
    (void)pvParameters;
    
    for (;;)
    {
        /* 等待 ISR 发来的通知，阻塞直到收到 */
        ulNotificationValue = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        /* 收到通知 → 有以太网帧到达 */
        GPIOB->ODR ^= GPIO_PIN_1;  /* 快速翻转指示 */
        /* 这里稍后放 SOEM 接收处理 */
    }
}
```

**步骤 3.3** 模拟 ISR 通知（稍后 Day 4 改为 ETH IRQ）:
```c
// 在 TIM7_IRQHandler 或其他测试 ISR 中:
BaseType_t xHigherPriorityTaskWoken = pdFALSE;
vTaskNotifyGiveFromISR(xEcatTaskHandle, &xHigherPriorityTaskWoken);
portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
```

### ▎时段 4: 队列实验（45 min）

**步骤 4.1** 创建一个队列，传递时间戳数据:
```c
#include "queue.h"

QueueHandle_t xTimestampQueue = NULL;

// 定义: xTimestampQueue = xQueueCreate(10, sizeof(uint32_t));
// 发送任务:
void vProducerTask(void *pvParameters)
{
    uint32_t last_wake = xTaskGetTickCount();
    uint32_t count = 0;
    for (;;)
    {
        count++;
        xQueueSend(xTimestampQueue, &count, 0);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
    }
}

// 接收任务:
void vConsumerTask(void *pvParameters)
{
    uint32_t received;
    for (;;)
    {
        if (xQueueReceive(xTimestampQueue, &received, portMAX_DELAY) == pdTRUE)
        {
            printf("Queue got: %lu\r\n", received);
        }
    }
}
```

### ▎时段 5: 互斥锁保护共享资源（30 min）

```c
SemaphoreHandle_t xMutex;

void vSharedResourceTask(void *pvParameters)
{
    for (;;)
    {
        if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
        {
            /* 安全访问共享资源 (如 ETH 描述符) */
            printf("Accessing shared resource...\r\n");
            xSemaphoreGive(xMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### ▎时段 6: 验证 Day 1+2 的编译（60 min）

**步骤 6.1** 为 FreeRTOS 添加 `event_groups.c` 到 CMake（可选）:
```cmake
# 在 FreeRTOS_Src 中新增:
${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/FreeRTOS/event_groups.c
```

**步骤 6.2** 完整编译测试:
```bash
cmake -B build -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake
cmake --build build
```

**⚠️ 常见问题:**
- ❌ `undefined reference to vPortYieldFromISR` → 需要 `port.c` 中实现，确认 `portable/GCC/ARM_CM4F/port.c` 已添加
- ❌ `FreeRTOS.h` not found after adding new paths → 重新配置 cmake (`-B build` 清除缓存)
- ❌ `error: #error "FreeRTOSConfig.h must define INCLUDE_..."` → 检查 `FreeRTOSConfig.h` 中的 `INCLUDE_*` 宏

### ▎Day 2 结束检查清单 ✅
- [ ] 二值信号量实验通过 (TIM7 ISR → Take/Give)
- [ ] 任务通知编译通过
- [ ] 队列传递数据测试通过
- [ ] 互斥锁编译通过
- [ ] 串口输出能看到不同任务的打印

---

## Day 3 — STM32 ETH MAC 深入分析（5h）

**目标:** 彻底理解现有 `ethernetif.c` 的 DMA 描述符、接收路径、发送路径

### ▎时段 1: 读 low_level_init — MAC 初始化（60 min）

**分析代码** `LWIP/Target/ethernetif.c:142-244`:

逐行理解这些关键配置:
```c
// 第 147-158 行: ETH 初始化参数
heth.Init.MediaInterface = HAL_ETH_MII_MODE;   // MII 模式
heth.Init.TxDesc = DMATxDscrTab;                // TX 描述符数组 (4 个)
heth.Init.RxDesc = DMARxDscrTab;                // RX 描述符数组 (4 个)
heth.Init.RxBuffLen = 1536;                     // 每个 RX 缓冲区大小
```

**步骤 1.1** 在 `Core/Src/` 中创建 `eth_dump.c` — 读取并打印 ETH 寄存器:
```c
#include "main.h"
#include "stm32f4xx_hal_eth.h"

extern ETH_HandleTypeDef heth;

void ETH_DumpRegisters(void)
{
    printf("=== ETH Register Dump ===\r\n");
    printf("ETH->MACSR   = 0x%08lX\r\n", ETH->MACSR);
    printf("ETH->DMASR   = 0x%08lX\r\n", ETH->DMASR);
    printf("ETH->MACCR   = 0x%08lX\r\n", ETH->MACCR);
    printf("ETH->MACFFR  = 0x%08lX\r\n", ETH->MACFFR);
    
    /* RX 描述符 0 */
    ETH_DMADescTypeDef *rxdesc = &heth.RxDesc[0];
    printf("RX Desc[0] TDES0=0x%08lX TDES1=0x%08lX\r\n", 
           rxdesc->DESC0, rxdesc->DESC1);
    printf("           TDES2=0x%08lX TDES3=0x%08lX\r\n",
           rxdesc->DESC2, rxdesc->DESC3);
    
    printf("=== End ===\r\n");
}
```

**步骤 1.2** 在调试任务中周期调用 `ETH_DumpRegisters()`（每 5 秒一次）。

### ▎时段 2: 读 TX 路径（60 min）

**分析** `low_level_output()` (ethernetif.c:262-299):
- 将 pbuf 链转换成 `ETH_BufferTypeDef` 数组
- 设置 `TxConfig` (checksum, CRC padding)
- 调用 `HAL_ETH_Transmit(&heth, &TxConfig, timeout)`

**图解放入笔记:**
```
pbuf1 → Txbuffer[0] → ETH DMA → MII → DP83848 → 网线
pbuf2 → Txbuffer[1] ┘
```

**需要理解的关键:**
- `ETH_TX_DESC_CNT = 4` — 最多 4 个分片
- `HAL_ETH_Transmit` 内部做了: 等待描述符可用 → 设置 DMA 描述符 → 触发 TX 轮询
- `TxFreeCallback` 在发送完成后释放 pbuf

### ▎时段 3: 读 RX 路径（60 min）

**分析** `low_level_input()` 和两个回调:

1. **`HAL_ETH_RxAllocateCallback`** (ethernetif.c:733-752):
   - 从 LWIP MEMPOOL 分配 `RxBuff_t`
   - 包含 pbuf_custom + 对齐缓冲区
   - 调用 `pbuf_alloced_custom()` 创建零拷贝 pbuf

2. **`HAL_ETH_RxLinkCallback`** (ethernetif.c:756-790):
   - 将多个 DMA 描述符链起来的缓冲区串成 pbuf 链
   - `tot_len` 累计

3. **`low_level_input`** → 调用 `HAL_ETH_ReadData` 获取接收完成的 pbuf

### ▎时段 4: 理解零拷贝 vs 拷贝模式（60 min）

**关键洞察:** SOEM 需要拷贝模式（不是零拷贝）:

| 特征 | 当前 LwIP 方式 (零拷贝) | SOEM 需要的方式 (拷贝) |
|------|----------------------|---------------------|
| 缓冲区 | pbuf 来自 LwIP 内存池 | 固定大小 `ec_bufT[EC_MAXECATFRAME]` |
| 分配时机 | 每个帧到达时分配 | 初始化时预分配 |
| 数据路径 | DMA 直接写入 pbuf | DMA 写入临时缓冲区，拷贝到 SOEM 缓冲区 |
| 释放 | LwIP 自动管理 | 主站显式管理 |

**SOEM 的 `ec_bufT` 定义:**
```c
// SOEM/include/soem/ec_type.h:
#define EC_MAXECATFRAME  1518
#define EC_BUFSIZE       EC_MAXECATFRAME
typedef uint8 ec_bufT[EC_BUFSIZE];
```

### ▎时段 5: ETH 中断分析 + 动手（60 min）

**当前 ETH 中断为什么没工作？**

分析启动文件 `startup_stm32f407xx.s`:
```
.word ETH_IRQHandler       /* 在向量表第 61 位 */
.word ETH_WKUP_IRQHandler  /* 第 62 位 */
```

这两个默认指向 `Default_Handler`（无限循环）。需要我们自己实现。

**修改 `stm32f4xx_it.c`（准备，Day 4 才会实际使用）:**
```c
/* USER CODE BEGIN 1 */

/* ETH 中断服务函数 (Day 4 启用) */
void ETH_IRQHandler(void)
{
    /* 读取中断状态寄存器确定中断源 */
    uint32_t dmasr = ETH->DMASR;
    
    /* 清除 RX 中断标志 */
    if (dmasr & ETH_DMASR_RS)
    {
        ETH->DMASR = ETH_DMASR_RS;  /* 写 1 清除 */
        
        /* 通知 EtherCAT 处理任务 */
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(xEcatTaskHandle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    
    /* 清除其他标志以防止反复触发 */
    ETH->DMASR = dmasr & 0x0000FFFF;  /* 清除所有可写状态位 */
}
```

**需要确认:** NVIC 中 ETH 中断是否使能:
```c
/* 在 main.c 或 ETH 初始化后调用 */
HAL_NVIC_SetPriority(ETH_IRQn, 5, 0);  /* 优先级 5 (在 configMAX_SYSCALL_INTERRUPT_PRIORITY 范围内) */
HAL_NVIC_EnableIRQ(ETH_IRQn);
```

### ▎Day 3 结束检查清单 ✅
- [ ] 理解 `low_level_init` 的每行代码
- [ ] 理解 TX 描述符和 TX 缓冲区如何工作
- [ ] 理解 RX 零拷贝路径和 pbuf 分配/链接
- [ ] 知道 SOEM 为什么需要拷贝模式
- [ ] 知道如何修改 ETH IRQ 使能
- [ ] 完成 `eth_dump.c` 寄存器打印

---

## Day 4 — FreeRTOS + ETH 整合（5h）

**目标:** 将 ETH 改为中断驱动，FreeRTOS 任务处理，3 个任务骨架

### ▎时段 1: 修改 CMakeLists 添加 FreeRTOS + 准备编译（30 min）

**步骤 1.1** 确认 Day 1-2 的 CMakeLists 修改已保存:
- `FreeRTOS` 库已添加
- `Core/Inc/FreeRTOSConfig.h` 存在
- `freertos_tasks.c` 已添加

### ▎时段 2: 创建 3 个任务骨架（90 min）

**步骤 2.1** 重新设计 `freertos_tasks.c` — 最终的 4 任务架构:

```c
/* ==========================================
 *  任务优先级定义
 * ========================================== */
#define PRIO_ECAT    4   /* 最高: EtherCAT 周期任务 */
#define PRIO_LWIP    2   /* 中等: LwIP 处理 */
#define PRIO_DEBUG   1   /* 最低: 串口调试 */

TaskHandle_t xEcatTaskHandle = NULL;
TaskHandle_t xLwipTaskHandle = NULL;
TaskHandle_t xDebugTaskHandle = NULL;

/* ==========================================
 *  EtherCAT 处理任务 (优先级 4)
 * ========================================== */
void vEthercatTask(void *pvParameters)
{
    (void)pvParameters;
    uint32_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        /* 等待 ETH RX 中断通知 (初始版本用 vTaskDelay 轮询) */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));

        /* 在这里放 SOEM 接收处理 */
        /* ecx_receive_processdata(...) */
        ETH_DumpRegisters();

        /* 周期任务: 1ms (SOEM 周期) */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1));
    }
}

/* ==========================================
 *  LwIP 处理任务 (优先级 2)
 * ========================================== */
void vLwipTask(void *pvParameters)
{
    (void)pvParameters;
    uint32_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        MX_LWIP_Process();
        tcp_echoserver_spi_stream_poll();
        net_config_poll();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(5));
    }
}

/* ==========================================
 *  调试任务 (优先级 1)
 * ========================================== */
void vDebugTask(void *pvParameters)
{
    (void)pvParameters;
    uint32_t last_wake = xTaskGetTickCount();
    uint32_t uptime_sec = 0;

    for (;;)
    {
        uptime_sec++;
        printf("[%lu] FreeRTOS: ECAT=%d LWIP=%d DBG=%d\r\n",
               uptime_sec,
               uxTaskGetStackHighWaterMark(xEcatTaskHandle),
               uxTaskGetStackHighWaterMark(xLwipTaskHandle),
               uxTaskGetStackHighWaterMark(xDebugTaskHandle));
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(5000));
    }
}

/* ==========================================
 *  任务创建入口
 * ========================================== */
void vApplicationTasksCreate(void)
{
    xTaskCreate(vEthercatTask, "Ecat", 256, NULL, PRIO_ECAT, &xEcatTaskHandle);
    xTaskCreate(vLwipTask,     "LwIP", 256, NULL, PRIO_LWIP, &xLwipTaskHandle);
    xTaskCreate(vDebugTask,    "Debug",256, NULL, PRIO_DEBUG, &xDebugTaskHandle);
}
```

**步骤 2.2** 修改 `Core/Src/main.c`:
保持现有 `MX_*_Init()` 不变, 替换 `while(1)`:

```c
int main(void)
{
    SCB->VTOR = 0x08010000U;
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_SPI1_Init();
    MX_SPI2_Init();
    MX_SPI3_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    MX_USART3_UART_Init();
    MX_LWIP_Init();          /* 这会初始化 ETH MAC/DMA/PHY */
    tcp_echoserver_init();

    /* 创建 FreeRTOS 任务 */
    vApplicationTasksCreate();

    /* 启动调度器 */
    vTaskStartScheduler();

    /* 不应该到达这里 */
    Error_Handler();
}
```

### ▎时段 3: 修改 main.c 和 stm32f4xx_it.c（60 min）

**步骤 3.1** 使能 ETH 中断:
```c
// 在 MX_LWIP_Init() 之后, 或 vApplicationTasksCreate() 中:
HAL_NVIC_SetPriority(ETH_IRQn, 5, 0);
HAL_NVIC_EnableIRQ(ETH_IRQn);

/* 使能 ETH MAC 接收中断 */
ETH->MACSR = 0;                                    /* 清除状态 */
ETH->MACIER |= ETH_MACIER_RSVIE;                    /* 接收状态有效中断 */
/* 使能 ETH DMA 接收中断 */
ETH->DMASR = 0x0000FFFF;                            /* 清除所有状态 */
ETH->DMAIER |= ETH_DMAIER_RIE;                      /* 接收中断使能 */
```

**步骤 3.2** 实现 ETH_IRQHandler 于 `stm32f4xx_it.c`:
```c
/* USER CODE BEGIN 1 */
#include "FreeRTOS.h"
#include "task.h"

extern TaskHandle_t xEcatTaskHandle;

void ETH_IRQHandler(void)
{
    uint32_t dmasr = ETH->DMASR;
    
    /* 接收中断 */
    if (dmasr & ETH_DMASR_RS)
    {
        ETH->DMASR = ETH_DMASR_RS;
        
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(xEcatTaskHandle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    
    /* 清除其他标志 */
    ETH->DMASR = dmasr & ~0xFF000000;
}
```

### ▎时段 4: 解决 lwipopts.h 的 NO_SYS 冲突（60 min）

**核心问题:** LwIP 当前 `NO_SYS=1`（无 RTOS 模式），但我们现在有 FreeRTOS。

**方案 A（推荐 — 简单）:** 保持 `NO_SYS=1`，LwIP 继续在单任务中轮询:
- LwIP 不启用 `tcpip.c` 协议栈线程
- 仍从 `vLwipTask()` 中调用 `MX_LWIP_Process()`
- 这是最安全的，不改变 lwIP 内部锁模型

**方案 B（复杂）:** `NO_SYS=0`, `LWIP_TCPIP_CORE_LOCKING=1`
- 需要配置 `sys_arch.c`/`sys_arch.h`
- 需要实现邮箱、信号量等
- 移后到 Week 3，不是现在

**决定:** 采用**方案 A** — 暂时保持 `NO_SYS=1`。修改 `lwipopts.h`:

```c
// lwipopts.h — 保持:
#define NO_SYS 1
// 但添加:
#define LWIP_TIMERS 1
// FreeRTOS 任务中调用 MX_LWIP_Process 即可
```

### ▎时段 5: 编译并调试（60 min）

**步骤 5.1** 编译:
```bash
cmake -B build -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake
cmake --build build 2>&1 | head -60
```

**典型的编译错误:**

| 错误 | 原因 | 解决 |
|------|------|------|
| `undefined reference to vTaskNotifyGiveFromISR` | 缺少 task.h include | `#include "task.h"` in stm32f4xx_it.c |
| `SVC_Handler redefined` | stm32f4xx_it.c 和 port.c 冲突 | 注释掉 stm32f4xx_it.c 中的空 SVC_Handler |
| `ETH_IRQHandler redefined` | stm32f4xx_it.c 启动文件中都有 | 确保只有一个定义，删除启动文件中的 weak 默认 |
| `error: 'ETH_BufferTypeDef' undeclared` | stm32f4xx_hal_eth.h 未包含 | 检查 HAL ETH module 是否 enable |

### ▎时段 6: 验证（30 min）

烧录后验证:
1. 串口打印: `[1] FreeRTOS: ECAT=248 LWIP=248 DBG=248`（栈余量因任务而异）
2. TCP echo server 仍然可以 ping 通（证明 LwIP 仍然工作）
3. 示波器看 ETH TX_EN 引脚是否有 RX 中断触发时的活动

### ▎Day 4 结束检查清单 ✅
- [ ] 3 个任务骨架创建完成并编译通过
- [ ] ETH 中断已使能 (NVIC + MAC + DMA)
- [ ] `ETH_IRQHandler` 实现，发送任务通知给 EtherCAT 任务
- [ ] LwIP 仍能正常工作 (TCP echo server)
- [ ] 串口每 5s 打印一次 3 个任务的栈余量

---

## Day 5 — SOEM OSAL 层开始 + EtherCAT 帧结构（5h）

**目标:** 理解 SOEM 需要什么 OS 服务，创建 osal_defs.h 和 osal.c 的 5 个核心函数

### ▎时段 1: 读 SOEM 的 osal.h — 理解 SOEM 需要的 OS 服务（45 min）

**打开文件:** `c:/ST_project/DP83848_FreeRTOS/SOEM/SOEM-master/osal/osal.h`

需要的 13 个函数:
```
时间类:   osal_get_monotonic_time, osal_current_time, osal_time_diff
定时器:   osal_timer_start, osal_timer_is_expired
延时:     osal_usleep, osal_monotonic_sleep
内存:     osal_malloc, osal_free
线程:     osal_thread_create, osal_thread_create_rt
互斥锁:   osal_mutex_create, osal_mutex_destroy, osal_mutex_lock, osal_mutex_unlock
```

### ▎时段 2: 读 Win32 osal.c — 理解每个函数的实现（45 min）

**打开文件:** `osal/win32/osal.c` 和 `osal/win32/osal_defs.h`

对照阅读，逐行理解:
- Win32 的 `QueryPerformanceCounter` ≈ DWT `CYCCNT`
- Win32 的 `SleepEx` ≈ FreeRTOS `vTaskDelay`
- Win32 的 `CreateThread` ≈ `xTaskCreate`
- Win32 的 `CRITICAL_SECTION` ≈ FreeRTOS 互斥锁

### ▎时段 3: 设计 FreeRTOS 映射表（30 min）

```
SOEM API                  → FreeRTOS API
────────────────────────────────────────────────────
ec_timet                  → uint64_t (微秒时间戳, 不是 timespec)
osal_get_monotonic_time() → DWT->CYCCNT → µs
osal_current_time()       → 同上
osal_time_diff()          → end - start
osal_timer_start()        → 记录 stop_time = now + timeout
osal_timer_is_expired()   → now >= stop_time
osal_usleep(>=1000)       → vTaskDelay(pdMS_TO_TICKS(usec/1000))
osal_usleep(<1000)        → DWT 自旋等待
osal_malloc()             → pvPortMalloc()
osal_free()               → vPortFree()
osal_thread_create()      → xTaskCreate()
osal_thread_create_rt()   → xTaskCreate(高优先级)
osal_mutex_create()       → xSemaphoreCreateMutex()
osal_mutex_destroy()      → vSemaphoreDelete()
osal_mutex_lock()         → xSemaphoreTake(portMAX_DELAY)
osal_mutex_unlock()       → xSemaphoreGive()
```

### ▎时段 4: 创建 osal_defs.h（45 min）

**创建文件:** `SOEM/SOEM-master/osal/stm32f4/osal_defs.h`

```c
#ifndef _osal_defs_
#define _osal_defs_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* 注意:
 * SOEM 期望 ec_timet = struct timespec (tv_sec, tv_nsec)
 * 但为了简化 FreeRTOS 移植, 我们用 uint64_t 表示微秒
 * 和 Win32/Linux 不同, stm32f4 版本用纯整型时间
 */
#define EC_TIMET_USING_UINT64

#ifdef EC_DEBUG
#include <stdio.h>
#define EC_PRINT printf
#else
#define EC_PRINT(...) \
   do                 \
   {                  \
   } while (0)
#endif

/* Packed 结构体 (STM32 GCC) */
#define OSAL_PACKED_BEGIN
#define OSAL_PACKED      __attribute__((__packed__))
#define OSAL_PACKED_END

/* 时间类型 — 使用微秒整型 */
typedef uint64_t ec_timet;

/* 线程句柄 */
typedef void *OSAL_THREAD_HANDLE;
typedef void (*OSAL_THREAD_FUNC)(void *);
typedef void (*OSAL_THREAD_FUNC_RT)(void *);

/* 互斥锁类型 */
typedef void *osal_mutext;

#ifdef __cplusplus
}
#endif

#endif
```

**⚠️ 注意:** `ec_timet` 的兼容性问题。标准的 SOEM osal_defs.h 中 `ec_timet = struct timespec`。如果改为 `uint64_t`，需要确保 `osal.h` 中依赖 `timespec` 的宏（`osal_timespec_from_usec`, `osal_timespeccmp` 等）不被使用。由于我们直接实现所有函数，不调用这些宏，所以没问题。

更安全的做法——尽量保持与 Linux 版本一致：
```c
// 如果 struct timespec 不可用, 直接定义:
typedef struct {
    uint32_t tv_sec;
    uint32_t tv_nsec;
} ec_timet;
```

我们需要仔细检查 SOEM 内部代码是否直接使用 `timespec` 的字段——是的, `osal.h` 中的宏 `osal_timespec_from_usec` 等直接操作 `tv_sec` / `tv_nsec`。如果用 `uint64_t` 这些宏会编译报错。

**修正方案:** 采用 `struct timespec` 风格的兼容方式:
```c
typedef struct {
    uint32_t tv_sec;
    uint32_t tv_nsec;
} ec_timet;
```

### ▎时段 5: 创建 osal.c — 实现前 5 个函数（90 min）

**创建文件:** `SOEM/SOEM-master/osal/stm32f4/osal.c`

```c
#include "osal.h"
#include "FreeRTOS.h"
#include "task.h"

/* DWT 初始化标志 */
static int dwt_initialized = 0;

static void dwt_init(void)
{
    if (!dwt_initialized)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_MASK;
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_MASK;
        dwt_initialized = 1;
    }
}

/* 获取当前 DWT 周期计数 (1 周期 = 1/168 MHz ≈ 5.95ns) */
static inline uint64_t dwt_get_us(void)
{
    /* DWT->CYCCNT 是 32-bit, 会溢出. 
     * 我们只用于差值计算, 但最好用 uint64 累计.
     * 简化版本: 直接读 32-bit, 在 µs 精度下约 25 秒溢出一次.
     * 这里用简单方式: DWT 计数 / 168 = µs */
    return (uint64_t)DWT->CYCCNT / 168ULL;
}

/* ==========================================
 *  时间函数
 * ========================================== */

void osal_get_monotonic_time(ec_timet *ts)
{
    uint64_t us = dwt_get_us();
    ts->tv_sec  = (uint32_t)(us / 1000000ULL);
    ts->tv_nsec = (uint32_t)((us % 1000000ULL) * 1000ULL);
}

ec_timet osal_current_time(void)
{
    ec_timet ts;
    osal_get_monotonic_time(&ts);
    return ts;
}

void osal_time_diff(ec_timet *start, ec_timet *end, ec_timet *diff)
{
    uint64_t start_us = (uint64_t)start->tv_sec * 1000000ULL + start->tv_nsec / 1000ULL;
    uint64_t end_us   = (uint64_t)end->tv_sec   * 1000000ULL + end->tv_nsec   / 1000ULL;
    uint64_t diff_us  = end_us - start_us;
    diff->tv_sec  = (uint32_t)(diff_us / 1000000ULL);
    diff->tv_nsec = (uint32_t)((diff_us % 1000000ULL) * 1000ULL);
}

/* ==========================================
 *  定时器函数
 * ========================================== */

void osal_timer_start(osal_timert *self, uint32 timeout_usec)
{
    ec_timet now;
    osal_get_monotonic_time(&now);
    uint64_t now_us = (uint64_t)now.tv_sec * 1000000ULL + now.tv_nsec / 1000ULL;
    uint64_t stop_us = now_us + timeout_usec;
    self->stop_time.tv_sec  = (uint32_t)(stop_us / 1000000ULL);
    self->stop_time.tv_nsec = (uint32_t)((stop_us % 1000000ULL) * 1000ULL);
}

boolean osal_timer_is_expired(osal_timert *self)
{
    ec_timet now;
    osal_get_monotonic_time(&now);
    
    /* 比较 now >= stop_time */
    if (now.tv_sec > self->stop_time.tv_sec) return TRUE;
    if (now.tv_sec < self->stop_time.tv_sec) return FALSE;
    return now.tv_nsec >= self->stop_time.tv_nsec;
}

/* ==========================================
 *  延时函数 (核心移植函数)
 * ========================================== */

int osal_usleep(uint32 usec)
{
    if (usec >= 1000)
    {
        /* > 1ms: 使用 vTaskDelay, 让出 CPU */
        vTaskDelay(pdMS_TO_TICKS(usec / 1000));
        
        /* 处理剩余不足 1ms 的部分 */
        uint32 remainder = usec % 1000;
        if (remainder > 0)
        {
            uint64_t start = dwt_get_us();
            while ((dwt_get_us() - start) < remainder) { }
        }
    }
    else
    {
        /* < 1ms: DWT 自旋等待 */
        uint64_t start = dwt_get_us();
        while ((dwt_get_us() - start) < usec) { }
    }
    return 0;
}

int osal_monotonic_sleep(ec_timet *ts)
{
    ec_timet now;
    osal_get_monotonic_time(&now);
    
    /* 计算剩余时间 */
    uint64_t now_us  = (uint64_t)now.tv_sec * 1000000ULL + now.tv_nsec / 1000ULL;
    uint64_t goal_us = (uint64_t)ts->tv_sec * 1000000ULL + ts->tv_nsec / 1000ULL;
    
    if (goal_us <= now_us)
        return 0;  /* 已超时 */
    
    uint32_t diff_us = (uint32_t)(goal_us - now_us);
    return osal_usleep(diff_us);
}

/* ==========================================
 *  内存函数 (简易实现)
 * ========================================== */

void *osal_malloc(size_t size)
{
    return pvPortMalloc(size);
}

void osal_free(void *ptr)
{
    vPortFree(ptr);
}

/* ==========================================
 *  线程函数 (骨架 — SOEM 单线程暂时不需要)
 * ========================================== */

int osal_thread_create(void *thandle, int stacksize, void *func, void *param)
{
    /* SOEM 核心在单个任务中运行, 这个接口暂时用不到 */
    (void)thandle;
    (void)stacksize;
    (void)func;
    (void)param;
    return 1;  /* 伪实现 */
}

int osal_thread_create_rt(void *thandle, int stacksize, void *func, void *param)
{
    return osal_thread_create(thandle, stacksize, func, param);
}

/* ==========================================
 *  互斥锁函数 (骨架)
 * ========================================== */

void *osal_mutex_create(void)
{
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    return (void *)mutex;
}

void osal_mutex_destroy(void *mutex)
{
    vSemaphoreDelete((SemaphoreHandle_t)mutex);
}

void osal_mutex_lock(void *mutex)
{
    xSemaphoreTake((SemaphoreHandle_t)mutex, portMAX_DELAY);
}

void osal_mutex_unlock(void *mutex)
{
    xSemaphoreGive((SemaphoreHandle_t)mutex);
}
```

**关键设计决策:** 
- `osal_usleep(>=1000µs)` → `vTaskDelay()` 让出 CPU
- `osal_usleep(<1000µs)` → DWT 自旋（忙等）
- DWT 初始化: 在第一次调用 `osal_get_monotonic_time` 时自动初始化

### ▎时段 6: SOEM 编译准备 — 添加 SOEM 到 CMake（45 min）

**修改** `cmake/stm32cubemx/CMakeLists.txt`:

```cmake
# === SOEM 源码 ===
set(SOEM_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../SOEM/SOEM-master)

set(SOEM_Src
    ${SOEM_SRC_DIR}/src/ec_base.c
    ${SOEM_SRC_DIR}/src/ec_config.c
    ${SOEM_SRC_DIR}/src/ec_coe.c
    ${SOEM_SRC_DIR}/src/ec_dc.c
    ${SOEM_SRC_DIR}/src/ec_eoe.c
    ${SOEM_SRC_DIR}/src/ec_foe.c
    ${SOEM_SRC_DIR}/src/ec_main.c
    ${SOEM_SRC_DIR}/src/ec_print.c
    ${SOEM_SRC_DIR}/src/ec_soe.c
    ${SOEM_SRC_DIR}/osal/stm32f4/osal.c
)

set(SOEM_Include_Dirs
    ${SOEM_SRC_DIR}/include
    ${SOEM_SRC_DIR}/osal
    ${SOEM_SRC_DIR}/osal/stm32f4
)

# 创建 SOEM 目标
add_library(SOEM OBJECT)
target_sources(SOEM PRIVATE ${SOEM_Src})
target_include_directories(SOEM PUBLIC ${SOEM_Include_Dirs})
target_link_libraries(SOEM PUBLIC stm32cubemx)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE SOEM)
```

**注意:** 这会暂时编译失败，因为 SOEM 的 `nicdrv.c` 还没创建。Day 5 目标只是 `osal.c` 和 `osal_defs.h` 编译通过，不包含 NIC 驱动。

### ▎Day 5 结束检查清单 ✅
- [ ] `osal/stm32f4/osal_defs.h` 已创建（ec_timet, OSAL_THREAD_HANDLE, osal_mutext 等类型定义）
- [ ] `osal/stm32f4/osal.c` 已创建并实现 5 个函数: `osal_get_monotonic_time`, `osal_current_time`, `osal_timer_start`, `osal_timer_is_expired`, `osal_usleep`
- [ ] DWT 周期计数器已启用
- [ ] SOEM 的 osal 层添加到 CMake
- [ ] 编译通过（只针对 osal.c 和 ec_base.c, ec_main.c 等核心文件 — NIC 驱动部分 Week 2）

---

## 第一周里程碑 🔵

**目标:** FreeRTOS 跑起来，ETH 中断唤醒任务，SOEM OSAL 开始

### 最终验证清单
- [ ] **Day 1:** FreeRTOS 3 个任务运行, 串口打印状态
- [ ] **Day 2:** 信号量/任务通知/队列/互斥锁实验通过
- [ ] **Day 3:** 理解 ETH DMA 描述符, 寄存器打印可用
- [ ] **Day 4:** ETH 中断驱动, 3 任务(ECAT/LwIP/Debug)骨架, LwIP 仍工作
- [ ] **Day 5:** `osal_defs.h` + `osal.c` 编译通过

### 第一周结束时已创建的完整文件列表

| 文件 | 用途 |
|------|------|
| `Core/Inc/FreeRTOSConfig.h` | FreeRTOS 配置 |
| `Core/Src/freertos_tasks.c` | 所有任务定义 |
| `Core/Inc/freertos_tasks.h` | 任务头文件 |
| `Core/Src/eth_dump.c` | ETH 寄存器调试 |
| `SOEM/SOEM-master/osal/stm32f4/osal_defs.h` | SOEM OSAL 头文件 |
| `SOEM/SOEM-master/osal/stm32f4/osal.c` | SOEM OSAL 实现 |

### 已修改的文件
| 文件 | 改动 |
|------|------|
| `cmake/stm32cubemx/CMakeLists.txt` | +FreeRTOS OBJECT library, +SOEM OBJECT library |
| `Core/Src/main.c` | 替换 while(1) 为 vTaskStartScheduler |
| `Core/Src/stm32f4xx_it.c` | +ETH_IRQHandler, 注释 SVC/PendSV/SysTick, +TIM7 ISR 信号量实验 |
| `LWIP/Target/lwipopts.h` | 保持 NO_SYS=1 (方案确认) |
