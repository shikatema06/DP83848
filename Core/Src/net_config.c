/**
 * @file net_config.c
 * @brief Flash-backed IP list and active address (boot strategy B).
 */

#include "net_config.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

extern struct netif gnetif;

#define NET_CFG_FLASH_ADDR    0x080E0000U
#define NET_CFG_MAGIC         0x49504346U  /* "IPCF" */

typedef struct __attribute__((packed)) {
  uint8_t ip[4];
  uint8_t mask[4];
  uint8_t valid;
  uint8_t pad[3];
} net_ip_slot_t;

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t count;
  uint8_t active_idx;
  uint8_t reserved[2];
  net_ip_slot_t slots[NET_IP_LIST_MAX];
  uint32_t crc32;
} net_ip_table_t;

static net_ip_table_t s_table;
static uint8_t s_config_window;

static const uint8_t s_factory_ips[NET_IP_LIST_MAX][4] = {
  {172, 16, 1, 69},
  {172, 16, 1, 169},
  {172, 16, 1, 70},
  {172, 16, 1, 170},
  {172, 16, 69, 1},
  {172, 16, 169, 1},
  {172, 16, 70, 1},
  {172, 16, 170, 1},
};

static const uint8_t s_factory_mask[4] = {255, 255, 255, 0};

static uint32_t crc32_calc(const uint8_t *data, uint32_t len)
{
  uint32_t crc = 0xFFFFFFFFU;
  uint32_t i;
  uint32_t j;

  for (i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (j = 0; j < 8U; j++)
    {
      if (crc & 1U)
        crc = (crc >> 1) ^ 0xEDB88320U;
      else
        crc >>= 1;
    }
  }
  return crc ^ 0xFFFFFFFFU;
}

static uint32_t table_crc(const net_ip_table_t *t)
{
  return crc32_calc((const uint8_t *)t, (uint32_t)(sizeof(net_ip_table_t) - sizeof(uint32_t)));
}

static void table_set_defaults(net_ip_table_t *t)
{
  uint8_t i;

  memset(t, 0, sizeof(*t));
  t->magic = NET_CFG_MAGIC;
  t->count = NET_IP_LIST_MAX;
  t->active_idx = 0;

  for (i = 0; i < NET_IP_LIST_MAX; i++)
  {
    memcpy(t->slots[i].ip, s_factory_ips[i], 4);
    memcpy(t->slots[i].mask, s_factory_mask, 4);
    t->slots[i].valid = 1;
  }
  t->crc32 = table_crc(t);
}

static int slot_ip_matches_factory(const net_ip_slot_t *slot, uint8_t index)
{
  if (index >= NET_IP_LIST_MAX)
    return 0;
  if (!slot->valid)
    return 0;
  if (memcmp(slot->ip, s_factory_ips[index], 4) != 0)
    return 0;
  if (memcmp(slot->mask, s_factory_mask, 4) != 0)
    return 0;
  return 1;
}

static int table_valid(const net_ip_table_t *t)
{
  uint8_t i;

  if (t->magic != NET_CFG_MAGIC)
    return 0;
  if (t->count != NET_IP_LIST_MAX)
    return 0;
  if (t->active_idx >= t->count)
    return 0;
  if (t->crc32 != table_crc(t))
    return 0;
  for (i = 0; i < t->count; i++)
  {
    if (!slot_ip_matches_factory(&t->slots[i], i))
      return 0;
  }
  return 1;
}

static int flash_erase_sector(void)
{
  FLASH_EraseInitTypeDef erase;
  uint32_t sector_error = 0;
  HAL_StatusTypeDef st;

  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  erase.Sector = FLASH_SECTOR_11;
  erase.NbSectors = 1;

  HAL_FLASH_Unlock();
  st = HAL_FLASHEx_Erase(&erase, &sector_error);
  HAL_FLASH_Lock();
  return (st == HAL_OK) ? 0 : -1;
}

static int flash_program_table(const net_ip_table_t *t)
{
  uint32_t addr = NET_CFG_FLASH_ADDR;
  const uint32_t *src = (const uint32_t *)t;
  uint32_t words = (sizeof(net_ip_table_t) + 3U) / 4U;
  uint32_t i;
  HAL_StatusTypeDef st;

  HAL_FLASH_Unlock();
  for (i = 0; i < words; i++)
  {
    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, src[i]);
    if (st != HAL_OK)
    {
      HAL_FLASH_Lock();
      return -1;
    }
    addr += 4U;
  }
  HAL_FLASH_Lock();
  return 0;
}

static int table_save(void)
{
  s_table.crc32 = table_crc(&s_table);
  if (flash_erase_sector() != 0)
    return -1;
  return flash_program_table(&s_table);
}

void net_config_init(void)
{
  const net_ip_table_t *flash_table = (const net_ip_table_t *)NET_CFG_FLASH_ADDR;

  if (table_valid(flash_table))
    memcpy(&s_table, flash_table, sizeof(s_table));
  else
    table_set_defaults(&s_table);

  /* 上电 30s 内 netif 使用列表第 0 项，便于连接配置 IP */
  s_config_window = 1U;
}

int net_config_get_entry_ip(uint8_t ip[4], uint8_t mask[4])
{
  if (!slot_ip_matches_factory(&s_table.slots[NET_CONFIG_ENTRY_IDX], NET_CONFIG_ENTRY_IDX))
    return -1;

  memcpy(ip, s_table.slots[NET_CONFIG_ENTRY_IDX].ip, 4);
  memcpy(mask, s_table.slots[NET_CONFIG_ENTRY_IDX].mask, 4);
  return 0;
}

int net_config_restore_default(void)
{
  s_table.active_idx = NET_CONFIG_ENTRY_IDX;
  if (table_save() != 0)
    return -1;

  s_config_window = 0U;
  return net_config_apply_to_netif();
}

void net_config_poll(void)
{
  static uint32_t last_check_tick;

  /* 每秒检查一次，避免占用主循环影响 80ms 数据发送 */
  if ((HAL_GetTick() - last_check_tick) < 1000U)
    return;
  last_check_tick = HAL_GetTick();

  if (s_config_window == 0U)
    return;

  if (HAL_GetTick() < NET_CONFIG_WINDOW_MS)
    return;

  s_config_window = 0U;

  if (s_table.active_idx == NET_CONFIG_ENTRY_IDX)
    return;

  (void)net_config_apply_to_netif();
}

uint8_t net_config_get_count(void)
{
  return s_table.count;
}

uint8_t net_config_get_active_index(void)
{
  return s_table.active_idx;
}

int net_config_get_active_ip(uint8_t ip[4], uint8_t mask[4])
{
  const net_ip_slot_t *slot;

  if (s_table.active_idx >= s_table.count)
    return -1;

  slot = &s_table.slots[s_table.active_idx];
  if (!slot->valid)
    return -1;

  memcpy(ip, slot->ip, 4);
  memcpy(mask, slot->mask, 4);
  return 0;
}

static int net_config_get_runtime_ip(uint8_t ip[4], uint8_t mask[4])
{
  if (s_config_window != 0U)
    return net_config_get_entry_ip(ip, mask);
  return net_config_get_active_ip(ip, mask);
}

int net_config_get_slot_ip(uint8_t index, uint8_t ip[4])
{
  if (index >= s_table.count)
    return -1;
  if (!s_table.slots[index].valid)
    return -1;

  memcpy(ip, s_table.slots[index].ip, 4);
  return 0;
}

int net_config_apply_to_netif(void)
{
  ip4_addr_t ipaddr;
  ip4_addr_t netmask;
  uint8_t ip[4];
  uint8_t mask[4];

  if (net_config_get_active_ip(ip, mask) != 0)
    return -1;

  IP4_ADDR(&ipaddr, ip[0], ip[1], ip[2], ip[3]);
  IP4_ADDR(&netmask, mask[0], mask[1], mask[2], mask[3]);

  if (!netif_is_up(&gnetif))
    return -1;

  netif_set_ipaddr(&gnetif, &ipaddr);
  netif_set_netmask(&gnetif, &netmask);
  return 0;
}

int net_config_select(uint8_t index)
{
  if (index >= s_table.count)
    return -1;
  if (!s_table.slots[index].valid)
    return -1;

  s_table.active_idx = index;
  if (table_save() != 0)
    return -1;

  s_config_window = 0U;
  return net_config_apply_to_netif();
}

uint16_t net_config_build_list_response(uint8_t *buf, uint16_t buf_len)
{
  uint8_t i;
  uint16_t pos = 0;

  if (buf_len < NET_CONFIG_LIST_RESP_LEN)
    return 0;

  buf[pos++] = 0x02;
  buf[pos++] = 0x01;
  buf[pos++] = 0x08;
  buf[pos++] = s_table.count;
  buf[pos++] = s_table.active_idx;

  for (i = 0; i < NET_IP_LIST_MAX; i++)
  {
    memcpy(&buf[pos], s_table.slots[i].ip, 4);
    pos += 4;
    buf[pos++] = s_table.slots[i].valid;
  }

  buf[pos++] = 0xDD;
  buf[pos++] = 0xAA;
  return pos;
}

void net_config_format_active_ip(char *buf, unsigned buf_len)
{
  uint8_t ip[4];
  uint8_t mask[4];

  if (buf_len == 0)
    return;

  if (net_config_get_runtime_ip(ip, mask) != 0)
  {
    snprintf(buf, buf_len, "0.0.0.0");
    return;
  }

  snprintf(buf, buf_len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}
