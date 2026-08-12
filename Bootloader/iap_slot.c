
#include "iap_slot.h"
#include "flash_if.h"
#include <stdint.h>

static uint32_t crc32_calc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint32_t j = 0; j < 8U; j++)
        {
            if (crc & 1U)
                crc = (crc >> 1) ^ 0xEDB88320U;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFFU;
}
uint32_t  iap_slot_start_addr(boot_slot_id_t id)
{
    if (id == SLOT_ID_A) return SLOT_A_START_ADDR;
    if (id == SLOT_ID_B) return SLOT_B_START_ADDR;
    return 0;
}
uint32_t  iap_slot_end_addr(boot_slot_id_t id)
{
    if(id == SLOT_ID_A) return SLOT_A_END_ADDR;
    if(id == SLOT_ID_B) return SLOT_B_END_ADDR;
    return 0;
}
uint32_t  iap_slot_meta_addr(boot_slot_id_t id)
{
    if(id == SLOT_ID_A) return SLOT_A_META_ADDR;
    if(id == SLOT_ID_B) return SLOT_B_META_ADDR;
    return 0;
}
boot_slot_id_t iap_slot_get_active(void)
{
    uint32_t boot_slot = *(volatile uint32_t *)BOOT_SLOT_ADDR;

    if ((boot_slot&0xFFFF0000)!= BOOT_SLOT_MAGIC)
        return SLOT_ID_NONE;
    
    uint8_t active = boot_slot & 0x000000FF;
    if (active == 0) return SLOT_ID_A;
    if (active == 1) return SLOT_ID_B;
    return SLOT_ID_NONE;
}
boot_slot_id_t iap_slot_get_spare(void)
{
    boot_slot_id_t active = iap_slot_get_active();
    if (active == SLOT_ID_A) return SLOT_ID_B;
    if (active == SLOT_ID_B) return SLOT_ID_A;
    return SLOT_ID_A;

}
uint32_t  iap_slot_calc_crc(boot_slot_id_t id)
{
    if (id == SLOT_ID_NONE)
        return 0;

    slot_meta_t *meta = (slot_meta_t *)iap_slot_meta_addr(id);

    if (meta->magic != SLOT_META_MAGIC) 
    {
        return 0;
    }
    if (meta->img_len == 0) 
    {
        return 0;
    }
    return crc32_calc((uint8_t *)iap_slot_start_addr(id), meta->img_len);
}
slot_img_state_t iap_slot_validate(boot_slot_id_t id)
{
    if (id == SLOT_ID_NONE)
        return SLOT_IMG_INVALID;

    slot_meta_t *meta = (slot_meta_t *)iap_slot_meta_addr(id);

    if (meta->magic != SLOT_META_MAGIC) 
    {
        return SLOT_IMG_EMPTY;
    }
    if (meta->img_state == SLOT_IMG_EMPTY) 
    {
        return SLOT_IMG_EMPTY;
    }
    if (meta->img_len == 0 || meta->img_len > (iap_slot_end_addr(id)-iap_slot_start_addr(id)) )
    {
        return SLOT_IMG_INVALID;
    }
    uint32_t cal = iap_slot_calc_crc(id);
    if (cal == meta->img_crc32) 
    {
        return SLOT_IMG_VALID;
    }
    else 
    {
        return SLOT_IMG_INVALID;
    }
    
}
static int8_t meta_commit(const slot_meta_t *meta_a, const slot_meta_t *meta_b, boot_slot_id_t active)
{
    uint32_t addr;
    uint32_t word;

    if (FLASH_If_Erase_Sector(FLAG_SECTOR_START_ADDR) != 0)
    {
        return IAP_SLOT_ERR;
    }

    addr = SLOT_A_META_ADDR;
    if (FLASH_If_Write(&addr, (uint32_t *)meta_a, sizeof(slot_meta_t)/4U) != 0)
        return IAP_SLOT_ERR;

    addr = SLOT_B_META_ADDR;
    if (FLASH_If_Write(&addr, (uint32_t *)meta_b, sizeof(slot_meta_t)/4U) != 0)
        return IAP_SLOT_ERR;

    addr = BOOT_SLOT_ADDR;
    word = BOOT_SLOT_ENCODE(active);
    if (FLASH_If_Write(&addr, &word, 1) != 0)
        return IAP_SLOT_ERR;
    return IAP_SLOT_OK;
} 
int8_t iap_slot_mark_new(boot_slot_id_t id, uint32_t img_len, uint32_t img_crc32)
{
    if (id == SLOT_ID_NONE)
      return IAP_SLOT_ERR;

    slot_meta_t meta_a;
    slot_meta_t meta_b;

    meta_a = *(slot_meta_t *)iap_slot_meta_addr(SLOT_ID_A);
    meta_b = *(slot_meta_t *)iap_slot_meta_addr(SLOT_ID_B);
    
    slot_meta_t *target = id == (SLOT_ID_A) ? &meta_a : &meta_b;
    target->magic = SLOT_META_MAGIC;
    target->img_state = SLOT_IMG_NEW;
    target->img_len = img_len;
    target->img_crc32 = img_crc32;
    target->commit_state = COMMIT_PENDING;

    boot_slot_id_t active = iap_slot_get_active();
    return meta_commit(&meta_a, &meta_b, active);
}
int8_t iap_slot_commit(boot_slot_id_t id)
{
     if (id == SLOT_ID_NONE)
       return IAP_SLOT_ERR;

     slot_img_state_t state = iap_slot_validate(id);

     if (state != SLOT_IMG_VALID) 
     {
        slot_meta_t meta_a;
        slot_meta_t meta_b;

        meta_a = *(slot_meta_t *)iap_slot_meta_addr(SLOT_ID_A);
        meta_b = *(slot_meta_t *)iap_slot_meta_addr(SLOT_ID_B);
        
        slot_meta_t *target = (id == SLOT_ID_A) ? &meta_a : &meta_b;
        target->img_state = SLOT_IMG_INVALID;
        target->commit_state = COMMIT_IDLE;
        return meta_commit(&meta_a, &meta_b, iap_slot_get_active());
     
     }
     else 
     {
        slot_meta_t meta_a;
        slot_meta_t meta_b;

        meta_a = *(slot_meta_t *)iap_slot_meta_addr(SLOT_ID_A);
        meta_b = *(slot_meta_t *)iap_slot_meta_addr(SLOT_ID_B);
        
        slot_meta_t *new_slot = (id == SLOT_ID_A) ? &meta_a : &meta_b;
        slot_meta_t *old_slot = (id == SLOT_ID_A) ? &meta_b : &meta_a;

        new_slot->commit_state = COMMIT_DONE;
        new_slot->img_state = SLOT_IMG_VALID;
        old_slot->commit_state = COMMIT_IDLE;
        old_slot->img_state = SLOT_IMG_OLD;
        return meta_commit(&meta_a, &meta_b, id);
     
     }
}
boot_slot_id_t iap_slot_rollback(void)
{
    boot_slot_id_t now = iap_slot_get_active();
    if(iap_slot_validate(now) == SLOT_IMG_VALID)
        return now;
    boot_slot_id_t spare = iap_slot_get_spare();
    if( iap_slot_validate(spare) == SLOT_IMG_VALID)
    {
        slot_meta_t meta_a;
        slot_meta_t meta_b;

        meta_a = *(slot_meta_t *)iap_slot_meta_addr(SLOT_ID_A);
        meta_b = *(slot_meta_t *)iap_slot_meta_addr(SLOT_ID_B);
        meta_commit(&meta_a, &meta_b, spare);
        
        return spare;
    }    
    return SLOT_ID_NONE;
}
void iap_slot_init(void)
{
    boot_slot_id_t active = iap_slot_get_active();
    

    if (active == SLOT_ID_NONE)
    {
        if (iap_slot_validate(SLOT_ID_A) == SLOT_IMG_VALID ) 
        {
            active = SLOT_ID_A;
        }
        else if (iap_slot_validate(SLOT_ID_B) == SLOT_IMG_VALID ) 
        {
            active = SLOT_ID_B;
        }
        else 
        {
            active = SLOT_ID_NONE;
        }
        if (active != SLOT_ID_NONE) 
        {
            slot_meta_t meta_a;
            slot_meta_t meta_b;

            meta_a = *(slot_meta_t *)iap_slot_meta_addr(SLOT_ID_A);
            meta_b = *(slot_meta_t *)iap_slot_meta_addr(SLOT_ID_B);
            meta_commit(&meta_a, &meta_b, active);
        
        }
        return;
    }
    
    boot_slot_id_t other = (active == SLOT_ID_A) ? SLOT_ID_B : SLOT_ID_A;
    if (iap_slot_validate(active) != SLOT_IMG_VALID)
    {
        iap_slot_rollback();
        return;
    }
    slot_meta_t *other_meta = (slot_meta_t *)iap_slot_meta_addr(other);
    if (other_meta->magic == SLOT_META_MAGIC &&
        other_meta->img_state == SLOT_IMG_NEW &&
        other_meta->commit_state == COMMIT_PENDING)
    {
        if ((iap_slot_validate(other) == SLOT_IMG_VALID))
            iap_slot_commit(other);
        else
            iap_slot_rollback();
    }
   /*  boot_slot_id_t spare = iap_slot_get_spare();
    slot_meta_t meta;
    meta = *(slot_meta_t *)iap_slot_meta_addr(spare);
    if (meta.img_state == SLOT_IMG_NEW || meta.commit_state == COMMIT_PENDING)
        iap_slot_validate(spare);
    if (iap_slot_validate(spare) == SLOT_IMG_VALID)
    {
        iap_slot_commit(spare);
    }
    else 
    {
        iap_slot_rollback();
    } */
}