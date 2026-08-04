#!/usr/bin/env python3
"""
Connect to the board, list IPs from firmware, select one, reconnect on new IP.
Boot strategy B: after power cycle the board uses the last selected IP.
"""

import argparse
import socket
import struct
import sys

PORT = 1030
FRAME_SOF = 0x02
FRAME_EOF = bytes((0xDD, 0xAA))

# Factory default (index 0) — use when Flash is empty or after reset to defaults
# 列表第 0 项：上电后 30s 内固定使用该地址
DEFAULT_CONNECT_IP = "172.16.1.69"

FACTORY_IPS = [
    "172.16.1.69",
    "172.16.1.169",
    "172.16.1.70",
    "172.16.1.170",
    "172.16.69.1",
    "172.16.169.1",
    "172.16.70.1",
    "172.16.170.1",
]

CMD_LIST_IP = 0x08
CMD_SELECT_IP = 0x0A
CMD_RESTORE_DEFAULT = 0x0B


def cmd_frame(cmd: int, byte3: int = 0) -> bytes:
    return bytes((FRAME_SOF, 0x01, cmd, byte3, 0x00, 0x00, 0xDD, 0xAA))


def parse_list_response(data: bytes) -> tuple[int, int, list[tuple[str, bool]]]:
    if len(data) < 7 or data[0] != FRAME_SOF or data[2] != CMD_LIST_IP:
        raise ValueError(f"unexpected LIST response: {data.hex()}")
    count = data[3]
    active = data[4]
    slots = []
    pos = 5
    for _ in range(8):
        ip = ".".join(str(b) for b in data[pos : pos + 4])
        valid = data[pos + 4] != 0
        slots.append((ip, valid))
        pos += 5
    if data[pos : pos + 2] != FRAME_EOF:
        raise ValueError("LIST response missing EOF")
    return count, active, slots[:count]


def list_ips(host: str, timeout: float = 3.0) -> tuple[int, int, list[tuple[str, bool]]]:
    with socket.create_connection((host, PORT), timeout=timeout) as s:
        s.sendall(cmd_frame(CMD_LIST_IP))
        data = s.recv(256)
    return parse_list_response(data)


def select_ip(host: str, index: int, timeout: float = 3.0) -> None:
    with socket.create_connection((host, PORT), timeout=timeout) as s:
        s.sendall(cmd_frame(CMD_SELECT_IP, index))
        ack = s.recv(8)
    if len(ack) != 8 or ack[2] != CMD_SELECT_IP or ack[3] != index:
        raise ValueError(f"SELECT failed, ack={ack.hex()}")


def find_reachable_ip(timeout: float = 0.8) -> str | None:
    for ip in FACTORY_IPS:
        try:
            with socket.create_connection((ip, PORT), timeout=timeout):
                return ip
        except OSError:
            continue
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="STM32 IP list select tool")
    parser.add_argument(
        "--scan",
        action="store_true",
        help="scan all factory IPs when --host is unreachable",
    )
    parser.add_argument(
        "--host",
        default=DEFAULT_CONNECT_IP,
        help=f"IP to connect for LIST/SELECT (default {DEFAULT_CONNECT_IP})",
    )
    parser.add_argument(
        "--index",
        type=int,
        default=None,
        help="slot index to select (0..7); interactive if omitted",
    )
    args = parser.parse_args()

    host = args.host
    print(f"Connecting to {host}:{PORT} ...")
    try:
        count, active, slots = list_ips(host)
    except OSError:
        if not args.scan:
            print(
                f"Cannot reach {host}. Try --scan or ping factory IPs.\n"
                f"Note: default is {DEFAULT_CONNECT_IP}, not 172.16.1.1.",
                file=sys.stderr,
            )
            return 1
        host = find_reachable_ip()
        if host is None:
            print("No device found on factory IP list.", file=sys.stderr)
            return 1
        print(f"Found device at {host}")
        count, active, slots = list_ips(host)
    print(f"IP list (count={count}, active={active}):")
    for i, (ip, valid) in enumerate(slots):
        mark = " *" if i == active else ""
        print(f"  [{i}] {ip}  valid={valid}{mark}")

    if args.index is None:
        try:
            idx = int(input("Select index: ").strip())
        except (ValueError, EOFError):
            print("Cancelled.", file=sys.stderr)
            return 1
    else:
        idx = args.index

    if idx < 0 or idx >= count:
        print(f"Invalid index {idx}", file=sys.stderr)
        return 1

    target_ip = slots[idx][0]
    if idx == active:
        print(f"Already active: {target_ip}")
        return 0

    print(f"SELECT index {idx} -> {target_ip}")
    select_ip(args.host, idx)
    print("ACK OK. Reconnecting on new IP ...")

    count2, active2, slots2 = list_ips(target_ip)
    print(f"Verified on {target_ip}: active={active2} -> {slots2[active2][0]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
