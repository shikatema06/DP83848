#!/usr/bin/env python3
"""Robust TFTP client for STM32F407 IAP Bootloader."""

import socket
import struct
import time
import zlib
import sys
import os

DEVICE_IP = '172.16.1.69'
BLOCK_SIZE = 512
MAX_RETRIES = 3


def main():
    # Prepare firmware
    bin_file = 'build/Debug/DP83848_test.bin'
    os.system(f'arm-none-eabi-objcopy -I ihex -O binary '
              f'build/Debug/DP83848.hex {bin_file}')

    with open(bin_file, 'rb') as f:
        app_bin = f.read()

    crc = zlib.crc32(app_bin) & 0xFFFFFFFF
    fw_data = app_bin + struct.pack('<I', crc)
    total = len(fw_data)
    print(f'Firmware: {total}B (app={len(app_bin)}, crc=0x{crc:08X})')

    # TFTP transfer
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3)
    sock.bind(('', 0))

    # WRQ
    wrq = b'\x00\x02' + b'fw.bin\x00' + b'octet\x00'
    for attempt in range(MAX_RETRIES):
        sock.sendto(wrq, (DEVICE_IP, 69))
        try:
            data, server_addr = sock.recvfrom(100)
            op = struct.unpack('>H', data[:2])[0]
            blk = struct.unpack('>H', data[2:4])[0]
            if op == 4 and blk == 0:
                print(f'ACK0 from {server_addr}')
                break
        except socket.timeout:
            print(f'WRQ retry {attempt+1}...')
    else:
        print('ERROR: No ACK0 after retries')
        return 1

    # Send data blocks with retransmission
    offset = 0
    block_num = 1
    total_blocks = 0
    retransmits = 0
    t_start = time.time()

    while offset < total:
        chunk = fw_data[offset:offset + BLOCK_SIZE]
        pkt = struct.pack('>HH', 3, block_num) + chunk
        sock.sendto(pkt, server_addr)

        try:
            ack, _ = sock.recvfrom(100)
            aop = struct.unpack('>H', ack[:2])[0]
            ab = struct.unpack('>H', ack[2:4])[0]

            if aop == 5:
                err_code = struct.unpack('>H', ack[2:4])[0]
                print(f'TFTP ERROR block {block_num}: code={err_code}')
                return 1

            if aop == 4:
                if ab == block_num:
                    # ACK matches — advance to next block
                    offset += len(chunk)
                    block_num += 1
                    total_blocks += 1
                    retransmits = 0
                elif ab < block_num:
                    # Duplicate ACK — server wants us to re-send ab+1
                    print(f'  dup ACK blk={ab} (we sent {block_num}), '
                          f're-sending {ab+1}')
                    # Re-send from ab+1
                    offset = ab * BLOCK_SIZE
                    block_num = ab + 1
                else:
                    print(f'  future ACK blk={ab} > {block_num}?')
        except socket.timeout:
            print(f'  timeout blk {block_num}, retransmitting...')
            retransmits += 1

        # Progress
        if total_blocks % 30 == 0 and total_blocks > 0:
            elapsed = time.time() - t_start
            kbps = (offset / 1024) / elapsed if elapsed > 0 else 0
            print(f'  {offset}/{total}B ({total_blocks} blocks) '
                  f'{kbps:.0f} KB/s')

    elapsed = time.time() - t_start
    print(f'\nTransfer done: {total}B in {elapsed:.1f}s '
          f'({total/1024/elapsed:.1f} KB/s)')

    # Send zero-length final block if last block was full size
    if len(chunk) == BLOCK_SIZE:
        pkt = struct.pack('>HH', 3, block_num)
        sock.sendto(pkt, server_addr)
        try:
            ack, _ = sock.recvfrom(100)
            print(f'Final zero-block {block_num} ACKed')
        except socket.timeout:
            pass  # Server might reset before ACKing final block

    sock.close()

    # Wait for device to validate CRC, write boot_valid, and reboot
    print('Waiting for device to reboot into App (8s)...')
    time.sleep(8)

    # Verify App
    s = socket.socket()
    s.settimeout(5)
    try:
        s.connect((DEVICE_IP, 1030))
        print('\n*** IAP UPGRADE SUCCESS - App running on TCP 1030 ***')
        s.close()
        return 0
    except Exception as e:
        print(f'\nFAIL: App not running ({e})')
        return 1


if __name__ == '__main__':
    sys.exit(main())
