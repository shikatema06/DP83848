#!/usr/bin/env python3
"""
tools/tftp_upgrade.py — TFTP IAP 升级脚本（无需外部 TFTP 客户端）。

用法:
    python tools/tftp_upgrade.py --file <firmware.bin> [--ip 172.16.1.69]

流程:
    1. 读固件 bin，计算 CRC32（与固件 crc32_calc 同款: IEEE 802.3 反射多项式）
    2. 固件尾部附加 4 字节 CRC trailer（小端）→ 上传内容
    3. TFTP WRQ 到 172.16.1.69:69，分块 (512B/块) 发送
    4. Bootloader 收到后写 spare 槽 → 校验 CRC → commit → NVIC_SystemReset

注意:
    - 固件必须链接到 spare 槽地址（当前 active=Slot A → spare=Slot B = 0x08060000）
    - 上传完成后设备会自动复位跳到新固件
"""

import argparse
import binascii
import socket
import struct
import sys
import time

TFTP_WRQ  = 2
TFTP_DATA = 3
TFTP_ACK  = 4
BLOCK_SIZE = 512


def crc32(data: bytes) -> int:
    """与 Bootloader iap_slot.c / tftp_server.c 的 crc32_calc 一致."""
    return binascii.crc32(data) & 0xFFFFFFFF


def make_upload(fw_path: str) -> bytes:
    fw = open(fw_path, 'rb').read()
    crc = crc32(fw)
    upload = fw + struct.pack('<I', crc)   # 4 字节 CRC trailer，小端
    print(f"[*] firmware : {fw_path} ({len(fw)} B, 0x{len(fw):X})")
    print(f"[*] crc32    : 0x{crc:08X}")
    print(f"[*] upload   : {len(upload)} B (fw + 4B CRC trailer)")
    return upload


def tftp_put(server: str, upload: bytes, retries: int = 5) -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(3)

    # ---- 阶段 1: WRQ -> port 69 ----
    wrq = struct.pack('!H', TFTP_WRQ) + b'firmware.bin\x00' + b'octet\x00'
    s.sendto(wrq, (server, 69))

    # 等 ACK 0，同时拿到服务器的临时端口（TID）
    ack, peer = None, None
    for _ in range(retries):
        try:
            pkt, peer = s.recvfrom(516)
            if pkt[:4] == struct.pack('!HH', TFTP_ACK, 0):
                ack = pkt
                break
        except socket.timeout:
            s.sendto(wrq, (server, 69))   # 重发 WRQ
    if ack is None:
        print("[!] WRQ 无响应（Bootloader 不在 IAP / 网络不通?）")
        s.close()
        return -1

    _, peer_port = peer
    print(f"[*] WRQ OK, server TID = {peer_port}")

    # ---- 阶段 2: DATA 块（块号从 1 开始，每块 ≤512B）----
    block = 1
    offset = 0
    sent_total = 0
    n = len(upload)

    while True:
        chunk = upload[offset:offset + BLOCK_SIZE]
        is_last = len(chunk) < BLOCK_SIZE

        # 文件恰好是 512 整数倍时，TFTP 约定最后还要发一个空 DATA 块标记结束
        if offset >= n:
            chunk = b''
            is_last = True

        pkt = struct.pack('!HH', TFTP_DATA, block) + chunk

        # 发块并等对应 ACK（超时重发）
        acked = False
        for _ in range(retries):
            s.sendto(pkt, (server, peer_port))
            try:
                resp, _ = s.recvfrom(516)
                if resp[:2] == struct.pack('!H', TFTP_ACK) and \
                   resp[2:4] == struct.pack('!H', block):
                    acked = True
                    break
            except socket.timeout:
                continue
        if not acked:
            print(f"[!] 块 {block} 未 ACK，传输中止")
            s.close()
            return -2

        sent_total += len(chunk)
        if block % 50 == 0 or is_last:
            print(f"[*] 块 {block} ACK, 已发 {sent_total} B")
        block += 1
        offset += len(chunk)

        if is_last and offset >= n:
            break

    s.close()
    print(f"[+] TFTP 完成: {sent_total} B, {block-1} 块")
    print("[+] 设备即将校验 CRC → commit → 复位 → 跳新固件")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--file', required=True, help='固件 bin（须链接到 spare 槽地址）')
    ap.add_argument('--ip', default='172.16.1.69')
    args = ap.parse_args()

    upload = make_upload(args.file)
    rc = tftp_put(args.ip, upload)
    sys.exit(rc)


if __name__ == '__main__':
    main()
