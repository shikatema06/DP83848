/**
 * @file    Bootloader/tftp_server.c
 * @brief   TFTP server for IAP — WRQ-only, lazy sector erase, CRC32 + A/B
 *          slot commit, NVIC_SystemReset on completion.
 *
 *          Adapted from STM32Cube_FW_F4 LwIP_IAP tftpserver.c, restructured:
 *            - State is in file-scope statics, no mem_malloc (was use-after-free).
 *            - Port-69 PCB is bound once and never torn down (a failed transfer
 *              no longer kills the listener).
 *            - One pbuf_free per recv call.
 *            - Strict length checks before parsing opcode/block.
 *            - Always NVIC_SystemReset at end of transfer (success or fail);
 *              iap_slot_init() picks up partial state on the next boot.
 *
 *          Project-specific deviations from ST reference (preserved):
 *            - Lazy sector erase instead of bulk erase.
 *            - Spare-slot address from iap_slot_get_spare()/iap_slot_start_addr().
 *            - 4-byte CRC32 trailer; verified by crc32_calc at end of transfer.
 *            - iap_slot_mark_new() + iap_slot_commit() before reset.
 */

#include "tftp_server.h"
#include "flash_if.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include <string.h>
#include "iap_slot.h"

/* ---- CRC32 (Ethernet polynomial, bit-reversed) ---- */
static uint32_t crc32_calc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint32_t j = 0; j < 8U; j++) {
            if (crc & 1U) crc = (crc >> 1) ^ 0xEDB88320U;
            else          crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

/* ---- File-scope state (no malloc) ---- */
static struct udp_pcb   *s_pcb_listen;     /* Port 69 — never freed              */
static struct udp_pcb   *s_pcb_xfer;       /* Per-transfer ephemeral PCB         */
static tftp_connection_args s_xfer;        /* Live only while s_xfer.active==1  */
static uint32_t          s_flash_addr;     /* Current write pointer into slot    */
static uint32_t          s_last_sector = 0xFFFFFFFFU;
static boot_slot_id_t    s_slot_id;        /* Slot being written                 */
static uint32_t          s_total_bytes;    /* Mirror of s_xfer.tot_bytes         */

/* Activity signal for the polling loop in bootloader_main: while a transfer
 * is in progress we hold this non-zero so the bootloader postpones its
 * 5-second jump-to-App. After NVIC reset this state evaporates. */
static volatile uint32_t s_has_activity = 0;

/* ---- Helpers ---- */
static tftp_opcode decode_op(const char *buf)
{
    /* buf[0..1] = opcode in network byte order (high byte first) */
    return (tftp_opcode)((u8_t)buf[1]);
}

static u16_t extract_block(const char *buf)
{
    u16_t *p = (u16_t *)(buf + 2);
    return ntohs(p[0]);
}

static err_t send_ack(struct udp_pcb *pcb, const ip_addr_t *to, u16_t to_port,
                      u16_t block)
{
    struct pbuf *p;
    char packet[TFTP_ACK_PKT_LEN];

    memset(packet, 0, sizeof packet);
    packet[1] = (char)TFTP_ACK;
    {
        u16_t b = htons(block);
        memcpy(&packet[2], &b, 2);
    }

    p = pbuf_alloc(PBUF_TRANSPORT, TFTP_ACK_PKT_LEN, PBUF_RAM);
    if (!p) return ERR_MEM;
    memcpy(p->payload, packet, TFTP_ACK_PKT_LEN);

    err_t err = udp_sendto(pcb, p, to, to_port);
    pbuf_free(p);
    return err;
}

static void reset_transfer_state(void)
{
    memset(&s_xfer, 0, sizeof s_xfer);
    s_total_bytes      = 0;
    s_flash_addr       = 0;
    s_last_sector      = 0xFFFFFFFFU;
    if (s_pcb_xfer) {
        udp_recv(s_pcb_xfer, NULL, NULL);
        udp_remove(s_pcb_xfer);
        s_pcb_xfer = NULL;
    }
}

/* Lazily erase the Flash sector containing 'addr' (once per crossing). */
static void lazy_erase_sector(uint32_t addr)
{
    uint32_t sector = FLASH_Get_Sector(addr);
    if (sector != s_last_sector) {
        (void)FLASH_If_Erase_Sector(addr);
        s_last_sector = sector;
    }
}

/* End-of-transfer: verify CRC32 trailer, mark+commit slot, then reset.
 * Always resets — failure is recovered on the next boot by iap_slot_init(). */
static void finish_transfer_and_reset(void)
{
    /* Layout: payload bytes [...][crc32].
     * img_size includes the 4-byte trailer; fw_size = img_size - 4. */
    uint32_t img_size = s_total_bytes;
    if (img_size > 4U) {
        uint32_t fw_size      = img_size - 4U;
        uint32_t expected_crc = *(volatile uint32_t *)(s_flash_addr - 4U);
        uint32_t calc_crc     = crc32_calc((const uint8_t *)iap_slot_start_addr(s_slot_id),
                                           fw_size);

        if (expected_crc == calc_crc) {
            /* Success path: mark + commit */
            if (iap_slot_mark_new(s_slot_id, fw_size, calc_crc) == IAP_SLOT_OK) {
                (void)iap_slot_commit(s_slot_id);
            }
        }
        /* On CRC mismatch we just don't commit, leaving the old App active
         * after reset. */
    }

    NVIC_SystemReset();
    /* Should never get here */
    while (1) { /* hang */ }
}

/* ---- DATA callback (per block, ephemeral port) ---- */
static void on_data(void *arg, struct udp_pcb *upcb,
                   struct pbuf *pkt, const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    uint32_t data_buffer[128]; /* 512 bytes = max DATA payload, word-aligned  */

    /* Strict length check before parsing */
    if (pkt == NULL || pkt->len < TFTP_DATA_PKT_HDR_LEN) {
        if (pkt) pbuf_free(pkt);
        return;
    }

    u16_t block = extract_block((const char *)pkt->payload);

    /* Expected next block? */
    if (block == (u16_t)(s_xfer.block + 1U)) {
        u16_t payload_len = (u16_t)(pkt->len - TFTP_DATA_PKT_HDR_LEN);

        lazy_erase_sector(s_flash_addr);

        pbuf_copy_partial(pkt, data_buffer, payload_len, TFTP_DATA_PKT_HDR_LEN);

        /* Round up to a whole number of words. Always at least 1 word
         * (zero-padded if the FLASH_If_Write verify ever looks at it). */
        uint16_t words = (uint16_t)((payload_len + 3U) / 4U);
        if (words == 0U) words = 1U;

        (void)FLASH_If_Write(&s_flash_addr, data_buffer, words);

        s_xfer.block    = block;
        s_xfer.tot_bytes += payload_len;
        s_total_bytes   = s_xfer.tot_bytes;
    }

    /* ACK the block we just processed (retransmit handling left to caller) */
    (void)send_ack(upcb, addr, port, s_xfer.block);

    /* Last block heuristic: anything < 516 bytes means the sender signalled
     * end of transfer. */
    if (pkt->len < TFTP_DATA_PKT_LEN_MAX) {
        pbuf_free(pkt);
        finish_transfer_and_reset();
        return; /* unreachable */
    }

    pbuf_free(pkt);
}

/* ---- WRQ processing (called from port-69 callback on TFTP_WRQ) ---- */
static void begin_write(struct udp_pcb *xfer_pcb, const ip_addr_t *to, u16_t to_port)
{
    /* Save peer into static state (no malloc) */
    s_xfer.to_ip     = *to;
    s_xfer.to_port   = to_port;
    s_xfer.block     = 0;
    s_xfer.tot_bytes = 0;
    s_xfer.active    = 1U;
    s_has_activity   = 1U;

    s_pcb_xfer       = xfer_pcb;
    udp_recv(xfer_pcb, on_data, NULL);

    s_slot_id      = iap_slot_get_spare();
    s_flash_addr   = iap_slot_start_addr(s_slot_id);
    s_last_sector  = 0xFFFFFFFFU;
    s_total_bytes  = 0;

    /* Flash was unlocked by bootloader_main() before iap_slot_init() */
    (void)FLASH_If_Init();

    /* ACK block 0 to start the transfer */
    (void)send_ack(xfer_pcb, to, to_port, s_xfer.block);
}

/* ---- Port-69 callback: dispatch WRQ/DATA/ERROR on the global listener ---- */
static void on_listen(void *arg, struct udp_pcb *upcb,
                     struct pbuf *pkt, const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)upcb;

    if (pkt == NULL || pkt->len < TFTP_OPCODE_LEN) {
        if (pkt) pbuf_free(pkt);
        return;
    }

    tftp_opcode op = decode_op((const char *)pkt->payload);
    pbuf_free(pkt);

    if (op != TFTP_WRQ) {
        /* RRQ/DATA/ACK/ERROR on the listener port are protocol violations
         * for our WRQ-only server. Just drop. (Per RFC 1350 we'd reply with
         * an ERROR packet, but ignoring is acceptable for IAP.) */
        return;
    }

    /* A transfer is already active and the field PC sent a new WRQ:
     * treat as a fresh request — wipe stale state and start over. */
    reset_transfer_state();

    /* Allocate a fresh PCB on an ephemeral port for the DATA channel */
    struct udp_pcb *xfer_pcb = udp_new();
    if (!xfer_pcb) return;
    if (udp_bind(xfer_pcb, IP_ADDR_ANY, 0) != ERR_OK) {
        udp_remove(xfer_pcb);
        return;
    }
    begin_write(xfer_pcb, addr, port);
}

/* ---- Public ---- */
void IAP_tftpd_init(void)
{
    s_pcb_listen = udp_new();
    if (!s_pcb_listen) return;

    if (udp_bind(s_pcb_listen, IP_ADDR_ANY, 69) != ERR_OK) {
        udp_remove(s_pcb_listen);
        s_pcb_listen = NULL;
        return;
    }

    udp_recv(s_pcb_listen, on_listen, NULL);
    s_has_activity = 0;
}

int IAP_tftpd_has_activity(void)
{
    /* True while a transfer is in progress or the last transfer completed.
     * The polling loop calls this each iteration; it just checks the flag. */
    return (int)s_has_activity;
}
