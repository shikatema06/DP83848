#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DP83848 TCP SPI 数据流验证工具集

工具 1: 录 60 秒 TCP 流到 raw 文件
   python3 tcp_capture.py capture --ip 172.16.1.69 --port 1030 --duration 60 --out capture.bin

工具 2: 从录制的文件抽一帧十六进制
   python3 tcp_capture.py extract --in capture.bin --index 0

工具 3: 解析所有帧, 统计丢包/重复/校验错
   python3 tcp_capture.py analyze --in capture.bin --duration 60

工具 4: 实时捕获 + 流式打印进度 + 结束后分析
   python3 tcp_capture.py stream --ip 172.16.1.69 --port 1030 --duration 60
"""

import argparse
import socket
import struct
import sys
import time
from pathlib import Path

# 帧结构常量
FRAME_HDR = bytes.fromhex("02012531323334")   # 帧头 7 字节
FRAME_TAIL = bytes.fromhex("ADDADDAA")         # 帧尾 4 字节: AD DA DD AA (实测板子帧尾，原 ADDADDDDAA 多了一个 DD)
FRAME_LEN = 498                                # 总长
HDR_LEN = 7                                    # 帧头长
DATA_LEN = 480                                 # 数据区长
TAIL_SEQ_OFF = 491                             # tail+4..5 包序号偏移
TAIL_CKSUM_OFF = 493                           # tail+6 checksum 偏移
SEQ_BYTES = 2
CKSUM_LEN = 493                                # checksum 覆盖前 493 字节


def parse_frames(buf):
    """解析一段字节流, 按帧头/帧尾切出所有完整帧.

    Returns:
        pkgs: list of (seq, offset_in_buf, checksum_ok)
        bad_hdr_tail: 没匹配上帧头/帧尾的位置数 (跳过的字节数)
    """
    pkgs = []
    bad_hdr_tail = 0
    i = 0
    n = len(buf)
    while i + FRAME_LEN <= n:
        if buf[i:i + HDR_LEN] != FRAME_HDR:
            i += 1
            continue
        if buf[i + FRAME_LEN - len(FRAME_TAIL):i + FRAME_LEN] != FRAME_TAIL:
            i += 1
            bad_hdr_tail += 1
            continue
        seq = struct.unpack(
            ">H", buf[i + TAIL_SEQ_OFF:i + TAIL_SEQ_OFF + SEQ_BYTES]
        )[0]
        cksum_in_frame = buf[i + TAIL_CKSUM_OFF]
        cksum_calc = sum(buf[i:i + CKSUM_LEN]) & 0xFF
        ok = (cksum_in_frame == cksum_calc)
        pkgs.append((seq, i, ok))
        i += FRAME_LEN
    return pkgs, bad_hdr_tail


def cmd_capture(args):
    """连 TCP 1030, 发 START_STREAM, 录 N 秒到文件."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect((args.ip, args.port))
    s.sendall(bytes.fromhex("020105000000DDAA"))   # CMD_START_STREAM
    print("[capture] connected to {}:{}, START_STREAM sent".format(args.ip, args.port))

    buf = bytearray()
    deadline = time.monotonic() + args.duration
    s.settimeout(2)
    try:
        while time.monotonic() < deadline:
            try:
                chunk = s.recv(8192)
            except socket.timeout:
                continue
            if not chunk:
                break
            buf.extend(chunk)
    finally:
        try:
            s.sendall(bytes.fromhex("020106000000DDAA"))   # CMD_STOP_STREAM
        except OSError:
            pass
        s.close()

    Path(args.out).write_bytes(bytes(buf))
    print("[capture] saved {} bytes to {}".format(len(buf), args.out))


def cmd_extract(args):
    """从录制文件抽第 N 帧, 十六进制打印."""
    buf = Path(args.in_).read_bytes()
    pkgs, _ = parse_frames(buf)
    if not pkgs:
        print("[extract] no valid frames found")
        return
    if args.index >= len(pkgs):
        print("[extract] only {} frames, index {} out of range".format(len(pkgs), args.index))
        return
    seq, off, ok = pkgs[args.index]
    frame = buf[off:off + FRAME_LEN]
    print("[extract] frame #{}: seq={} offset={} checksum_ok={}".format(args.index, seq, off, ok))
    print("hex:")
    for i in range(0, len(frame), 16):
        chunk = frame[i:i + 16]
        hex_str = " ".join("{:02X}".format(b) for b in chunk)
        ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print("  {:04X}  {:<48}  {}".format(i, hex_str, ascii_str))
    print("\ntail (offset 487..497):")
    print("  reserve [487..490]: {} (温湿度预留位)".format(frame[487:491].hex(' ')))
    print("  seq     [491..492]: {} (={})".format(frame[491:493].hex(' '), seq))
    print("  cksum   [493..493]: {:02X}".format(frame[493]))
    print("  end     [494..497]: {}".format(frame[494:498].hex(' ')))


def cmd_analyze(args):
    """分析整段录制文件: 丢包/重复/校验错/速率."""
    buf = Path(args.in_).read_bytes()
    pkgs, bad_hdr_tail = parse_frames(buf)
    if not pkgs:
        print("[analyze] no valid frames")
        return

    seqs = [p[0] for p in pkgs]
    cksum_bad = sum(1 for p in pkgs if not p[2])
    first, last = seqs[0], seqs[-1]

    monotonic = all(seqs[i] <= seqs[i+1] for i in range(len(seqs)-1))
    dup_count = len(seqs) - len(set(seqs))

    drops = []   # (prev_seq, this_seq, dropped_count)
    for i in range(1, len(seqs)):
        diff = (seqs[i] - seqs[i-1]) & 0xFFFF
        if diff > 1:
            drops.append((seqs[i-1], seqs[i], diff - 1))
    total_dropped = sum(d for _, _, d in drops)

    rate = len(pkgs) / max(1e-6, args.duration) if args.duration else 0

    print("[analyze] file: {} ({} bytes)".format(args.in_, len(buf)))
    print("[analyze] valid frames: {}".format(len(pkgs)))
    print("[analyze] bad header/tail skips: {}".format(bad_hdr_tail))
    print("[analyze] checksum failures: {}".format(cksum_bad))
    print("[analyze] duplicate frames: {}".format(dup_count))
    print("[analyze] monotonic sequence: {}".format(monotonic))
    print("[analyze] sequence range: {} -> {}".format(first, last))
    if args.duration:
        print("[analyze] expected ~600-900 frames in {}s (12.5/s)".format(args.duration))
        print("[analyze] observed rate: {:.2f} frames/s".format(rate))
    if drops:
        print("[analyze] gaps (dropped count in parens):")
        for a, b, d in drops[:30]:
            print("  {:5d} -> {:5d}  (dropped {})".format(a, b, d))
        if len(drops) > 30:
            print("  ... ({} more)".format(len(drops) - 30))
        span = (last - first + 1) & 0xFFFF
        if span > 0:
            print("[analyze] total dropped: {} ({:.2f}%)".format(total_dropped, 100 * total_dropped / span))
    else:
        print("[analyze] no gaps detected (perfect sequence)")


def cmd_stream(args):
    """实时 60s 抓包 + 进度打印 + 结束分析."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect((args.ip, args.port))
    s.sendall(bytes.fromhex("020105000000DDAA"))
    print("[stream] connected, START_STREAM sent, capturing {}s...".format(args.duration))

    buf = bytearray()
    deadline = time.monotonic() + args.duration
    s.settimeout(1)
    start = time.monotonic()
    last_print = start
    try:
        while time.monotonic() < deadline:
            try:
                chunk = s.recv(8192)
            except socket.timeout:
                continue
            if not chunk:
                break
            buf.extend(chunk)
            if time.monotonic() - last_print >= 5:
                pkgs, _ = parse_frames(bytes(buf))
                seqs = [p[0] for p in pkgs]
                rate = len(pkgs) / (time.monotonic() - start)
                if seqs:
                    print("  [{:5.1f}s]  parsed={:4d}  rate={:5.1f}/s  seq={}..{}".format(
                        time.monotonic() - start, len(pkgs), rate, seqs[0], seqs[-1]))
                else:
                    print("  [{:5.1f}s]  no complete frames yet".format(time.monotonic() - start))
                last_print = time.monotonic()
    finally:
        try:
            s.sendall(bytes.fromhex("020106000000DDAA"))
        except OSError:
            pass
        s.close()

    Path("capture.bin").write_bytes(bytes(buf))
    print("[stream] saved {} bytes to capture.bin".format(len(buf)))
    print("\n[stream] final report:")
    analyze_args = argparse.Namespace(in_="capture.bin", duration=args.duration)
    cmd_analyze(analyze_args)


def main():
    p = argparse.ArgumentParser(description="DP83848 TCP SPI 数据流验证工具集")
    sub = p.add_subparsers(dest="cmd", required=True)

    p_cap = sub.add_parser("capture", help="录 N 秒 TCP 流到文件")
    p_cap.add_argument("--ip", default="172.16.1.69")
    p_cap.add_argument("--port", type=int, default=1030)
    p_cap.add_argument("--duration", type=float, default=60.0)
    p_cap.add_argument("--out", default="capture.bin")

    p_ext = sub.add_parser("extract", help="从录制文件抽指定帧的十六进制")
    p_ext.add_argument("--in", dest="in_", required=True)
    p_ext.add_argument("--index", type=int, default=0)

    p_ana = sub.add_parser("analyze", help="分析整段录制的文件")
    p_ana.add_argument("--in", dest="in_", required=True)
    p_ana.add_argument("--duration", type=float, default=60.0,
                       help="录制时长 (秒), 仅用于输出 expected 区间参考")

    p_str = sub.add_parser("stream", help="实时 60s 捕获 + 进度 + 分析")
    p_str.add_argument("--ip", default="172.16.1.69")
    p_str.add_argument("--port", type=int, default=1030)
    p_str.add_argument("--duration", type=float, default=60.0)

    args = p.parse_args()
    {"capture": cmd_capture, "extract": cmd_extract,
     "analyze": cmd_analyze, "stream":  cmd_stream}[args.cmd](args)


if __name__ == "__main__":
    main()