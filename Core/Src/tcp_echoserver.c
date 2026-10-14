/**
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * This file is part of the lwIP TCP/IP stack.
 * Christiaan Simons rewrote this file to get a more stable echo application.
 */
 /* This file was modified by ST */


#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "main.h"
#include "pbuf.h"
#include "spi.h"
#include "tim.h"
#include "ads8354.h"
#include "net_config.h"
#include "boot_request.h"

/* SPI CS 控制 (与 ads8354.h 一致, 此处冗余定义以确保 CubeMX 安全) */
#define SPI1_CS_PORT    GPIOB
#define SPI1_CS_PIN     GPIO_PIN_6
#define SPI2_CS_PORT    GPIOD
#define SPI2_CS_PIN     GPIO_PIN_8
#define SPI3_CS_PORT    GPIOD
#define SPI3_CS_PIN     GPIO_PIN_0

#if LWIP_TCP

/* -------------------------------------------------------------------------- */
/* 协议常量                                                                   */
/* -------------------------------------------------------------------------- */
#define TCP_ECHO_PORT             1030

/* 二进制帧标记 */
#define FRAME_SOF                 0x02    /* Start of Frame */
#define FRAME_EOF1                0xDD    /* End of Frame byte 1 */
#define FRAME_EOF2                0xAA    /* End of Frame byte 2 */
#define FRAME_LEN                 8       /* 命令帧固定 8 字节 */

/* 命令码 */
#define CMD_START_STREAM          0x05    /* 开始发送 SPI 数据 */
#define CMD_STOP_STREAM           0x06    /* 停止发送 SPI 数据 */
#define CMD_READ_VERSION          0x07    /* 读版本号 */
#define CMD_HEALTH_CHECK          0x08    /* 健康监测 */
#define CMD_LIST_IP               0x09    /* 读 IP 列表 */
#define CMD_SELECT_IP             0x0A    /* 选择列表项并切换本机 IP */
#define CMD_RESTORE_DEFAULT_IP    0x0B    /* 恢复列表第 0 项为当前 IP */
#define CMD_ENTER_BOOTLOADER      0x0C    /* 进入 IAP Bootloader */

/* 版本号 — 允许不同 slot 构建用 -D 覆盖以区分 A/B/C */
#ifndef FW_VERSION_MAJOR
#define FW_VERSION_MAJOR          1
#endif
#ifndef FW_VERSION_MINOR
#define FW_VERSION_MINOR          0
#endif
#ifndef FW_VERSION_PATCH
#define FW_VERSION_PATCH          0
#endif

/* 健康监测参数 */
#define HEALTH_RESP_LEN           26
#define HEALTH_TEMP_EXP           0X0019
#define HEALTH_HUMI_EXP           0X0032

/* -------------------------------------------------------------------------- */
/*  SPI 数据采集: 硬件定时器 + 帧缓冲区                                         */
/* -------------------------------------------------------------------------- */
#define SPI_AVG_CNT              5     /* 5 次采集平均 → 1ms */
#define SPI_SAMPLE_NUM           80    /* 80 数据点 → 组包 */
#define SPI_CHAN_NUM             3     /* 3 路 SPI */

#define PKT_HDR_LEN              7     /* 帧头: 0x02 0x01 0x25 0x31 0x32 0x33 0x34 */
#define PKT_DATA_LEN             (SPI_SAMPLE_NUM * SPI_CHAN_NUM * 2)  /* 480 字节 */
#define PKT_FRAME_LEN            (PKT_HDR_LEN + PKT_DATA_LEN + 11)    /* 7+480+2+2+2+1+4 = 498 */

/* 流式发送: 双缓冲，避免 packet_ready 后 ISR 覆盖待发送帧 */
static volatile uint8_t  stream_enabled;
static volatile uint8_t  stream_packet_ready;
static volatile uint8_t  stream_wr_buf;         /* ISR 当前写入 0/1 */
static volatile uint8_t  stream_rd_buf;         /* 待发送缓冲 0/1 */
static uint8_t           stream_frame[2][PKT_FRAME_LEN];
static volatile uint32_t stream_drop_cnt;      /* 累计丢帧数 */
static volatile uint32_t stream_drop_consec;   /* 连续丢帧数 */
static volatile uint8_t  stream_stalled;       /* 断流标志：consec 越限后置 1 */


static int32_t  spi_acc[SPI_CHAN_NUM];          /* 累加器 (5 次求和) */
static uint8_t  spi_avg_cnt;                    /* 0..4 */
static uint16_t spi_sample_idx;                 /* 0..79 (数据点序号) */
static uint16_t spi_pkg_index;                  /* 包序号 (递增) */
static uint8_t  health_check_index;             /* 健康监测序号 (递增) */ 

/* TIM7 句柄由 CubeMX 在 tim.c 中定义并初始化 (MX_TIM7_Init)，此处通过 tim.h 引入全局 htim7 */

/* -------------------------------------------------------------------------- */
/* 数据结构                                                                   */
/* -------------------------------------------------------------------------- */

static struct tcp_pcb *tcp_echoserver_pcb;

/* ECHO protocol states */
enum tcp_echoserver_states
{
  ES_NONE = 0,
  ES_ACCEPTED,
  ES_RECEIVED,
  ES_CLOSING
};

/* structure for maintaining connection infos */
struct tcp_echoserver_struct
{
  u8_t state;             /* current connection state */
  u8_t retries;
  struct tcp_pcb *pcb;    /* pointer on the current tcp_pcb */
  struct pbuf *p;         /* pointer on the received/to be transmitted pbuf */
  uint8_t streaming;      /* 1 = 此连接正在流式发送 SPI 数据 */
  uint8_t pending_ip_switch;  /* ACK 发完后要切换的 IP index (0xFF=无, 否则是 idx) */
};

/* 全局指针: 指向当前正在流式发送的连接 */
static struct tcp_echoserver_struct *tcp_echoserver_stream_es;

/* -------------------------------------------------------------------------- */
/* 函数声明                                                                   */
/* -------------------------------------------------------------------------- */
static err_t tcp_echoserver_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t tcp_echoserver_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void tcp_echoserver_error(void *arg, err_t err);
static err_t tcp_echoserver_poll(void *arg, struct tcp_pcb *tpcb);
static err_t tcp_echoserver_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
static void tcp_echoserver_send(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es);
static void tcp_echoserver_connection_close(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es);
static struct pbuf *tcp_echoserver_process_command(struct pbuf *rx_pbuf, struct tcp_echoserver_struct *es);

/* 二进制帧处理 */
static int  tcp_echoserver_parse_binary_frame(const uint8_t *data, uint16_t len);
static struct pbuf *tcp_echoserver_handle_binary_cmd(uint8_t cmd, struct tcp_echoserver_struct *es,
                                                     const uint8_t *frame);

/* 文本命令处理 */
static struct pbuf *tcp_echoserver_handle_text_cmd(const char *data, uint16_t len);

/* SPI 数据采集 */
static uint16_t spi_read_adc(SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin);
static uint8_t  iap_data_check(uint8_t *data, uint16_t len);
/* SPI 采样中断逻辑现在放在 main.c 的 HAL_TIM_PeriodElapsedCallback (TIM7 分支) */
void            tcp_echoserver_tim7_isr(void);


/* ========================================================================== */
/*  二进制帧解析                                                              */
/* ========================================================================== */

/**
  * @brief  解析二进制命令帧，校验帧格式
  * @param  data: 接收数据指针
  * @param  len:  数据长度
  * @retval 命令码 (0x05/0x06/0x07)，-1 表示非二进制帧
  */
static int tcp_echoserver_parse_binary_frame(const uint8_t *data, uint16_t len)
{
  if (len != FRAME_LEN)
    return -1;

  if (data[0] != FRAME_SOF)
    return -1;

  if (!((data[6] == FRAME_EOF1 && data[7] == FRAME_EOF2) ||
        (data[6] == 0x0D && data[7] == 0x0A)))
    return -1;

  /* 返回命令码 */
  return (int)data[2];
}


/* ========================================================================== */
/*  二进制命令处理: 返回响应 pbuf (调用者负责释放 rx_pbuf)                      */
/* ========================================================================== */

/**
  * @brief  根据命令码生成响应帧
  * @param  cmd: 命令码
  * @param  es:  当前连接状态结构体
  * @retval 响应 pbuf，失败返回 NULL
  */
static char tcp_echoserver_active_slot(void)
{
  uint32_t w = *(volatile uint32_t *)BOOT_SLOT_ADDR;
  if ((w & 0xFFFF0000U) != BOOT_SLOT_MAGIC)
    return '?';
  return (BOOT_SLOT_DECODE(w) == 1U) ? 'B' : 'A';  /* 槽 id: 0=A, 1=B */
}

static struct pbuf *tcp_echoserver_handle_binary_cmd(uint8_t cmd, struct tcp_echoserver_struct *es,
                                                     const uint8_t *frame)
{
  struct pbuf *tx_pbuf;
  uint8_t *payload;

  switch (cmd)
  {
  case CMD_START_STREAM:   /* 0x05 — 开始发送 SPI 数据 */
    es->streaming = 1;
    tcp_echoserver_stream_es = es;

    /* 初始化采集状态 */
    stream_enabled = 0;  /* 先关, 等 TIM7 启动后再开 */
    spi_sample_idx = 0;
    spi_avg_cnt = 0;
    stream_packet_ready = 0;
    spi_acc[0] = 0;
    spi_acc[1] = 0;
    spi_acc[2] = 0;
    spi_pkg_index = 0;

    /* 预填充双缓冲帧头 */
    for (uint8_t b = 0; b < 2U; b++)
    {
      stream_frame[b][0] = 0x02;
      stream_frame[b][1] = 0x01;
      stream_frame[b][2] = 0x25;
      stream_frame[b][3] = 0x31;
      stream_frame[b][4] = 0x32;
      stream_frame[b][5] = 0x33;
      stream_frame[b][6] = 0x34;
    }
    stream_wr_buf = 0U;
    stream_rd_buf = 0U;

    /* 启动 200μs 硬件定时器 */
    stream_enabled = 1;
    HAL_TIM_Base_Start_IT(&htim7);

    /* 回复 ACK */
    tx_pbuf = pbuf_alloc(PBUF_TRANSPORT, FRAME_LEN, PBUF_RAM);
    if (tx_pbuf == NULL) return NULL;
    payload = (uint8_t *)tx_pbuf->payload;
    payload[0] = FRAME_SOF;
    payload[1] = 0x01;
    payload[2] = CMD_START_STREAM;
    payload[3] = 0x00;
    payload[4] = 0x00;
    payload[5] = 0x00;
    payload[6] = FRAME_EOF1;
    payload[7] = FRAME_EOF2;
    return tx_pbuf;

  case CMD_STOP_STREAM:    /* 0x06 — 停止发送 SPI 数据 */
    es->streaming = 0;
    stream_enabled = 0;
    tcp_echoserver_stream_es = NULL;
    HAL_TIM_Base_Stop_IT(&htim7);

    /* 回复 ACK */
    tx_pbuf = pbuf_alloc(PBUF_TRANSPORT, FRAME_LEN, PBUF_RAM);
    if (tx_pbuf == NULL) return NULL;
    payload = (uint8_t *)tx_pbuf->payload;
    payload[0] = FRAME_SOF;
    payload[1] = 0x01;
    payload[2] = CMD_STOP_STREAM;
    payload[3] = 0x00;
    payload[4] = 0x00;
    payload[5] = 0x00;
    payload[6] = FRAME_EOF1;
    payload[7] = FRAME_EOF2;
    return tx_pbuf;

  case CMD_READ_VERSION:   /* 0x07 — 读版本号 */
    {
      int resp_len;
      char resp[96];
      char ip_str[20];
      net_config_format_active_ip(ip_str, sizeof(ip_str));
      resp_len = snprintf(resp, sizeof(resp),
        "Version %d.%d.%d, STM32F407 + DP83848 + ADS8354\r\n"
        "Active IP: %s (idx %u)\r\n"
        "Active slot: %c\r\n",
        FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH,
        ip_str, (unsigned)net_config_get_active_index(),
        tcp_echoserver_active_slot());

      tx_pbuf = pbuf_alloc(PBUF_TRANSPORT, resp_len, PBUF_RAM);
      if (tx_pbuf == NULL) return NULL;
      memcpy(tx_pbuf->payload, resp, resp_len);
      return tx_pbuf;
    }


  case CMD_HEALTH_CHECK:    /* 0x08 0D 0A— 健康监测 */
    {
      uint8_t resp[HEALTH_RESP_LEN];

      resp[0] = 0x02;
      resp[1] = 0x01;
      resp[2] = 0x25;

      resp[3] = FW_VERSION_MAJOR;
      resp[4] = FW_VERSION_MINOR;
      resp[5] = FW_VERSION_PATCH;
      resp[6] = 0x00;

      resp[7] = health_check_index++;

      resp[8] = (uint8_t)(HEALTH_TEMP_EXP>>8);
      resp[9] = (uint8_t)(HEALTH_TEMP_EXP&0xFF);
      resp[10]= (uint8_t)(HEALTH_HUMI_EXP>>8);
      resp[11]= (uint8_t)(HEALTH_HUMI_EXP&0xFF);

      uint16_t adc1 = spi_read_adc(&hspi1, SPI1_CS_PORT, SPI1_CS_PIN);
      uint16_t adc2 = spi_read_adc(&hspi2, SPI2_CS_PORT, SPI2_CS_PIN);
      uint16_t adc3 = spi_read_adc(&hspi3, SPI3_CS_PORT, SPI3_CS_PIN);

      resp[12]= 0x00;
      resp[13]= (uint8_t)(adc1>>8); 
      resp[14]= (uint8_t)(adc1&0xFF);
      resp[15]= 0x00;
      resp[16]= (uint8_t)(adc2>>8); 
      resp[17]= (uint8_t)(adc2&0xFF);
      resp[18]= 0x00;
      resp[19]= (uint8_t)(adc3>>8); 
      resp[20]= (uint8_t)(adc3&0xFF);

      resp[21]= iap_data_check(resp, 21);
      
      resp[22]= 0xAD;
      resp[23]= 0xDA;
      resp[24]= 0xDD;
      resp[25]= 0xAA;
    
      tx_pbuf = pbuf_alloc(PBUF_TRANSPORT, HEALTH_RESP_LEN, PBUF_RAM);
      if (tx_pbuf == NULL)
        return NULL;
      memcpy(tx_pbuf->payload, resp, HEALTH_RESP_LEN);
      return tx_pbuf;



    }



  case CMD_LIST_IP:        /* 0x09 — 返回 Flash 中的 IP 列表 */
    {
      uint8_t list_buf[NET_CONFIG_LIST_RESP_LEN];
      uint16_t resp_len;

      resp_len = net_config_build_list_response(list_buf, sizeof(list_buf));
      if (resp_len == 0)
        return NULL;

      tx_pbuf = pbuf_alloc(PBUF_TRANSPORT, resp_len, PBUF_RAM);
      if (tx_pbuf == NULL)
        return NULL;
      memcpy(tx_pbuf->payload, list_buf, resp_len);
      return tx_pbuf;
    }

  case CMD_RESTORE_DEFAULT_IP: /* 0x0B — 恢复 index 0 (172.16.1.69) */
    {
      if (es->streaming)
      {
        es->streaming = 0;
        stream_enabled = 0;
        tcp_echoserver_stream_es = NULL;
        HAL_TIM_Base_Stop_IT(&htim7);
      }

      /* 真正的 restore(写Flash+切IP) 延后到 ACK 发完后执行,
         否则 ACK 用新 IP 发不出去 */
      es->pending_ip_switch = NET_CONFIG_ENTRY_IDX;   /* 标记: ACK 发完后切到 idx 0 */

      tx_pbuf = pbuf_alloc(PBUF_TRANSPORT, FRAME_LEN, PBUF_RAM);
      if (tx_pbuf == NULL)
      {
        es->pending_ip_switch = 0xFFU;
        return NULL;
      }
      payload = (uint8_t *)tx_pbuf->payload;
      payload[0] = FRAME_SOF;
      payload[1] = 0x01;
      payload[2] = CMD_RESTORE_DEFAULT_IP;
      payload[3] = NET_CONFIG_ENTRY_IDX;
      payload[4] = 0x00;
      payload[5] = 0x00;
      payload[6] = FRAME_EOF1;
      payload[7] = FRAME_EOF2;
      return tx_pbuf;
    }

  case CMD_SELECT_IP:      /* 0x0A — 选择 index，写 Flash，切换 netif */
    {
      uint8_t index;

      if (frame == NULL)
        return NULL;

      /* 切换 IP 前停止数据流 */
      if (es->streaming)
      {
        es->streaming = 0;
        stream_enabled = 0;
        tcp_echoserver_stream_es = NULL;
        HAL_TIM_Base_Stop_IT(&htim7);
      }

      index = frame[3];
      /* 先校验 idx 是否有效; 真正的 select(写Flash+切IP) 延后到 ACK 发完后
         (tcp_echoserver_sent 回调里执行), 否则 ACK 用新 IP 发不出去 */
      if (index >= net_config_get_count())
        return NULL;
      {
        uint8_t dummy_ip[4];
        if (net_config_get_slot_ip(index, dummy_ip) != 0)   /* slot 是否 valid */
          return NULL;
      }
      es->pending_ip_switch = index;   /* 标记: ACK 发完后切到这个 idx */

      tx_pbuf = pbuf_alloc(PBUF_TRANSPORT, FRAME_LEN, PBUF_RAM);
      if (tx_pbuf == NULL)
      {
        es->pending_ip_switch = 0xFFU;
        return NULL;
      }
      payload = (uint8_t *)tx_pbuf->payload;
      payload[0] = FRAME_SOF;
      payload[1] = 0x01;
      payload[2] = CMD_SELECT_IP;
      payload[3] = index;
      payload[4] = 0x00;
      payload[5] = 0x00;
      payload[6] = FRAME_EOF1;
      payload[7] = FRAME_EOF2;
      return tx_pbuf;
    }

  case CMD_ENTER_BOOTLOADER:   /* 0x0C — 请求升级，复位后 Bootloader 进入 IAP/TFTP */
    {
      if (es->streaming)
      {
        es->streaming = 0;
        stream_enabled = 0;
        tcp_echoserver_stream_es = NULL;
        HAL_TIM_Base_Stop_IT(&htim7);
      }

      /* 写 BOOT_REQUEST_UPGRADE 到 BOOT_REQUEST_ADDR (sector 3 末尾).
       * 关中断 → 写 Flash → 开中断，防止 Flash 编程期间被 DMA/ETH 中断打断。 */
      {
        __disable_irq();
        HAL_FLASH_Unlock();
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          BOOT_REQUEST_ADDR,
                          BOOT_REQUEST_UPGRADE);
        HAL_FLASH_Lock();
        __enable_irq();
      }

      tx_pbuf = pbuf_alloc(PBUF_TRANSPORT, FRAME_LEN, PBUF_RAM);
      if (tx_pbuf != NULL) {
        uint32_t br = *(volatile uint32_t *)BOOT_REQUEST_ADDR;
        payload = (uint8_t *)tx_pbuf->payload;
        payload[0] = FRAME_SOF;
        payload[1] = 0x01;
        payload[2] = CMD_ENTER_BOOTLOADER;
        payload[3] = (uint8_t)(br);
        payload[4] = (uint8_t)(br >> 8);
        payload[5] = (uint8_t)(br >> 16);
        payload[6] = (uint8_t)(br >> 24);
        payload[7] = FRAME_EOF2;
      }

      /* Give time for pending packets + PHY to settle */
      HAL_Delay(500);

      NVIC_SystemReset();
      return tx_pbuf;
    }

  default:
    return NULL;
  }
}


/* ========================================================================== */
/*  文本命令处理                                                              */
/* ========================================================================== */

static struct pbuf *tcp_echoserver_handle_text_cmd(const char *data, uint16_t len)
{
  char cmd[32];
  char response[320];
  int resp_len;
  uint16_t i, j;

  if (len == 0)
    return NULL;

  /* 提取第一个单词 (到空格/回车/换行截止) */
  memset(cmd, 0, sizeof(cmd));
  for (i = 0, j = 0; i < len && j < sizeof(cmd) - 1; i++)
  {
    char c = data[i];
    if (c == '\0' || c == '\r' || c == '\n' || c == ' ')
      break;
    cmd[j++] = c;
  }

  /* 转小写 */
  for (i = 0; cmd[i]; i++)
    cmd[i] = (char)tolower((unsigned char)cmd[i]);

  /* --- 命令匹配 --- */
  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
  {
    resp_len = snprintf(response, sizeof(response),
      "Commands:\r\n"
      "  hello    - Greeting\r\n"
      "  status   - System status\r\n"
      "  ver      - Firmware version\r\n"
      "  uptime   - System uptime\r\n"
      "  drop     - Drop statistics\r\n"
      "  help/?   - This help\r\n"
      "  <other>  - Echo back\r\n"
      "Binary frames (8 bytes): 0x02 0x01 <CMD> ... 0xDD 0xAA\r\n"
      "  0x08 list IP | 0x0A select IP | 0x0B restore default (idx0)\r\n");
  }
  else if (strcmp(cmd, "hello") == 0)
  {
    resp_len = snprintf(response, sizeof(response),
      "Hello from STM32F407 + DP83848!\r\n");
  }
  else if (strcmp(cmd, "status") == 0)
  {
    char ip_str[20];
    net_config_format_active_ip(ip_str, sizeof(ip_str));
    resp_len = snprintf(response, sizeof(response),
      "System Status: Running\r\n"
      "IP: %s (idx %u)\r\n"
      "Entry IP: 172.16.1.69 (idx0, first 5min after boot)\r\n"
      "Port: %d\r\n"
      "Streaming: %s\r\n",
      ip_str,
      (unsigned)net_config_get_active_index(),
      TCP_ECHO_PORT,
      (tcp_echoserver_stream_es != NULL) ? "ON" : "OFF");
  }
  else if (strcmp(cmd, "ver") == 0 || strcmp(cmd, "version") == 0)
  {
    resp_len = snprintf(response, sizeof(response),
      "TCP Echo Server v%d.%d.%d\r\n"
      "STM32F407 + DP83848 + LwIP\r\n"
      "Active slot: %c\r\n",
      FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH,
      tcp_echoserver_active_slot());
  }
  else if (strcmp(cmd, "uptime") == 0)
  {
    uint32_t ms = HAL_GetTick();
    uint32_t s  = ms / 1000;
    uint32_t m  = s / 60;
    uint32_t h  = m / 60;
    resp_len = snprintf(response, sizeof(response),
      "Uptime: %lums = %luh %lum %lus\r\n",
      ms, h, m % 60, s % 60);
  }
  else if (strcmp(cmd, "drop") == 0)
  {
    resp_len = snprintf(response, sizeof(response),
      "Drop: total=%lu consec=%lu stall=%u\r\n",
      (unsigned long)stream_drop_cnt,
      (unsigned long)stream_drop_consec,
      (unsigned)stream_stalled);
  }
  else
  {
    return NULL;  /* 不匹配 → echo 原数据 */
  }

  {
    struct pbuf *tx_pbuf = pbuf_alloc(PBUF_TRANSPORT, resp_len, PBUF_RAM);
    if (tx_pbuf != NULL)
      memcpy(tx_pbuf->payload, response, resp_len);
    return tx_pbuf;
  }
}


/* ========================================================================== */
/*  命令处理入口: 先尝试二进制帧，再尝试文本命令，都不匹配则 echo               */
/* ========================================================================== */

static struct pbuf *tcp_echoserver_process_command(struct pbuf *rx_pbuf, struct tcp_echoserver_struct *es)
{
  uint8_t *data;
  uint16_t len;
  int binary_cmd;
  struct pbuf *resp;

  len = rx_pbuf->tot_len;
  if (len == 0)
    return NULL;

  data = (uint8_t *)rx_pbuf->payload;

  /* 1. 尝试二进制帧解析 */
  binary_cmd = tcp_echoserver_parse_binary_frame(data, len);
  if (binary_cmd >= 0)
  {
    resp = tcp_echoserver_handle_binary_cmd((uint8_t)binary_cmd, es, data);
    if (resp != NULL)
      return resp;
    /* 二进制命令处理失败, 回退到 echo */
  }

  /* 2. 尝试文本命令 */
  resp = tcp_echoserver_handle_text_cmd((const char *)data, len);
  if (resp != NULL)
    return resp;

  /* 3. 都不匹配 → NULL, 调用者将原数据 echo 回去 */
  return NULL;
}


/* ========================================================================== */
/*  SPI 数据采集函数                                                           */
/* ========================================================================== */

/**
  * @brief  从单个 SPI 通道读取 16-bit ADC 数据 (与 Ads8354ReadConvers 等价)
  * @param  hspi:    SPI 句柄
  * @param  cs_port: CS 引脚端口
  * @param  cs_pin:  CS 引脚编号
  * @retval 16-bit ADC 值 (rxBuf[2]<<8 | rxBuf[3])
  */
static uint16_t spi_read_adc(SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin)
{
  uint8_t txBuf[6] = {0};
  uint8_t rxBuf[6] = {0};

  HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_RESET);
  HAL_SPI_TransmitReceive(hspi, txBuf, rxBuf, 6, 10);
  HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_SET);

  return ((uint16_t)rxBuf[2] << 8) | rxBuf[3];
}

/**
  * @brief  简单求和校验 (与 ads8354.c IAP_DataCheck 一致)
  * @param  data: 数据指针
  * @param  len:  数据长度
  * @retval 低 8 位校验和
  */
static uint8_t iap_data_check(uint8_t *data, uint16_t len)
{
  uint8_t sum = 0;
  uint16_t i;
  for (i = 0; i < len; i++)
  {
    sum += data[i];
  }
  return sum;
}

/**
  * @brief  TIM7 周期到回调 (由 CubeMX 生成的 TIM7_IRQHandler → HAL_TIM_IRQHandler →
  *         main.c HAL_TIM_PeriodElapsedCallback 检测到 TIM7 后转调本函数)
  * @note   每 200μs 触发: 读 3 路 SPI → 5 次平均 → 1ms 一个数据点 → 80 点后置 packet_ready
  */
void tcp_echoserver_tim7_isr(void)
{
  if (!stream_enabled)
    return;

  /* 读 3 路 SPI ADC */
  uint16_t r0 = spi_read_adc(&hspi1, SPI1_CS_PORT, SPI1_CS_PIN);
  uint16_t r1 = spi_read_adc(&hspi2, SPI2_CS_PORT, SPI2_CS_PIN);
  uint16_t r2 = spi_read_adc(&hspi3, SPI3_CS_PORT, SPI3_CS_PIN);

  spi_acc[0] += (int32_t)r0;
  spi_acc[1] += (int32_t)r1;
  spi_acc[2] += (int32_t)r2;
  spi_avg_cnt++;

  if (spi_avg_cnt >= SPI_AVG_CNT)
  {
    uint8_t *wr = stream_frame[stream_wr_buf];

    /* SPI1 块: 前 160 字节 (0..159) */
    wr[PKT_HDR_LEN + spi_sample_idx * 2 + 0] = (uint8_t)((spi_acc[0] / SPI_AVG_CNT) >> 8);
    wr[PKT_HDR_LEN + spi_sample_idx * 2 + 1] = (uint8_t)((spi_acc[0] / SPI_AVG_CNT) & 0xFF);
    /* SPI2 块: 中间 160 字节 (160..319) */
    wr[PKT_HDR_LEN + 160 + spi_sample_idx * 2 + 0] = (uint8_t)((spi_acc[1] / SPI_AVG_CNT) >> 8);
    wr[PKT_HDR_LEN + 160 + spi_sample_idx * 2 + 1] = (uint8_t)((spi_acc[1] / SPI_AVG_CNT) & 0xFF);
    /* SPI3 块: 最后 160 字节 (320..479) */
    wr[PKT_HDR_LEN + 320 + spi_sample_idx * 2 + 0] = (uint8_t)((spi_acc[2] / SPI_AVG_CNT) >> 8);
    wr[PKT_HDR_LEN + 320 + spi_sample_idx * 2 + 1] = (uint8_t)((spi_acc[2] / SPI_AVG_CNT) & 0xFF);

    spi_acc[0] = 0;
    spi_acc[1] = 0;
    spi_acc[2] = 0;
    spi_avg_cnt = 0;
    spi_sample_idx++;

    if (spi_sample_idx >= SPI_SAMPLE_NUM)
    {
      spi_sample_idx = 0;
      if (stream_packet_ready == 0U)
      {
        stream_rd_buf = stream_wr_buf;
        stream_packet_ready = 1U;
        stream_wr_buf ^= 1U;
      }
      else
      {
        stream_drop_cnt++;
        stream_drop_consec++;
      }
      /* 上一帧尚未发出则丢弃本帧，避免覆盖 rd 缓冲 */
    }
    
  }
}


/* ========================================================================== */
/*  流式 SPI 数据推送 (在主循环中调用)                                          */
/* ========================================================================== */

/**
  * @brief  主循环调用: 检查 packet_ready → 组帧 + TCP 发送
  */
void tcp_echoserver_spi_stream_poll(void)
{
  struct tcp_echoserver_struct *es;
  uint8_t *tx;
  uint16_t tail;
  err_t err;

  if (!stream_packet_ready)
    return;

  if (stream_drop_consec >= 25U)
    stream_stalled = 1U;

  if (!stream_enabled)
  {
    stream_packet_ready = 0;
    return;
  }

  es = tcp_echoserver_stream_es;
  if (es == NULL || es->pcb == NULL)
    return;

  /* 发送队列不足时保留 ready，下轮主循环重试（不丢帧） */
  if (tcp_sndbuf(es->pcb) < PKT_FRAME_LEN)
    return;

  tx = stream_frame[stream_rd_buf];
  tail = PKT_HDR_LEN + PKT_DATA_LEN;

  tx[0] = 0x02;
  tx[1] = 0x01;
  tx[2] = 0x25;
  tx[3] = 0x31;
  tx[4] = 0x32;
  tx[5] = 0x33;
  tx[6] = 0x34;

  tx[tail + 0] = 0;
  tx[tail + 1] = 0;
  tx[tail + 2] = 0;
  tx[tail + 3] = 0;
  tx[tail + 4] = (uint8_t)(spi_pkg_index >> 8);
  tx[tail + 5] = (uint8_t)(spi_pkg_index & 0xFF);
  spi_pkg_index++;

  tx[tail + 6] = iap_data_check(tx, tail + 6);
  tx[tail + 7]  = 0xAD;
  tx[tail + 8]  = 0xDA;
  tx[tail + 9]  = 0xDD;
  tx[tail + 10] = 0xAA;

  err = tcp_write(es->pcb, tx, PKT_FRAME_LEN, TCP_WRITE_FLAG_COPY);
  if (err != ERR_OK)
  {
    stream_drop_cnt++;
    stream_drop_consec++;
    return;
  }
    

  tcp_output(es->pcb);
  stream_packet_ready = 0U;
  stream_drop_consec = 0;
  stream_stalled = 0;
}


/* ========================================================================== */
/*  服务器初始化                                                               */
/* ========================================================================== */

/**
  * @brief  Initializes the tcp echo server
  */
void tcp_echoserver_init(void)
{
  tcp_echoserver_pcb = tcp_new();

  if (tcp_echoserver_pcb != NULL)
  {
    err_t err;

    err = tcp_bind(tcp_echoserver_pcb, IP_ADDR_ANY, TCP_ECHO_PORT);

    if (err == ERR_OK)
    {
      tcp_echoserver_pcb = tcp_listen(tcp_echoserver_pcb);
      tcp_accept(tcp_echoserver_pcb, tcp_echoserver_accept);
    }
    else
    {
      memp_free(MEMP_TCP_PCB, tcp_echoserver_pcb);
    }
  }

  /* TIM7 已由 CubeMX (tim.c MX_TIM7_Init, main.c 调用) 初始化为 200us 周期,
     此处不重复初始化; 等 0x05 命令到达时再用 HAL_TIM_Base_Start_IT(&htim7) 启动 */

  /* 初始化三路 ADS8354 ADC 为 single SDO 模式 */
  Ads8354Init();
}


/* ========================================================================== */
/*  LwIP 回调: accept                                                          */
/* ========================================================================== */

static err_t tcp_echoserver_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
  err_t ret_err;
  struct tcp_echoserver_struct *es;

  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(err);

  tcp_setprio(newpcb, TCP_PRIO_MIN);

  es = (struct tcp_echoserver_struct *)mem_malloc(sizeof(struct tcp_echoserver_struct));
  if (es != NULL)
  {
    es->state     = ES_ACCEPTED;
    es->pcb       = newpcb;
    es->retries   = 0;
    es->p         = NULL;
    es->streaming = 0;
    es->pending_ip_switch = 0xFFU;   /* 初始无待切换 IP */

    tcp_arg(newpcb, es);
    tcp_recv(newpcb, tcp_echoserver_recv);
    tcp_err(newpcb, tcp_echoserver_error);
    tcp_poll(newpcb, tcp_echoserver_poll, 0);

    ret_err = ERR_OK;
  }
  else
  {
    tcp_echoserver_connection_close(newpcb, es);
    ret_err = ERR_MEM;
  }
  return ret_err;
}


/* ========================================================================== */
/*  LwIP 回调: recv                                                            */
/* ========================================================================== */

static err_t tcp_echoserver_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  struct tcp_echoserver_struct *es;
  err_t ret_err;

  LWIP_ASSERT("arg != NULL", arg != NULL);
  es = (struct tcp_echoserver_struct *)arg;

  /* 客户端关闭连接 */
  if (p == NULL)
  {
    es->state = ES_CLOSING;
    if (es->p == NULL)
    {
      /* 清理流式发送状态 */
      if (tcp_echoserver_stream_es == es)
        tcp_echoserver_stream_es = NULL;
      tcp_echoserver_connection_close(tpcb, es);
    }
    else
    {
      tcp_sent(tpcb, tcp_echoserver_sent);
      tcp_echoserver_send(tpcb, es);
    }
    ret_err = ERR_OK;
  }
  else if (err != ERR_OK)
  {
    if (p != NULL)
    {
      es->p = NULL;
      pbuf_free(p);
    }
    ret_err = err;
  }
  else if (es->state == ES_ACCEPTED)
  {
    /* 第一包数据: 尝试命令匹配 */
    es->state = ES_RECEIVED;

    {
      struct pbuf *resp = tcp_echoserver_process_command(p, es);
      if (resp != NULL)
      {
        pbuf_free(p);
        es->p = resp;
      }
      else
      {
        es->p = p;  /* echo 原数据 */
      }
    }

    tcp_sent(tpcb, tcp_echoserver_sent);
    tcp_echoserver_send(tpcb, es);
    ret_err = ERR_OK;
  }
  else if (es->state == ES_RECEIVED)
  {
    if (es->p == NULL)
    {
      {
        struct pbuf *resp = tcp_echoserver_process_command(p, es);
        if (resp != NULL)
        {
          pbuf_free(p);
          es->p = resp;
        }
        else
        {
          es->p = p;
        }
      }
      tcp_echoserver_send(tpcb, es);
    }
    else
    {
      struct pbuf *ptr = es->p;
      pbuf_chain(ptr, p);
    }
    ret_err = ERR_OK;
  }
  else if (es->state == ES_CLOSING)
  {
    tcp_recved(tpcb, p->tot_len);
    es->p = NULL;
    pbuf_free(p);
    ret_err = ERR_OK;
  }
  else
  {
    tcp_recved(tpcb, p->tot_len);
    es->p = NULL;
    pbuf_free(p);
    ret_err = ERR_OK;
  }
  return ret_err;
}


/* ========================================================================== */
/*  LwIP 回调: error                                                           */
/* ========================================================================== */

static void tcp_echoserver_error(void *arg, err_t err)
{
  struct tcp_echoserver_struct *es;

  LWIP_UNUSED_ARG(err);

  es = (struct tcp_echoserver_struct *)arg;
  if (es != NULL)
  {
    /* 清理流式发送状态 */
    if (tcp_echoserver_stream_es == es)
      tcp_echoserver_stream_es = NULL;

    mem_free(es);
  }
}


/* ========================================================================== */
/*  LwIP 回调: poll                                                            */
/* ========================================================================== */

static err_t tcp_echoserver_poll(void *arg, struct tcp_pcb *tpcb)
{
  err_t ret_err;
  struct tcp_echoserver_struct *es;

  es = (struct tcp_echoserver_struct *)arg;
  if (es != NULL)
  {
    if (es->p != NULL)
    {
      tcp_sent(tpcb, tcp_echoserver_sent);
      tcp_echoserver_send(tpcb, es);
    }
    else
    {
      if (es->state == ES_CLOSING)
      {
        if (tcp_echoserver_stream_es == es)
          tcp_echoserver_stream_es = NULL;
        tcp_echoserver_connection_close(tpcb, es);
      }
    }
    ret_err = ERR_OK;
  }
  else
  {
    tcp_abort(tpcb);
    ret_err = ERR_ABRT;
  }
  return ret_err;
}


/* ========================================================================== */
/*  LwIP 回调: sent                                                            */
/* ========================================================================== */

static err_t tcp_echoserver_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
  struct tcp_echoserver_struct *es;

  LWIP_UNUSED_ARG(len);

  es = (struct tcp_echoserver_struct *)arg;
  es->retries = 0;

  if (es->p != NULL)
  {
    tcp_sent(tpcb, tcp_echoserver_sent);
    tcp_echoserver_send(tpcb, es);
  }
  else
  {
    /* ACK 已发完: 若有待切换 IP, 现在写 Flash + 切 netif, 然后关闭旧连接.
       必须在切换后关闭连接, 因为切 IP 后这条以旧 IP 建立的 pcb 已无效 */
    if (es->pending_ip_switch != 0xFFU)
    {
      uint8_t idx = es->pending_ip_switch;
      es->pending_ip_switch = 0xFFU;

      if (idx == NET_CONFIG_ENTRY_IDX)
        (void)net_config_restore_default();   /* 0x0B: 切回 idx 0 */
      else
        (void)net_config_select(idx);          /* 0x0A: 切到指定 idx */

      if (tcp_echoserver_stream_es == es)
        tcp_echoserver_stream_es = NULL;
      tcp_echoserver_connection_close(tpcb, es);
    }
    else if (es->state == ES_CLOSING)
    {
      if (tcp_echoserver_stream_es == es)
        tcp_echoserver_stream_es = NULL;
      tcp_echoserver_connection_close(tpcb, es);
    }
  }
  return ERR_OK;
}


/* ========================================================================== */
/*  数据发送                                                                   */
/* ========================================================================== */

static void tcp_echoserver_send(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es)
{
  struct pbuf *ptr;
  err_t wr_err = ERR_OK;

  while ((wr_err == ERR_OK) &&
         (es->p != NULL) &&
         (es->p->len <= tcp_sndbuf(tpcb)))
  {
    ptr = es->p;
    wr_err = tcp_write(tpcb, ptr->payload, ptr->len, 1);

    if (wr_err == ERR_OK)
    {
      u16_t plen;
      u8_t freed;

      plen = ptr->len;
      es->p = ptr->next;

      if (es->p != NULL)
      {
        pbuf_ref(es->p);
      }

      do
      {
        freed = pbuf_free(ptr);
      }
      while (freed == 0);

      tcp_recved(tpcb, plen);
    }
    else if (wr_err == ERR_MEM)
    {
      es->p = ptr;
    }
  }
}


/* ========================================================================== */
/*  连接关闭                                                                   */
/* ========================================================================== */

static void tcp_echoserver_connection_close(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es)
{
  tcp_arg(tpcb, NULL);
  tcp_sent(tpcb, NULL);
  tcp_recv(tpcb, NULL);
  tcp_err(tpcb, NULL);
  tcp_poll(tpcb, NULL, 0);

  if (es != NULL)
  {
    if (tcp_echoserver_stream_es == es)
      tcp_echoserver_stream_es = NULL;
    mem_free(es);
  }

  tcp_close(tpcb);
}

#endif /* LWIP_TCP */
