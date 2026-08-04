"""Patch Bootloader hex file: add boot_valid magic at 0x0800FFFC.

This inserts a NEW hex record (does NOT overwrite existing code) at the
end of sector 3, after all Bootloader code/data ends at ~0x0800BE00.
"""

import sys
import os

BOOT_VALID_ADDR = 0x0800FFFC
BOOT_VALID_MAGIC = 0xAA55AA55


def patch_hex(filepath):
    with open(filepath, 'r') as f:
        lines = [line.strip() for line in f]

    # Build hex record: :04FFFC0055AA55AACC
    offset = BOOT_VALID_ADDR & 0xFFFF          # 0xFFFC
    data = BOOT_VALID_MAGIC.to_bytes(4, 'little')  # 55 AA 55 AA
    rectype = 0x00

    record_body = bytes([0x04, (offset >> 8) & 0xFF, offset & 0xFF,
                         rectype]) + data
    checksum = (-sum(record_body)) & 0xFF
    new_line = (f":04{offset:04X}{rectype:02X}"
                f"{data.hex().upper()}{checksum:02X}")

    # Check if this address is already in the hex file (should not be)
    for line in lines:
        if line.startswith(f":04{offset:04X}"):
            print(f"WARNING: Record at 0x{offset:04X} already exists, "
                  f"will be replaced")
            lines.remove(line)
            break

    # Insert new record before EOF line
    for i, line in enumerate(lines):
        if line.startswith(':00000001FF') or line.startswith(':00000001'):
            # This is the EOF record — insert before it
            lines.insert(i, new_line)
            break
    else:
        # No EOF found — append both
        lines.append(new_line)
        lines.append(':00000001FF')

    with open(filepath, 'w') as f:
        for line in lines:
            f.write(line + '\n')

    print(f"ADDED: boot_valid ({BOOT_VALID_MAGIC:#010X}) at "
          f"0x{BOOT_VALID_ADDR:08X}")
    print(f"OK: {filepath} patched")
    return 0


if __name__ == '__main__':
    hex_file = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.join(os.path.dirname(__file__), '..', 'build', 'Debug',
                     'DP83848_BOOT.hex')
    sys.exit(patch_hex(hex_file))
