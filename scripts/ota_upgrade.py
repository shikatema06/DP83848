"""
OTA firmware upgrade tool for DP83848 IAP Bootloader.

Usage:
    python ota_upgrade.py <firmware.bin> [--ip <target_ip>] [--port <port>]

Workflow:
    1. Connect to App TCP server (default 172.16.1.69:1030)
    2. Send CMD_ENTER_BOOTLOADER (0x0C)
    3. Wait 5 s for device to reset and Bootloader to initialise LwIP
    4. TFTP-upload the firmware to port 69
    5. Device verifies CRC32, writes boot_valid, and resets into new App

Requirements:
    pip install tftpy
"""

import argparse
import socket
import sys
import time


FRAME_SOF  = 0x02
FRAME_EOF1 = 0xDD
FRAME_EOF2 = 0xAA


def send_enter_bootloader(ip: str, port: int, timeout: float = 3.0) -> bool:
    """Send CMD_ENTER_BOOTLOADER (0x0C) binary frame over TCP."""
    frame = bytes([FRAME_SOF, 0x01, 0x0C, 0x00, 0x00, 0x00, FRAME_EOF1, FRAME_EOF2])

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((ip, port))
        sock.sendall(frame)

        # Read ACK
        ack = sock.recv(8)
        if len(ack) == 8 and ack[0] == FRAME_SOF and ack[2] == 0x0C:
            print(f"[OK] Received ACK from App (0x0C)")
            sock.close()
            return True
        else:
            print(f"[WARN] Unexpected response: {ack.hex()}")
            sock.close()
            return False
    except socket.timeout:
        print("[WARN] Timeout waiting for ACK, continuing anyway...")
        return True
    except Exception as e:
        print(f"[WARN] TCP error: {e}, continuing anyway...")
        return True


def tftp_upload(ip: str, firmware_path: str, timeout: int = 10) -> bool:
    """Upload firmware via TFTP."""
    try:
        import tftpy
    except ImportError:
        print("[ERROR] tftpy not installed. Run: pip install tftpy")
        return False

    try:
        client = tftpy.TftpClient(ip, 69, {'timeout': timeout})
        print(f"[INFO] Starting TFTP upload to {ip}:69 ...")
        client.upload(firmware_path, "fw.bin")
        print("[OK] TFTP upload complete.")
        return True
    except Exception as e:
        print(f"[ERROR] TFTP upload failed: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description="DP83848 OTA Firmware Upgrade")
    parser.add_argument("firmware", help="Path to firmware .bin file")
    parser.add_argument("--ip", default="172.16.1.69",
                        help="Target IP address (default: 172.16.1.69)")
    parser.add_argument("--port", type=int, default=1030,
                        help="App TCP port (default: 1030)")
    parser.add_argument("--wait", type=int, default=5,
                        help="Seconds to wait for bootloader init (default: 5)")
    parser.add_argument("--tftp-timeout", type=int, default=10,
                        help="TFTP timeout in seconds (default: 10)")

    args = parser.parse_args()

    print("=" * 55)
    print("DP83848 OTA Firmware Upgrade")
    print("=" * 55)
    print(f"  Target IP  : {args.ip}")
    print(f"  App port   : {args.port}")
    print(f"  Firmware   : {args.firmware}")
    print("=" * 55)

    # Step 1: Tell App to enter bootloader
    print("\n[1/3] Sending CMD_ENTER_BOOTLOADER (0x0C) ...")
    send_enter_bootloader(args.ip, args.port)

    # Step 2: Wait for Bootloader to initialise
    print(f"\n[2/3] Waiting {args.wait} s for Bootloader LwIP init ...")
    time.sleep(args.wait)

    # Step 3: TFTP upload
    print(f"\n[3/3] TFTP uploading firmware ...")
    success = tftp_upload(args.ip, args.firmware, args.tftp_timeout)

    if success:
        print("\n" + "=" * 55)
        print("UPGRADE SUCCESSFUL — device will boot new firmware.")
        print("=" * 55)
    else:
        print("\n[FAIL] Upgrade failed.")
        sys.exit(1)


if __name__ == "__main__":
    main()
