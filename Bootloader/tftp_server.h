/**
 * @file    Bootloader/tftp_server.h
 * @brief   TFTP server header — unmodified from STM32Cube_FW_F4 LwIP_IAP.
 */

#ifndef __TFTPSERVER_BOOT_H
#define __TFTPSERVER_BOOT_H

#include "lwip/mem.h"
#include "lwip/udp.h"

#define TFTP_OPCODE_LEN         2
#define TFTP_BLKNUM_LEN         2
#define TFTP_DATA_LEN_MAX       512
#define TFTP_DATA_PKT_HDR_LEN   (TFTP_OPCODE_LEN + TFTP_BLKNUM_LEN)
#define TFTP_ERR_PKT_HDR_LEN    (TFTP_OPCODE_LEN + TFTP_BLKNUM_LEN)
#define TFTP_ACK_PKT_LEN        (TFTP_OPCODE_LEN + TFTP_BLKNUM_LEN)
#define TFTP_DATA_PKT_LEN_MAX   (TFTP_DATA_PKT_HDR_LEN + TFTP_DATA_LEN_MAX)
#define TFTP_MAX_RETRIES        3
#define TFTP_TIMEOUT_INTERVAL   5

#define TFTP_ERRCODE_LEN        2

typedef struct
{
    int         op;
    char        data[TFTP_DATA_PKT_LEN_MAX];
    int         data_len;
    ip_addr_t   to_ip;
    int         to_port;
    int         block;
    int         tot_bytes;
    unsigned long long last_time;
} tftp_connection_args;

typedef enum {
    TFTP_RRQ   = 1,
    TFTP_WRQ   = 2,
    TFTP_DATA  = 3,
    TFTP_ACK   = 4,
    TFTP_ERROR = 5
} tftp_opcode;

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

void IAP_tftpd_init(void);
int  IAP_tftpd_has_activity(void);   /* returns non-zero if any DATA block received */

#endif /* __TFTPSERVER_BOOT_H */
