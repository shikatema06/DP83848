#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
onekey_upgrade.py — DP83848 一键 OTA 升级工具（TFTP）

用法:
    python onekey_upgrade.py --firmware app_b.bin --slot B
    python onekey_upgrade.py --firmware app_a.bin --slot A --ip 172.16.1.69
    python onekey_upgrade.py --firmware app_b.bin --slot auto    # 需固件支持 Active slot 查询

流程:
    1. 解析固件链接槽（从向量表 Reset_Handler 地址判断）→ Slot A / Slot B
    2. 确定目标 spare 槽:
         --slot A/B : 显式指定（产线推荐，不依赖 App 固件）
         --slot auto: 连 App TCP 1030 发 0x07，若固件返回 "Active slot" 则自动判定
    3. 校验 固件链接槽 == 目标 spare 槽，不匹配立即报错（防止刷错槽变砖）
    4. 发 ENTER_BOOTLOADER(0x0C) → 等待复位进 Bootloader
    5. TFTP 上传（自动附加 4 字节 CRC32 尾部，与板内 crc32_calc 一致）
    6. 设备校验 CRC → iap_slot_commit → 复位跳新固件

注意:
    - 固件必须链接到 spare 槽地址（Slot A 版 ~0x0801xxxx，Slot B 版 ~0x0806xxxx）。
    - 若板子已在 IAP 模式（TCP 1030 不可达），工具跳过查询/0x0C 直接上传，
      此时必须用 --slot A 或 --slot B 显式指定目标 spare 槽。
    - 纯 Python 标准库实现，可用 PyInstaller 打包为单 exe（产线免装 Python）。

打包（产线用，免装 Python）:
    pip install pyinstaller
    pyinstaller --onefile --name DP83848_Upgrade onekey_upgrade.py
"""

import argparse
import binascii
import re
import socket
import struct
import sys
import time

# ---------------- 常量 ----------------
PORT_APP = 1030
FRAME_SOF = 0x02
FRAME_EOF = (0xDD, 0xAA)

CMD_READ_VERSION = 0x07
CMD_ENTER_BOOTLOADER = 0x0C

# 数据采集（固件 tcp_echoserver.c 定义）
CMD_START_STREAM = 0x05     # 开始采集: 02 01 05 00 00 00 DD AA → ACK 后持续推帧
CMD_STOP_STREAM = 0x06      # 停止采集: 02 01 06 00 00 00 DD AA

# 数据流帧格式（498 字节）
STREAM_FRAME_LEN = 498
STREAM_HDR_LEN = 7
STREAM_DATA_LEN = 480       # 3 通道 × 80 点 × 2B
STREAM_CHAN_BLK = 160       # 每通道字节数
STREAM_POINTS = 80
STREAM_HEADER_MAGIC = b"\x02\x01\x25\x31\x32\x33\x34"
STREAM_TAIL_MAGIC = b"\xAD\xDA\xDD\xAA"

# IP 列表 / 切换（固件 tcp_echoserver.c 定义）
CMD_LIST_IP = 0x09          # 查 IP 列表（注意：固件响应帧 cmd 字节写死 0x08）
CMD_SELECT_IP = 0x0A        # 切换 IP，帧: 02 01 0A <idx> 00 00 DD AA
CMD_RESTORE_DEFAULT = 0x0B  # 恢复 idx0 = 172.16.1.69

# 与固件 net_config.c s_factory_ips 一致的 8 组出厂 IP
FACTORY_IPS = [
    "172.16.1.69", "172.16.1.169", "172.16.1.70", "172.16.1.170",
    "172.16.69.1", "172.16.169.1", "172.16.70.1", "172.16.170.1",
]

TFTP_WRQ = 2
TFTP_DATA = 3
TFTP_ACK = 4
BLOCK_SIZE = 512

# 槽地址范围（兼容旧布局 Slot A 起点 0x08010000 与新布局 0x08020000）
SLOT_A_RANGE = (0x08010000, 0x08060000)
SLOT_B_RANGE = (0x08060000, 0x080C0000)


# ---------------- 固件解析 ----------------
def detect_slot(fw_path: str):
    """从向量表 Reset_Handler 判断固件链接槽。
    返回 (slot 'A'|'B'|None, reset_addr, img_bytes)"""
    with open(fw_path, 'rb') as f:
        img = f.read()
    if len(img) < 8:
        return None, 0, img
    _sp, reset = struct.unpack('<II', img[:8])
    if SLOT_A_RANGE[0] <= reset < SLOT_A_RANGE[1]:
        return 'A', reset, img
    if SLOT_B_RANGE[0] <= reset < SLOT_B_RANGE[1]:
        return 'B', reset, img
    return None, reset, img


def make_upload(fw: bytes) -> bytes:
    """固件 + 4 字节 CRC32 尾部（小端），与板内 crc32_calc 一致。"""
    crc = binascii.crc32(fw) & 0xFFFFFFFF
    return fw + struct.pack('<I', crc)


# ---------------- TCP 1030 交互 ----------------
def tcp_query(ip: str, cmd: int, timeout: float = 3.0):
    """发送二进制帧，返回响应 bytes；失败返回 None。"""
    frame = bytes([FRAME_SOF, 0x01, cmd, 0x00, 0x00, 0x00,
                   FRAME_EOF[0], FRAME_EOF[1]])
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect((ip, PORT_APP))
        s.sendall(frame)
        s.settimeout(2.0)
        chunks = []
        while True:
            try:
                d = s.recv(256)
                if not d:
                    break
                chunks.append(d)
            except socket.timeout:
                break
        s.close()
        return b''.join(chunks)
    except Exception:
        return None


def read_active_slot(ip: str):
    """0x07 查询。返回 (active_slot 'A'|'B'|None, 响应文本)。"""
    resp = tcp_query(ip, CMD_READ_VERSION)
    if not resp:
        return None, None
    text = resp.decode('utf-8', 'ignore')
    m = re.search(r'Active slot:\s*([AB])', text, re.I)
    return (m.group(1).upper() if m else None), text


def send_enter_bootloader(ip: str) -> bool:
    return tcp_query(ip, CMD_ENTER_BOOTLOADER) is not None


# ---------------- IP 列表 / 切换 ----------------
def parse_list_response(data: bytes):
    """解析 0x09 列表响应。
    固件响应帧: [0]=SOF [1]=0x01 [2]=0x08(固件写死) [3]=count [4]=active_idx
                [5..]=8 × (ip4 + valid) ... 0xDD 0xAA
    返回 (count, active_idx, [(ip_str, valid), ...])"""
    if len(data) < 7 or data[0] != FRAME_SOF or data[2] not in (0x08, 0x09):
        raise ValueError(f"unexpected LIST response: {data.hex()}")
    count = data[3]
    active = data[4]
    slots = []
    pos = 5
    for _ in range(8):
        ip = ".".join(str(b) for b in data[pos:pos + 4])
        valid = data[pos + 4] != 0
        slots.append((ip, valid))
        pos += 5
    if data[pos:pos + 2] != bytes(FRAME_EOF):
        raise ValueError("LIST response missing EOF")
    return count, active, slots[:count]


def list_ips(host: str, timeout: float = 3.0):
    """查询设备 IP 列表。返回 (count, active_idx, [(ip, valid), ...])"""
    resp = tcp_query(host, CMD_LIST_IP, timeout)
    if not resp:
        raise OSError(f"Cannot reach {host}:{PORT_APP} or no response")
    return parse_list_response(resp)


def select_ip(host: str, index: int, timeout: float = 3.0):
    """0x0A 切换 IP。

    固件行为：回 8B ACK → 立即切换 IP → RST 旧连接。ACK 与 RST 几乎同时到达，
    存在竞态，因此这里只要"命令发出、连接建立成功"即不抛错（ACK 收不齐、
    收到 RST 都属正常）。**最终是否成功以调用方用目标 IP 重连验证为准。**"""
    frame = bytes([FRAME_SOF, 0x01, CMD_SELECT_IP, index, 0x00, 0x00,
                   FRAME_EOF[0], FRAME_EOF[1]])
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect((host, PORT_APP))          # 设备不可达才会抛异常
    try:
        s.sendall(frame)
        ack = b""
        while len(ack) < 8:
            try:
                chunk = s.recv(8 - len(ack))
                if not chunk:
                    break
                ack += chunk
            except socket.timeout:
                break
            except ConnectionResetError:
                break                    # 固件切 IP 后 RST，属正常
    finally:
        s.close()


def restore_default_ip(host: str, timeout: float = 3.0):
    """0x0B 恢复 idx0 (172.16.1.69)。RST/ACK 竞态处理同 select_ip。"""
    frame = bytes([FRAME_SOF, 0x01, CMD_RESTORE_DEFAULT, 0x00, 0x00, 0x00,
                   FRAME_EOF[0], FRAME_EOF[1]])
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect((host, PORT_APP))
    try:
        s.sendall(frame)
        ack = b""
        while len(ack) < 8:
            try:
                chunk = s.recv(8 - len(ack))
                if not chunk:
                    break
                ack += chunk
            except socket.timeout:
                break
            except ConnectionResetError:
                break
    finally:
        s.close()


# ---------------- 数据采集 ----------------
def start_stream(host: str, timeout: float = 3.0):
    """0x05 开始采集。固件 ACK `02 01 05 00 00 00 DD AA` 后持续推送数据帧。"""
    frame = bytes([FRAME_SOF, 0x01, CMD_START_STREAM, 0x00, 0x00, 0x00,
                   FRAME_EOF[0], FRAME_EOF[1]])
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect((host, PORT_APP))
    try:
        s.sendall(frame)
        ack = s.recv(8)
    finally:
        s.close()
    if len(ack) != 8 or ack[2] != CMD_START_STREAM:
        raise ValueError(f"START_STREAM failed, ack={ack.hex()}")


def stop_stream(host: str, timeout: float = 3.0):
    """0x06 停止采集。"""
    frame = bytes([FRAME_SOF, 0x01, CMD_STOP_STREAM, 0x00, 0x00, 0x00,
                   FRAME_EOF[0], FRAME_EOF[1]])
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect((host, PORT_APP))
    try:
        s.sendall(frame)
        ack = s.recv(8)
    finally:
        s.close()
    if len(ack) != 8 or ack[2] != CMD_STOP_STREAM:
        raise ValueError(f"STOP_STREAM failed, ack={ack.hex()}")


def read_version_text(host: str, timeout: float = 3.0):
    """0x07 读版本号，返回响应文本（版本号 + Active IP + Active slot）。"""
    resp = tcp_query(host, CMD_READ_VERSION, timeout)
    if not resp:
        raise OSError(f"Cannot reach {host}:{PORT_APP} or no response")
    return resp.decode("utf-8", "ignore")


def parse_stream_frame(buf):
    """解析 498 字节采集帧。

    帧布局（固件 tcp_echoserver_spi_stream_poll）:
        [0:7]    帧头 02 01 25 31 32 33 34
        [7:487]  数据 480B = SPI1(160B)|SPI2(160B)|SPI3(160B)，每通道 80 点 uint16 大端
        [487:491] 保留 00 00 00 00
        [491:493] 包序号 uint16 大端
        [493]    前 493B 的 8 位累加和
        [494:498] AD DA DD AA

    返回 (pkg_index, ch1[80], ch2[80], ch3[80])；非法帧返回 None。"""
    if len(buf) < STREAM_FRAME_LEN:
        return None
    if buf[0:STREAM_HDR_LEN] != STREAM_HEADER_MAGIC:
        return None
    if buf[STREAM_FRAME_LEN - 4:STREAM_FRAME_LEN] != STREAM_TAIL_MAGIC:
        return None
    if (sum(buf[0:STREAM_FRAME_LEN - 5]) & 0xFF) != buf[STREAM_FRAME_LEN - 5]:
        return None

    pkg = (buf[STREAM_FRAME_LEN - 7] << 8) | buf[STREAM_FRAME_LEN - 6]
    out = []
    for c in range(3):
        off = STREAM_HDR_LEN + c * STREAM_CHAN_BLK
        out.append([(buf[off + i * 2] << 8) | buf[off + i * 2 + 1]
                    for i in range(STREAM_POINTS)])
    return pkg, out[0], out[1], out[2]


def calc_channel_stats(vals):
    """单通道噪声统计。

    返回 (avg, sigma, pkpk)：
      avg   = 均值（直流分量）
      sigma = 样本标准差 sqrt(Σ(x-avg)²/(n-1))（噪声水平）
      pkpk  = 峰峰值 max-min（幅值噪声）
    空/单点列表返回 (0.0, 0.0, 0.0)。"""
    n = len(vals)
    if n == 0:
        return 0.0, 0.0, 0.0
    avg = sum(vals) / n
    if n == 1:
        return avg, 0.0, 0.0
    var = sum((v - avg) ** 2 for v in vals) / (n - 1)
    sigma = var ** 0.5
    pkpk = float(max(vals) - min(vals))
    return avg, sigma, pkpk


# ---------------- TFTP 上传（无第三方依赖） ----------------
def tftp_put(server: str, upload: bytes, retries: int = 5) -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(3)

    # 阶段 1: WRQ -> port 69
    wrq = struct.pack('!H', TFTP_WRQ) + b'firmware.bin\x00' + b'octet\x00'
    s.sendto(wrq, (server, 69))

    ack, peer = None, None
    for _ in range(retries):
        try:
            pkt, peer = s.recvfrom(516)
            if pkt[:4] == struct.pack('!HH', TFTP_ACK, 0):
                ack = pkt
                break
        except socket.timeout:
            s.sendto(wrq, (server, 69))
    if ack is None:
        print("[!] WRQ 无响应（Bootloader 不在 IAP / 网络不通?）")
        s.close()
        return -1

    _, peer_port = peer
    print(f"[*] WRQ OK, server TID = {peer_port}")

    # 阶段 2: DATA 块
    block = 1
    offset = 0
    sent_total = 0
    n = len(upload)

    while True:
        chunk = upload[offset:offset + BLOCK_SIZE]
        is_last = len(chunk) < BLOCK_SIZE
        if offset >= n:               # 512 整数倍时补一个空块标记结束
            chunk = b''
            is_last = True

        pkt = struct.pack('!HH', TFTP_DATA, block) + chunk
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
    print(f"[+] TFTP 完成: {sent_total} B, {block - 1} 块")
    print("[+] 设备即将校验 CRC → commit → 复位 → 跳新固件")
    return 0


# ---------------- 主流程 ----------------
def main():
    ap = argparse.ArgumentParser(description="DP83848 一键 OTA 升级（TFTP）")
    ap.add_argument('--firmware', required=True,
                    help='固件 bin（须链接到 spare 槽地址）')
    ap.add_argument('--ip', default='172.16.1.69',
                    help='设备 IP（默认 172.16.1.69）')
    ap.add_argument('--slot', choices=['auto', 'A', 'B'], default='auto',
                    help='目标 spare 槽：A/B 显式指定（产线推荐）；auto 自动查询（需固件支持）')
    ap.add_argument('--wait', type=int, default=5,
                    help='发 0x0C 后等待 Bootloader 初始化秒数（默认 5）')
    ap.add_argument('--tftp-retries', type=int, default=5,
                    help='TFTP 每块重试次数（默认 5）')
    args = ap.parse_args()

    print("=" * 60)
    print("DP83848 一键 OTA 升级（TFTP）")
    print("=" * 60)

    # 1. 解析固件链接槽
    fw_slot, reset_addr, img = detect_slot(args.firmware)
    if fw_slot is None:
        print(f"[FAIL] 无法识别固件链接槽（Reset_Handler=0x{reset_addr:08X}）")
        print("       固件必须链接到 Slot A(0x0801xxxx) 或 Slot B(0x0806xxxx)。")
        sys.exit(2)
    print(f"[*] 固件   : {args.firmware}")
    print(f"[*] 链接槽 : Slot {fw_slot}  (Reset_Handler=0x{reset_addr:08X})")

    # 2. 确定目标 spare 槽
    app_online = False
    if args.slot == 'auto':
        active_slot, _txt = read_active_slot(args.ip)
        if active_slot:
            app_online = True
            spare = 'A' if active_slot == 'B' else 'B'
            print(f"[*] App 在线: active=Slot {active_slot} → spare=Slot {spare} (auto)")
        else:
            print("[*] App 未在线或固件不支持槽查询（TCP 1030 无 'Active slot' 信息）")
            print("    → 若板子已在 IAP 模式，请用 --slot A 或 --slot B 显式指定目标 spare 槽")
            sys.exit(2)
    else:
        spare = args.slot
        app_online = tcp_query(args.ip, CMD_READ_VERSION) is not None
        print(f"[*] 目标 spare: Slot {spare}（显式指定）")

    # 3. 校验固件链接槽 == spare 槽
    if fw_slot != spare:
        print(f"[FAIL] 固件链接槽 Slot {fw_slot} ≠ 目标 spare 槽 Slot {spare}")
        print(f"       板子 active 槽的另一边才是 spare。请换用链接到 Slot {spare} 的固件，")
        print(f"       或改用 --slot {fw_slot}（若板子 spare 确为 Slot {fw_slot}）。")
        sys.exit(2)
    print(f"[OK] 固件链接槽与目标 spare 槽匹配（Slot {spare}）")

    # 4. 发 ENTER_BOOTLOADER（App 在线时）
    if app_online:
        print("\n[1/4] 发送 ENTER_BOOTLOADER(0x0C) → 设备复位进 Bootloader ...")
        send_enter_bootloader(args.ip)
        print(f"[2/4] 等待 {args.wait}s 让 Bootloader 初始化 LwIP/TFTP ...")
        time.sleep(args.wait)
    else:
        print("\n[1/4] App 不在线，假定设备已在 IAP 模式，直接上传 ...")

    # 5. TFTP 上传
    crc = binascii.crc32(img) & 0xFFFFFFFF
    upload = make_upload(img)
    print(f"[*] CRC32   : 0x{crc:08X}")
    print(f"[*] upload  : {len(upload)} B (fw + 4B CRC trailer)")
    print(f"[3/4] TFTP 上传到 {args.ip}:69 ...")
    rc = tftp_put(args.ip, upload, retries=args.tftp_retries)

    print("=" * 60)
    if rc == 0:
        print("UPGRADE OK — 设备即将校验/提交/复位，运行新固件。")
    else:
        print(f"UPGRADE FAILED (code={rc})")
    print("=" * 60)
    sys.exit(rc)


if __name__ == '__main__':
    main()
