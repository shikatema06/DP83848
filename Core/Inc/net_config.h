#ifndef NET_CONFIG_H
#define NET_CONFIG_H

#include <stdint.h>

#define NET_IP_LIST_MAX       8U
#define NET_CONFIG_ENTRY_IDX  0U
#define NET_CONFIG_WINDOW_MS  30000U  /* 上电后 30s 内保持 172.16.1.69 (idx 0), 便于配置/ping;
                                         30s 后自动切到 Flash 中记录的 active_idx */

/* LIST_IP 响应帧长度: SOF+ADDR+CMD+count+active + 8*(ip4+valid) + EOF */
#define NET_CONFIG_LIST_RESP_LEN  (3U + 2U + (NET_IP_LIST_MAX * 5U) + 2U)

void net_config_init(void);
void net_config_poll(void);
int net_config_restore_default(void);
int net_config_get_entry_ip(uint8_t ip[4], uint8_t mask[4]);
uint8_t net_config_get_count(void);
uint8_t net_config_get_active_index(void);
int net_config_get_active_ip(uint8_t ip[4], uint8_t mask[4]);
int net_config_get_slot_ip(uint8_t index, uint8_t ip[4]);
int net_config_select(uint8_t index);
int net_config_apply_to_netif(void);
uint16_t net_config_build_list_response(uint8_t *buf, uint16_t buf_len);
void net_config_format_active_ip(char *buf, unsigned buf_len);

#endif /* NET_CONFIG_H */
