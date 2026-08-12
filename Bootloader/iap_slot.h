#ifndef __IAP_SLOT_H__
#define __IAP_SLOT_H__


#include <stdint.h>
#include "flash_if.h"

#define SLOT_META_MAGIC     0x49415053U   /* "IAPS" */
#define BOOT_SLOT_MAGIC     0x55AA0000U   /* BOOT_SLOT_ADDR 高 16 位 */
#define BOOT_SLOT_ENCODE(id)     (BOOT_SLOT_MAGIC | ((id) & 0xFFU))
#define BOOT_SLOT_DECODE(word)   ((word) & 0xFFU)
#define SLOT_A_META_ADDR    0x0800FFD0U
#define SLOT_B_META_ADDR    0x0800FFE0U


#define IAP_SLOT_OK   0
#define IAP_SLOT_ERR -1


//槽编号枚举
typedef enum {
    SLOT_ID_A = 0,
    SLOT_ID_B = 1,
    SLOT_ID_NONE = 0xFF   /* 找不到合法槽时用 */
} boot_slot_id_t;

// 槽装填枚举
typedef enum {
    SLOT_IMG_EMPTY   = 0x00,
    SLOT_IMG_VALID   = 0x01,
    SLOT_IMG_NEW     = 0x02,
    SLOT_IMG_INVALID = 0x03,
    SLOT_IMG_OLD     = 0x04
} slot_img_state_t;

//双阶段提交状态
typedef enum {
    COMMIT_IDLE     = 0x00,
    COMMIT_ERASING  = 0x01,
    COMMIT_PENDING  = 0x02,
    COMMIT_DONE     = 0x03
} slot_commit_state_t;

typedef struct 
{
    uint32_t magic;        /*  SLOT_META_MAGIC 0x49415053U */
    uint32_t img_len;      /* 镜像实际字节数 */
    uint32_t img_crc32;    /* 镜像 CRC32（覆盖 img_len 字节，不含 meta 本身） */
    uint8_t  img_state;    /* slot_img_state_t */
    uint8_t  commit_state; /* slot_commit_state_t */
    uint16_t reserved;
} slot_meta_t;   /* 建议 16 字节对齐，方便 Flash 按字写 */





/* 初始化：校验两槽 meta，必要时修复（如 ERASING 卡死→INVALID） */
void iap_slot_init(void);

/* 取当前 active 槽 id（读 BOOT_SLOT_ADDR，magic不匹配或active_id非法时返回SLOT_ID_NONE） */
boot_slot_id_t iap_slot_get_active(void);

/* 取备用槽 id（active ^ 1） */
boot_slot_id_t iap_slot_get_spare(void);

/* 计算某槽镜像 CRC32（覆盖 [slot_start, slot_start+meta->img_len)） */
uint32_t  iap_slot_calc_crc(boot_slot_id_t id);

/* 校验某槽：读 meta→比对 CRC→返回 img_state */
slot_img_state_t iap_slot_validate(boot_slot_id_t id);

/* 标记某槽为 NEW（写 meta：magic+img_len+img_crc32+state=NEW） */
int8_t    iap_slot_mark_new(boot_slot_id_t id, uint32_t img_len, uint32_t img_crc32);

/* 提交：校验 CRC→写 meta state=VALID→翻转 BOOT_SLOT_ADDR→旧槽标记 OLD */
int8_t    iap_slot_commit(boot_slot_id_t id);

/* 回滚：active 槽无效时，找另一个 VALID 槽设为 active */
boot_slot_id_t iap_slot_rollback(void);



/* 取槽起始/结束地址的辅助宏或 inline */
uint32_t  iap_slot_start_addr(boot_slot_id_t id);
uint32_t  iap_slot_end_addr(boot_slot_id_t id);
uint32_t  iap_slot_meta_addr(boot_slot_id_t id);


#endif /* __IAP_SLOT_H__ */