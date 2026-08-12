/**
 * @file    Bootloader/tftp_server.h
 * @brief   TFTP server header — WRQ-only IAP, IP CRC32 A/B slot commit.
 *
 *          Rewritten to follow STM32Cube_FW_F4 LwIP_IAP tftpserver structure
 *          (state stays in file-scope statics; port-69 PCB is never torn down),
 *          but with this project's per-slot commit & CRC32 trailer preserved.
 */

#ifndef __TFTPSERVER_BOOT_H
#define __TFTPSERVER_BOOT_H

#include "lwip/udp.h"
#include <stdint.h>

/* TFTP packet geometry (RFC 1350) */
#define TFTP_OPCODE_LEN         2
#define TFTP_BLKNUM_LEN         2
#define TFTP_DATA_LEN_MAX       512
#define TFTP_DATA_PKT_HDR_LEN   (TFTP_OPCODE_LEN + TFTP_BLKNUM_LEN)
#define TFTP_ACK_PKT_LEN        (TFTP_OPCODE_LEN + TFTP_BLKNUM_LEN)
#define TFTP_DATA_PKT_LEN_MAX   (TFTP_DATA_PKT_HDR_LEN + TFTP_DATA_LEN_MAX) /* 516 */

/* TFTP opcodes */
typedef enum {
    TFTP_RRQ   = 1,
    TFTP_WRQ   = 2,
    TFTP_DATA  = 3,
    TFTP_ACK   = 4,
    TFTP_ERROR = 5
} tftp_opcode;

/* TFTP error codes (sent in ERROR packet) */
typedef enum {
    TFTP_ERR_NOTDEFINED,
    TFTP_ERR_FILE_NOT_FOUND,
    TFTP_ERR_ACCESS_VIOLATION,
    TFTP_ERR_DISKFULL,
    TFTP_ERR_ILLEGALOP,
    TFTP_ERR_UKNOWN_TRANSFER_ID,
    TFTP_ERR_FILE_ALREADY_EXISTS,
    TFTP_ERR_NO_SUCH_USER,
} tftp_errorcode;

/* One TFTP write transfer's runtime state. Kept small so it fits in BSS
 * instead of being malloc'd (malloc was the source of a use-after-free). */
typedef struct {
    ip_addr_t   to_ip;       /* Peer address at WRQ time                */
    u16_t       to_port;     /* Peer TID                                 */
    u16_t       block;       /* Last ACKed block (next expected = +1)    */
    u32_t       tot_bytes;   /* Bytes of payload received into Flash     */
    u8_t        active;      /* Set while a transfer is in progress      */
} tftp_connection_args;

void IAP_tftpd_init(void);
int  IAP_tftpd_has_activity(void);   /* non-zero while a transfer is active or
                                       just-completed; resets after a small
                                       in-activity window so the bootloader
                                       knows when to jump to App              */

#endif /* __TFTPSERVER_BOOT_H */
