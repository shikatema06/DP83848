# 代码 Bug 排查报告（网络 / ADC-SPI / IAP）

> 排查范围：LWIP/TCP、ADS8354(SPI/ADC)、IAP Bootloader。
> 排除项：USART 串口（用户标记为非关键测试项）。
> 排查方式：纯静态走查 + 对照链接脚本 / Flash 布局 / ADS8354 数据手册，**未修改任何源文件**。
> 日期：2026-08-04

风险等级说明：
- **高**：会导致变砖 / HardFault / 网络死锁 / 数据永久错位。
- **中**：特定条件下触发，影响功能正确性。
- **低**：健壮性 / 可维护性问题。

---

## 一、IAP Bootloader（最高风险）

### IAP-1 【高】APP 链接脚本 FLASH 起始地址与 Bootloader 重叠
- 位置：`STM32F407XX_APP.ld:11` vs `Bootloader/STM32F407XX_BOOT.ld:11`
- 现象：APP 链接脚本 `FLASH ORIGIN = 0x08000000, LENGTH = 1024K`；Bootloader 链接脚本 `FLASH ORIGIN = 0x08000000, LENGTH = 64K`。
- 根因：Bootloader 占用 `0x08000000–0x08010000`，而工程里 IAP 约定 APP 起始为 `APP_START_ADDR = 0x08010000`（见 `bootloader_main.c:18`、`flash_if.h:16` `USER_FLASH_FIRST_PAGE_ADDRESS`）。APP 链接脚本却从 `0x08000000` 开始，向量表落在 Bootloader 区域。
- 后果：
  1. 烧录 APP 会**覆盖 Bootloader**，设备变砖（需 ST-Link 重新烧 Bootloader）。
  2. APP 复位后从 `0x08000000`（Bootloader 区）取向量，而非 `0x08010000`。
- 修复建议：将 `STM32F407XX_APP.ld` 改为：
  ```
  FLASH (rx) : ORIGIN = 0x08010000, LENGTH = 960K   /* 1024K - 64K */
  ```
  并确认 `net_config` 区（`0x080E0000`，sector 11）仍在 APP 范围内（0x08010000+960K = 0x080F0000，含 0x080E0000，OK）。

### IAP-2 【中】TFTP 收完末包后"直接跳转 App"未走系统复位流
- 位置：`Bootloader/tftp_server.c:175-184`
- 现象：CRC 校验通过后，直接 `__set_MSP(app_sp); ((void(*)(void))app_pc)();` 跳转，仅设 `SCB->VTOR` 未先关闭 LWIP/ETH/定时器外设、未关中断。
- 根因：此时 UDP/TFTP 回调仍在运行，ETH DMA、TIM 等外设处于活动态。直接跳转会让 App 继承残余外设状态（尤其 ETH DMA 描述符、pending 中断），可能导致 App 网络初始化异常或 HardFault。
- 修复建议（二选一，优先方案 A）：
  - **方案 A（推荐）**：校验成功后统一走 `NVIC_SystemReset()`。Bootloader 已写 `boot_valid`，复位后 `bootloader_main.c` 的 `jump_to_app()` 会干净地初始化并跳转（`bootloader_main.c:64-79`）。
  - **方案 B**：跳转前显式 `eth_stop()` / `HAL_TIM_Base_Stop_IT` / `__disable_irq()`，并 deinit 所有外设句柄，再设 VTOR + MSP + PC。
- 补充：`CMD_ENTER_BOOTLOADER`（`tcp_echoserver.c:361-399`）已正确使用 `NVIC_SystemReset()`，建议 TFTP 路径与之保持一致。

### IAP-3 【中】TFTP 分片包（`len != tot_len`）被静默丢弃且未回 ACK
- 位置：`Bootloader/tftp_server.c:103-104`
- 现象：`if (pkt_buf->len != pkt_buf->tot_len) return;` 直接 return，既不处理也不回 ACK。
- 根因：TFTP 数据块理论 516 字节 < 以太网 MTU，一般不分片；但若网络层出现分片或 LwIP 将包拆成 pbuf 链，该包会被丢弃且无 ACK，发送端只能超时重传，严重时卡死升级。
- 修复建议：用 `pbuf_copy_partial()` 将整条 pbuf 链拷到本地缓冲后再解析块号/数据（与 `:119-121` 已有做法一致），不要仅看 `pkt_buf->len`。

### IAP-4 【低】块号重传处理只 ACK 不写，但仍触发"末包判断"
- 位置：`Bootloader/tftp_server.c:134-140, 143`
- 现象：`IAP_tftp_extract_block(...) == args->block+1` 的零长度分支（`:134`）与正常分支重复收紧；重传包（block <= args->block）被忽略但仍 `IAP_tftp_send_ack_packet` 回旧块号 ACK，而末包判断 `pkt_buf->len < TFTP_DATA_PKT_LEN_MAX`（`:143`）在重传短包时会误触发"末包"→ 校验失败分支复位。
- 修复建议：明确区分"期望块 / 重复块（仅回 ACK 不写不判断末包）/ 乱序块（回错误码或忽略）"三种情况，末包判断只在"期望块且长度不足"时成立。

### IAP-5 【低】`CMD_ENTER_BOOTLOADER` 写 `boot_valid` 未校验返回值
- 位置：`Core/Src/tcp_echoserver.c:376`
- 现象：`HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, 0x0800FFFCU, 0)` 未检查返回状态，第 383 行读回 `bv` 也未判 "是否真为 0"。
- 根因：Flash 位只能 1→0；`boot_valid` 位于 sector 3（`0x0800FFFC`），若该扇区此前未擦除且原位非全 1，则写 0 可能失败（但仍可能成功，因为 target bit 同为 0）。风险偏低，但缺乏失败检测。
- 修复建议：按与 `flash_if.c` 一致的流程——先 `FLASH_If_Erase_Sector(BOOT_VALID_ADDR)` 再写，并检查 `HAL_FLASH_Program` 返回值；复位前确认 `*(volatile uint32_t*)0x0800FFFC == 0`。

---

## 二、LWIP / TCP

### LWIP-1 【高（功能性的）】"30s 配置窗口后切到 Flash active IP"逻辑注释与实现不一致
- 位置：`LWIP/App/lwip.c:76-94`（仅设 entry IP）、`Core/Src/main.c:115`（调用 `net_config_poll`）、`Core/Src/net_config.c:207-228`（`net_config_poll`）
- 现象：注释明说"上电用 entry IP，30s 后再切到 Flash active idx"，但 `MX_LWIP_Init` 只在初始化时把 entry IP 写进 `IP_ADDRESS[]`；`net_config_poll()` 在 30s 后调用 `net_config_apply_to_netif()` 切到 `active_idx` 的 IP。**逻辑本身能跑通**（main.c 主循环确实调了 poll）。
- 隐藏 bug：`net_config_get_entry_ip()`（`net_config.c:187-195`）仅在 `slot[ENTRY_IDX]` **仍等于出厂值**时返回成功。一旦用户用 SET 命令改过 slot0，entry IP 取不到，配置窗口期将**回落到硬编码的 172.16.1.69**（`lwip.c:63-66`），而不是用户设定的 entry IP。
- 修复建议：
  1. 配置窗口期应直接使用 `active_idx` 指向的 entry 项，而非依赖"是否等于出厂值"判断。
  2. 若取不到 entry IP，应回退到出厂默认值并打日志，而不是静默用 172.16.1.69。

### LWIP-2 【中】`net_config_poll` 窗口逻辑依赖绝对 tick 阈值，存在回绕与方向歧义
- 位置：`Core/Src/net_config.c:219`
- 现象：`if (HAL_GetTick() < NET_CONFIG_WINDOW_MS) return;`
- 分析：方向其实正确（tick < 30s 保持窗口）。但它是**绝对 tick 阈值**而非"上电后 30s"，且 `HAL_GetTick()` 在 ~49.7 天回绕到 0 后会重新进入窗口期（4 字节毫秒计数回绕）。若设备长期运行，回绕瞬间会错误地再次进入配置窗口并尝试切 IP。
- 修复建议：用相对时间——记录 `boot_tick = HAL_GetTick()`，判断 `HAL_GetTick() - boot_tick >= NET_CONFIG_WINDOW_MS`；或用 `uint32_t` 减法比较 `(HAL_GetTick() - boot_tick) < WINDOW_MS` 规避回绕。

### LWIP-3 【中/待确认】GATEWAY 设为 0.0.0.0
- 位置：`LWIP/App/lwip.c:71-74`、`Bootloader/lwip_boot.c:53`
- 现象：网关全 0。
- 影响：若 PC/上位机与设备**同网段**（172.16.1.x/24），无需网关，功能正常；若跨网段/经路由器访问，则不可达。
- 修复建议：确认现场网络拓扑。跨网段时需填真实网关；同网段可保留，但建议在注释中说明。

### LWIP-4 【中】`NetConfigHandleReq` 响应长度宏可能溢出 / 未校验累加
- 位置：`Core/Src/tcp_echoserver.c:195-216`（`NET_CONFIG_LIST` 响应）
- 现象：`NET_CONFIG_LIST_RESP_LEN = 2 + NET_IP_LIST_MAX*(13+1) = 58`；`txlen += sprintf(...)` 累加到 `txbuf`，但未校验 `txlen` 是否越界，也未保证结尾 `\0`。
- 根因：每条 `IPx=172.16.1.69\r\n` 实际约 17 字符，4 条约 68 字节 + 头部 2 字节 = 70 字节 > 58，存在**缓冲区溢出风险**（取决于 `txbuf` 实际大小，需核对 `NetConfigHandleReq` 内 `txbuf` 声明）。
- 修复建议：按实际字符串长度计算 `RESP_LEN`（或动态 `vsnprintf` + 边界检查），发送前 `txlen < sizeof(txbuf)` 断言。

### LWIP-5 【中】CONT 数据流仅拷贝单一 pbuf，未处理 pbuf 链分片
- 位置：`Core/Src/tcp_echoserver.c:245-290`（`case CMD_SET_FREQ` / CONT 分支）
- 现象：`do { pbuf_copy_partial(p, ..., p->tot_len, 0); } while (0);` 单次拷贝整链（此处用 `tot_len` 实际 OK），但后续用 `p->len` 判断 payload 长度与 `strnstr(p->payload, "\r?\n", p->len)` 只在**首个 pbuf** 内搜索命令结束符。
- 根因：TCP 收受窗口/分段时，一个逻辑命令可能跨多个 pbuf 链；仅看首 pbuf 的 `len` 与 `payload` 会漏掉后续片段，导致命令截断或粘包解析错误。
- 修复建议：在 `tcp_echoserver_recv` 中先用 `pbuf_copy_partial` 把整条链拼到线性缓冲，再做命令解析；或用 `pbuf_coalesce()` 后再处理。

### LWIP-6 【低】`recv` 回调内 `UART_Send`（阻塞 HAL_UART_Transmit）与网络上下文
- 位置：`Core/Src/tcp_echoserver.c:187/205/216/225`（NetConfigHandleReq 内调用 `UART_Send` 输出调试）
- 现象：在 LwIP `recv` 回调（无 RTOS 时由 `MX_LWIP_Process` 调用）里直接 `UART_Send` 阻塞发送。
- 影响：阻塞网络处理线程、可能与其他 UART 发送路径竞争 `huart`；若波特率慢、字符串长，会拖慢 `MX_LWIP_Process` 轮询，导致 ACK 延迟、重传。
- 修复建议：协议回调内只用环形缓冲入队日志，由主循环/低优先级任务异步发送；或确保 `UART_Send` 有发送锁且非阻塞。

---

## 三、ADS8354 / SPI

### ADC-1 【高/待确认】三路 ADC 共用 `hspi1` 句柄，但 CS 切到 SPI2/SPI3 对应的 GPIO
- 位置：`ADS8354/ads8354.c:137-161`（`Ads8354ReadConvers`）
- 现象：函数按 `hspi` 参数判断是 `&hspi1/2/3`，然后 `Spi1Enable/Spi2Enable/Spi3Enable` 拉对应 CS，但 **SPI 传输一律用传入的 `hspi` 句柄**。
- 根因（两种可能，需核对硬件）：
  - 若三片 ADS8354 物理上都挂在 **SPI1** 上、仅用 3 个不同 GPIO 作 CS（独立片选）：则 `Spi2Enable/Spi3Enable` 必须只是 GPIO 操作（不是切换 SPI 外设），否则传输会走到错误的 SPI 控制器。需确认 `spi.c` 中 `Spi2Enable/Spi3Enable` 宏定义。
  - 若三片分别挂在 SPI1/2/3 三个独立控制器：则 `tcp_echoserver_init` 中三路应各自传 `&hspi1/2/3`，但 `Ads8354Init()`（`ads8354.c:117-121`）只用 `Spi1Enable` 配置了**一路** CFR，另外两路未初始化 CFR，会沿用上电默认（可能不是单 SDO 模式）。
- 修复建议：明确硬件拓扑。若是"单 SPI1 + 3 GPIO CS"，统一用 `hspi1` 并让 `SpiXEnable` 仅为 GPIO；若是"3 独立 SPI"，则 `Ads8354Init` 需对三路分别写 CFR。

### ADC-2 【高/待确认】单 SDO 模式下读取 6 字节，可能多读导致数据错位
- 位置：`ADS8354/ads8354.c:120`（`Ads8354Write(ADC_CFR_W, 0x400)`）、`:150`（`HAL_SPI_TransmitReceive(hspi, txBuf, rxBuf, 6, 10)`）
- 现象：注释"single sdo mode"，CFR 写 `0x400`；读取收 6 字节。
- 根因：需对照 ADS8354 数据手册确认 `0x400`（bit9=SDO 控制）是否真为单 SDO。若单 SDO + 双通道 A/B，每帧 = 2×16bit = 4 字节；收 6 字节会多读 2 字节，且 `val = (rxBuf[2]<<8)+rxBuf[3]` 只取中间 2 字节，帧对齐稍偏就可能取到错误通道数据。
- 修复建议：对照手册确认 SDO 模式与帧长（单 SDO=4B，双 SDO=2B，三 SDO/四 SDO 更少）；`rxBuf` 长度与取值偏移必须与该模式严格匹配。

### ADC-3 【中】`Ads8354Write` 写 CFR 后未释放 CS
- 位置：`ADS8354/ads8354.c`（写函数末段，CS Disable 被注释掉，见 `Spi1Disable` 在 `:113` 仅 Init 读回路径）
- 现象：`Ads8354Write` 配置 CFR 后若未拉高 CS，片选持续有效，可能干扰后续 `Ads8354ReadConvers` 的首字节对齐。
- 修复建议：写操作后务必 `SpiXDisable`（拉高 CS），保证帧边界清晰。

### ADC-4 【低】`Ads8354Init` 仅配置一路 CFR，未校验回读
- 位置：`ADS8354/ads8354.c:117-121`
- 现象：`Ads8354Write(ADC_CFR_W, 0x400)` 后无回读校验，若 SPI 通信失败（CS/时钟相位错），初始化静默失败，后续读数全 0 或固定值。
- 修复建议：写后读回 CFR 校验，失败置 `spi_error` 并通过状态字上报。

---

## 四、待用户确认项（非确属 bug，需现场/需求确认）

| 编号 | 项 | 当前状态 | 需确认 |
|------|----|----------|--------|
| Q1 | APP 链接脚本起始地址 | 当前 0x08000000，与 Bootloader 重叠 | 是否接受改为 0x08010000（见 IAP-1） |
| Q2 | 网关 0.0.0.0 | 同网段可用 | 上位机是否跨网段？ |
| Q3 | 配置窗口 entry IP 逻辑 | 改过 slot0 后回落硬编码 172.16.1.69 | 配置窗口期期望用哪个 IP？ |
| Q4 | ADS8354 硬件拓扑 | 代码疑似单 SPI1+3 CS 或 3 独立 SPI | 实际 PCB 接线？ |
| Q5 | ADS8354 SDO 模式 | CFR=0x400，读 6 字节 | 对照手册确认帧长 4B or 6B？ |
| Q6 | TIM7 周期 100us（Prescaler=83, Period=199 @168MHz） | 注释写 200us | 注：TIM7 关联数据发送帧率，本次未深入（属网络/UART 交叉），是否纳入？ |

---

## 五、IAP 问题专项解决方案（汇总）

用户重点关注 IAP，以下给出**落地修复步骤**（计划确认后再改代码）：

1. **修正 APP 链接脚本（必做，防变砖）**
   - `STM32F407XX_APP.ld`：`FLASH ORIGIN = 0x08010000, LENGTH = 960K`。
   - 同步确认 `STM32F407XX_BOOT.ld` 仍为 64K，且 `APP_START_ADDR`/`USER_FLASH_FIRST_PAGE_ADDRESS` = `0x08010000` 三者一致。

2. **统一"进入 App"路径为系统复位流**
   - TFTP 升级成功（`tftp_server.c:175-184`）：改为 `NVIC_SystemReset()`，由 Bootloader 的 `jump_to_app()` 干净跳转（已具备 VTOR + MSP + 向量检查）。
   - `CMD_ENTER_BOOTLOADER`（`tcp_echoserver.c:398`）已用 `NVIC_SystemReset()`，保持。

3. **完善 boot_valid 写流程**
   - `tcp_echoserver.c:376`：参照 `flash_if.c` 先 `FLASH_If_Erase_Sector(BOOT_VALID_ADDR)` 再写，校验返回值。
   - `tftp_server.c:166-172` 已做擦除+写+校验，保持。

4. **TFTP 健壮性**
   - `tftp_server.c:103`：分片包用 `pbuf_copy_partial` 拼合后再解析，不要直接丢弃。
   - `tftp_server.c:134-143`：区分期望块/重传块/末包，仅在"期望块且长度不足"时判末包。

5. **升级失败保护**
   - `tftp_server.c:189+`（CRC 失败分支）：确保 `NVIC_SystemReset()` 回到 Bootloader 等待重传，而非跳到损坏的 App。
   - Bootloader 启动时应校验 `boot_valid` + App 向量合法性（`bootloader_main.c:64-79` 已有 MSP 范围检查，保持）。

6. **验证清单（改后）**
   - ST-Link 单独烧 Bootloader → 复位应停在 TFTP 等待。
   - TFTP 升级 App → CRC 通过 → 自动跳 App，网络通信正常。
   - App 运行中发 `CMD_ENTER_BOOTLOADER` → 写 boot_valid=0 → 复位 → 回到 Bootloader。
   - 二次升级覆盖 App 不破坏 Bootloader（验证 APP 不占用 0x08000000-0x08010000）。

---

## 六、结论优先级（建议修复顺序）

1. **IAP-1**（链接脚本重叠）—— 最高优先级，随时可能变砖。
2. **IAP-2 / IAP-3**（TFTP 跳转与分片）—— 升级可靠性。
3. **LWIP-1 / LWIP-2**（配置窗口 IP 切换逻辑）—— 网络可用性。
4. **ADC-1 / ADC-2**（SPI 拓扑与 SDO 帧长）—— 采集数据正确性，需硬件确认。
5. **LWIP-4 / LWIP-5**（响应长度 / pbuf 链）—— 协议健壮性。
6. 其余低优先级项随版本迭代处理。
