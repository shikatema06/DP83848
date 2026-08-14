"""Patch Bootloader hex file: embed Slot A meta + boot_valid into sector 3.

This inserts a hex record at the end of sector 3 (0x0800FFD0-0x0800FFFF),
after all Bootloader code/data ends at ~0x0800BE00.

Slot layout:
  0x0800FFD0 - 0x0800FFDF : Slot A meta (16 bytes)
  0x0800FFE0 - 0x0800FFEF : Slot B meta (16 bytes, empty)
  0x0800FFF0 - 0x0800FFFB : Reserved (12 bytes)
  0x0800FFFC - 0x0800FFFF : Boot slot word (active = A)
"""

import sys
import os
import zlib
import subprocess

SLOT_A_META_ADDR = 0x0800FFD0
SLOT_META_MAGIC = 0x49415053
BOOT_SLOT_ADDR = 0x0800FFFC
BOOT_SLOT_MAGIC = 0x55AA0000


def build_hex_record(addr, data):
    """Build a single Intel HEX data record."""
    offset = addr & 0xFFFF
    rectype = 0x00
    record_body = bytes([len(data), (offset >> 8) & 0xFF, offset & 0xFF, rectype]) + data
    checksum = (-sum(record_body)) & 0xFF
    return f":{len(data):02X}{offset:04X}{rectype:02X}{data.hex().upper()}{checksum:02X}"


def patch_hex(filepath):
    hex_dir = os.path.dirname(os.path.abspath(filepath))
    # App ELF is always in the parent build directory (e.g. build/Debug/),
    # while the Bootloader hex may live in build/Debug/Bootloader/.
    build_dir = os.path.dirname(hex_dir) if os.path.basename(hex_dir).lower() == 'bootloader' else hex_dir
    elf_path = os.path.join(build_dir, 'DP83848.elf')
    bin_path = os.path.join(build_dir, 'DP83848_meta.bin')

    if not os.path.exists(elf_path):
        print(f"[FAIL] App ELF not found: {elf_path}")
        return 1

    # Generate binary from App ELF and compute CRC32 over the image bytes.
    subprocess.run(
        ['arm-none-eabi-objcopy', '-O', 'binary', elf_path, bin_path],
        check=True
    )
    with open(bin_path, 'rb') as f:
        data = f.read()
    img_len = len(data)
    img_crc32 = zlib.crc32(data) & 0xFFFFFFFF

    # Slot A meta (16 bytes):
    #   magic(4) + img_len(4) + img_crc32(4) + img_state(1)+commit_state(1)+reserved(2)
    slot_a_meta = (
        SLOT_META_MAGIC.to_bytes(4, 'little') +
        img_len.to_bytes(4, 'little') +
        img_crc32.to_bytes(4, 'little') +
        bytes([0x01, 0x00, 0x00, 0x00])   # VALID, IDLE, reserved
    )

    # Slot B meta: empty (16 bytes of 0)
    slot_b_meta = bytes(16)

    # Reserved gap 0x0800FFF0 - 0x0800FFFB: leave erased (0xFF)
    reserved = bytes([0xFF] * 12)

    # Boot slot word: active A
    boot_slot = (BOOT_SLOT_MAGIC | 0).to_bytes(4, 'little')

    payload = slot_a_meta + slot_b_meta + reserved + boot_slot
    new_line = build_hex_record(SLOT_A_META_ADDR, payload)

    with open(filepath, 'r') as f:
        lines = [line.strip() for line in f]

    # Remove any existing records overlapping the patched region (0xFFD0-0xFFFF).
    filtered_lines = []
    for line in lines:
        if line.startswith(':'):
            try:
                byte_count = int(line[1:3], 16)
                rec_offset = int(line[3:7], 16)
                rec_end = rec_offset + byte_count
                patch_start = SLOT_A_META_ADDR & 0xFFFF
                patch_end = (BOOT_SLOT_ADDR & 0xFFFF) + 4
                if rec_end > patch_start and rec_offset < patch_end:
                    continue
            except ValueError:
                pass
        filtered_lines.append(line)
    lines = filtered_lines

    # Insert the new record before the EOF line.
    for i, line in enumerate(lines):
        if line.startswith(':00000001'):
            lines.insert(i, new_line)
            break
    else:
        lines.append(new_line)
        lines.append(':00000001FF')

    with open(filepath, 'w') as f:
        for line in lines:
            f.write(line + '\n')

    print(f"ADDED: Slot A meta at 0x{SLOT_A_META_ADDR:08X}")
    print(f"       img_len={img_len} (0x{img_len:08X}), img_crc32=0x{img_crc32:08X}")
    print(f"ADDED: boot_slot=0x{BOOT_SLOT_MAGIC | 0:08X} (active=A) at 0x{BOOT_SLOT_ADDR:08X}")
    print(f"OK: {filepath} patched")
    return 0


if __name__ == '__main__':
    hex_file = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.join(os.path.dirname(__file__), '..', 'build', 'Debug',
                     'DP83848_BOOT.hex')
    sys.exit(patch_hex(hex_file))
