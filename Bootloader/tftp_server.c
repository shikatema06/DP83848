/**
 * @file    Bootloader/tftp_server.c
 * @brief   TFTP server for IAP — WRQ-only, lazy sector erase, CRC32 + boot_valid.
 *
 *          Adapted from STM32Cube_FW_F4 LwIP_IAP tftpserver.c.
 *          Changes:
 *            - Removed LCD_LOG dependency.
 *            - Bulk erase replaced with per-sector lazy erase on first write.
 *            - CRC32 verification and boot_valid flag on transfer completion.
 */

#include "tftp_server.h"
#include "flash_if.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include <string.h>

/* ---- CRC32 (Ethernet polynomial) ---- */
static uint32_t crc32_calc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t i, j;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8U; j++) {
            if (crc & 1U)
                crc = (crc >> 1) ^ 0xEDB88320U;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

/* ---- Private state ---- */
static uint32_t          Flash_Write_Address;
static uint32_t          Last_Erased_Sector = 0xFFFFFFFFU;
static struct udp_pcb   *UDPpcb;
static __IO uint32_t     total_count;

/* ---- Helpers ---- */
static tftp_opcode IAP_tftp_decode_op(char *buf)
{
    return (tftp_opcode)(buf[1]);
}

static u16_t IAP_tftp_extract_block(char *buf)
{
    u16_t *b = (u16_t *)buf;
    return ntohs(b[1]);
}

static void IAP_tftp_set_opcode(char *buffer, tftp_opcode opcode)
{
    buffer[0] = 0;
    buffer[1] = (u8_t)opcode;
}

static void IAP_tftp_set_block(char *packet, u16_t block)
{
    u16_t *p = (u16_t *)packet;
    p[1] = htons(block);
}

static err_t IAP_tftp_send_ack_packet(struct udp_pcb *upcb, const ip_addr_t *to,
                                       int to_port, int block)
{
    err_t        err;
    struct pbuf *pkt_buf;
    char         packet[TFTP_ACK_PKT_LEN];

    memset(packet, 0, TFTP_ACK_PKT_LEN);
    IAP_tftp_set_opcode(packet, TFTP_ACK);
    IAP_tftp_set_block(packet, block);

    pkt_buf = pbuf_alloc(PBUF_TRANSPORT, TFTP_ACK_PKT_LEN, PBUF_POOL);
    if (!pkt_buf)
        return ERR_MEM;

    memcpy(pkt_buf->payload, packet, TFTP_ACK_PKT_LEN);
    err = udp_sendto(upcb, pkt_buf, to, to_port);
    pbuf_free(pkt_buf);
    return err;
}

static void IAP_tftp_cleanup_wr(struct udp_pcb *upcb, tftp_connection_args *args)
{
    mem_free(args);
    udp_disconnect(upcb);
    udp_remove(upcb);
    udp_recv(UDPpcb, NULL, NULL);  /* restore port-69 callback */
}

/* ---- DATA callback (per-block, port != 69) ---- */
static void IAP_wrq_recv_callback(void *_args, struct udp_pcb *upcb,
                                   struct pbuf *pkt_buf,
                                   const ip_addr_t *addr, u16_t port)
{
    tftp_connection_args *args = (tftp_connection_args *)_args;
    uint32_t data_buffer[128];   /* 512 bytes */
    uint16_t count;

    if (pkt_buf->len != pkt_buf->tot_len)
        return;

    /* Expected next block ? */
    if ((pkt_buf->len > TFTP_DATA_PKT_HDR_LEN) &&
        (IAP_tftp_extract_block(pkt_buf->payload) == (args->block + 1)))
    {
        /* ---- Lazy sector erase (once per sector) ---- */
        {
            uint32_t sector = FLASH_Get_Sector(Flash_Write_Address);
            if (sector != Last_Erased_Sector) {
                FLASH_If_Erase_Sector(Flash_Write_Address);
                Last_Erased_Sector = sector;
            }
        }

        pbuf_copy_partial(pkt_buf, data_buffer,
                          pkt_buf->len - TFTP_DATA_PKT_HDR_LEN,
                          TFTP_DATA_PKT_HDR_LEN);

        total_count += pkt_buf->len - TFTP_DATA_PKT_HDR_LEN;

        count = (pkt_buf->len - TFTP_DATA_PKT_HDR_LEN) / 4;
        if (((pkt_buf->len - TFTP_DATA_PKT_HDR_LEN) % 4) != 0)
            count++;

        FLASH_If_Write(&Flash_Write_Address, data_buffer, count);

        args->block++;
        args->tot_bytes += (pkt_buf->len - TFTP_DATA_PKT_HDR_LEN);
    }
    else if (IAP_tftp_extract_block(pkt_buf->payload) == (args->block + 1))
    {
        /* Zero-length DATA block — exact multiple of 512 */
        args->block++;
    }

    IAP_tftp_send_ack_packet(upcb, addr, port, args->block);

    /* Last block ? Calculate CRC32 on whole image, write boot_valid, reset */
    if (pkt_buf->len < TFTP_DATA_PKT_LEN_MAX)
    {
        IAP_tftp_cleanup_wr(upcb, args);
        pbuf_free(pkt_buf);

        /* CRC32 over the whole App image in Flash */
        {
            uint32_t img_size = (uint32_t)args->tot_bytes;
            uint32_t calc_crc;

            /* Last 4 bytes in the received image are the expected CRC32 */
            if (img_size > 4U) {
                uint32_t expected_crc;
                uint32_t fw_size = img_size - 4U;

                /* Read expected CRC from end of received data in Flash */
                expected_crc = *(volatile uint32_t *)(Flash_Write_Address - 4U);

                /* Compute CRC32 over firmware portion (excluding CRC itself) */
                calc_crc = crc32_calc((const uint8_t *)USER_FLASH_FIRST_PAGE_ADDRESS, fw_size);

                if (calc_crc == expected_crc && fw_size > 0) {
                    /* Write boot_valid flag */
                    FLASH_If_Init();
                    FLASH_If_Erase_Sector(BOOT_VALID_ADDR);
                    {
                        uint32_t magic = BOOT_VALID_MAGIC;
                        uint32_t addr  = BOOT_VALID_ADDR;
                        FLASH_If_Write((__IO uint32_t *)&addr, &magic, 1);
                    }
                    HAL_FLASH_Lock();

                    /* Jump directly to the new App — no reset needed */
                    {
                        uint32_t app_sp = *(volatile uint32_t *)USER_FLASH_FIRST_PAGE_ADDRESS;
                        uint32_t app_pc = *(volatile uint32_t *)(USER_FLASH_FIRST_PAGE_ADDRESS + 4U);
                        if (app_sp >= 0x20000000U && app_sp <= 0x20020000U) {
                            SCB->VTOR = USER_FLASH_FIRST_PAGE_ADDRESS;
                            __set_MSP(app_sp);
                            ((void (*)(void))app_pc)();
                        }
                    }
                }
            }
        }

        /* Upgrade failed (CRC mismatch or zero-length) — reset and retry */
        NVIC_SystemReset();
        while (1) {}
    }
    else
    {
        pbuf_free(pkt_buf);
    }
}

/* ---- WRQ processing ---- */
static int IAP_tftp_process_write(struct udp_pcb *upcb, const ip_addr_t *to, int to_port)
{
    tftp_connection_args *args;

    args = mem_malloc(sizeof *args);
    if (!args) {
        IAP_tftp_cleanup_wr(upcb, args);
        return 0;
    }

    args->op        = TFTP_WRQ;
    args->to_ip.addr = to->addr;
    args->to_port   = to_port;
    args->block     = 0;
    args->tot_bytes = 0;

    udp_recv(upcb, IAP_wrq_recv_callback, args);

    total_count            = 0;
    Flash_Write_Address    = USER_FLASH_FIRST_PAGE_ADDRESS;
    Last_Erased_Sector      = 0xFFFFFFFFU;

    FLASH_If_Init();

    /* ACK block 0 — starts the transfer. No bulk erase here. */
    IAP_tftp_send_ack_packet(upcb, to, to_port, args->block);
    return 0;
}

/* ---- Port-69 callback ---- */
static void IAP_tftp_recv_callback(void *arg, struct udp_pcb *upcb,
                                    struct pbuf *pkt_buf,
                                    const ip_addr_t *addr, u16_t port)
{
    tftp_opcode      op;
    struct udp_pcb  *upcb_data;
    err_t            err;

    upcb_data = udp_new();
    if (!upcb_data)
        return;

    /* TFTP requires data on a new ephemeral port */
    err = udp_bind(upcb_data, IP_ADDR_ANY, 0);
    if (err != ERR_OK) {
        udp_remove(upcb_data);
        pbuf_free(pkt_buf);
        return;
    }

    op = IAP_tftp_decode_op(pkt_buf->payload);
    if (op == TFTP_WRQ) {
        IAP_tftp_process_write(upcb_data, addr, port);
    } else {
        udp_remove(upcb_data);
    }
    pbuf_free(pkt_buf);
}

/* ---- Public ---- */
void IAP_tftpd_init(void)
{
    err_t err;

    UDPpcb = udp_new();
    if (!UDPpcb)
        return;

    err = udp_bind(UDPpcb, IP_ADDR_ANY, 69);
    if (err == ERR_OK) {
        udp_recv(UDPpcb, IAP_tftp_recv_callback, NULL);
    }
}

int IAP_tftpd_has_activity(void)
{
    return (int)total_count;
}
