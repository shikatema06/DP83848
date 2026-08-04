#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DP83848 7x24 长期数据流监测守护脚本 (production-grade, 无第三方依赖)

设计要点:
  1. 双连接:
     - 流连接 (port 1030): 发 START_STREAM, 持续收 SPI 数据流, 增量解析帧 (内存恒定)
     - 控制连接 (port 1030, 每条轮询新建): 发 "uptime"/"status" 文本命令,
       PC 侧无法区分"网络断线"与"板子重启", 必须靠 uptime 回退来检测重启
  2. 增量帧解析: 滚动缓冲 (cap ~2KB), 边收边切帧, 不缓存全天数据
  3. 断线指数退避重连 (1/2/4/8/16/30s 封顶), 重连成功后复位退避
  4. 每日日志轮转 (monitor_YYYYMMDD.log) + 每小时汇总块
  5. stall 检测: 数据流 > STALL_TIMEOUT 秒无新帧即告警 (兜底背压丢帧)
  6. 优雅退出: SIGINT/SIGTERM -> 发 STOP_STREAM + 关连接 + flush
  7. 可选 --status-json: 周期写 JSON 快照, 供外部监控/dashboard 抓取

用法:
  python3 monitor_7x24.py --ip 172.16.1.69 --port 1030 --hours 0
  python3 monitor_7x24.py --ip 172.16.1.69 --hours 12 --log-dir ./logs
  python3 monitor_7x24.py --ip 172.16.1.69 --hours 0 --status-json ./status.json
"""

import argparse
import json
import signal
import socket
import struct
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path

# ===================== 帧结构常量 (与板子 tcp_echoserver.c 一致) =====================
FRAME_HDR = bytes.fromhex("02012531323334")   # 帧头 7 字节
FRAME_TAIL = bytes.fromhex("ADDADDAA")         # 帧尾 4 字节 (已修正: 原脚本误写为 ADDADDDDAA)
FRAME_LEN = 498                                # 总长
HDR_LEN = 7
DATA_LEN = 480
TAIL_SEQ_OFF = 491                             # seq (2B) 偏移
TAIL_CKSUM_OFF = 493                           # checksum (1B) 偏移
CKSUM_LEN = 493                                # checksum 覆盖前 493 字节
SEQ_BYTES = 2

# 命令 (与板子协议一致)
CMD_START = bytes.fromhex("020105000000DDAA")
CMD_STOP = bytes.fromhex("020106000000DDAA")

# 控制连接文本命令
TXT_UPTIME = b"uptime\r\n"
TXT_STATUS = b"status\r\n"

# ===================== 配置默认值 =====================
DEFAULT_STALL_TIMEOUT = 5.0      # 秒: 无新帧超过此值视为 stall
DEFAULT_CTRL_INTERVAL = 60.0     # 秒: 控制连接轮询周期
DEFAULT_BACKOFF_CAP = 30.0       # 秒: 重连退避上限
DEFAULT_STATUS_INTERVAL = 60.0   # 秒: JSON 状态快照周期
ROLL_BUF_CAP = 2048              # 滚动缓冲上限, 超出未解出帧则判定为失步并重置


# ===================== 增量帧解析器 (内存恒定) =====================
class FrameParser:
    """维护一个小滚动缓冲, 边收边切完整帧. 解析出的帧回调 on_frame(seq, cksum_ok)."""

    def __init__(self, on_frame):
        self.buf = bytearray()
        self.on_frame = on_frame
        self._search = 0  # 下次搜索起点

    def feed(self, chunk):
        self.buf.extend(chunk)
        if len(self.buf) > ROLL_BUF_CAP:
            # 缓冲膨胀过多 => 失步/乱码, 截断前段保末尾 (末尾更可能是有效数据起点)
            self.buf = self.buf[-ROLL_BUF_CAP // 2:]
            self._search = 0
        self._drain()

    def _drain(self):
        n = len(self.buf)
        while True:
            # 从 _search 位置找帧头
            pos = self.buf.find(FRAME_HDR, self._search)
            if pos < 0:
                # 本段无帧头: 丢弃已消费部分, 保留尾部可能半截帧
                keep = max(0, n - (HDR_LEN - 1))
                del self.buf[:keep]
                self._search = 0
                return
            end = pos + FRAME_LEN
            if end > len(self.buf):
                # 帧头在, 但整帧未到齐, 等更多数据
                self._search = pos  # 下次仍从此帧头试
                # 但为避免半截帧长期占用, 截断其前无效部分
                if pos > 0:
                    del self.buf[:pos]
                    self._search = 0
                return
            # 整帧到齐, 校验帧尾
            tail = self.buf[end - len(FRAME_TAIL):end]
            if tail == FRAME_TAIL:
                seq = struct.unpack(">H", bytes(self.buf[pos + TAIL_SEQ_OFF:pos + TAIL_SEQ_OFF + SEQ_BYTES]))[0]
                cksum_in = self.buf[pos + TAIL_CKSUM_OFF]
                cksum_calc = sum(self.buf[pos:pos + CKSUM_LEN]) & 0xFF
                ok = (cksum_in == cksum_calc)
                self.on_frame(seq, ok)
                # 消费该帧
                del self.buf[:end]
                self._search = 0
            else:
                # 帧头命中但帧尾不符: 候选无效, 跳 1 字节重新找 (避免死循环)
                self._search = pos + 1


# ===================== 控制连接探针 (检测重启) =====================
class ControlProbe:
    """每个轮询周期新建一条短连接, 发 uptime 命令, 解析返回值.
    用 uptime 回退 (处理 49.7 天环绕) 检测板子重启/掉电."""

    def __init__(self, ip, port, timeout=3.0):
        self.ip = ip
        self.port = port
        self.timeout = timeout

    def query_uptime_ms(self):
        """返回 (ok, uptime_ms) ; ok=False 表示连接/读超时或解析失败."""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(self.timeout)
            s.connect((self.ip, self.port))
            s.sendall(TXT_UPTIME)
            data = b""
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                try:
                    chunk = s.recv(256)
                except socket.timeout:
                    break
                if not chunk:
                    break
                data += chunk
                if b"\r\n" in data and b"Uptime:" in data:
                    break
            s.close()
        except OSError:
            return (False, 0)
        # 解析 "Uptime: 1234ms = ..."
        txt = data.decode("latin-1", "ignore")
        idx = txt.find("Uptime:")
        if idx < 0:
            return (False, 0)
        sp = txt.find("ms", idx)
        if sp < 0:
            return (False, 0)
        num = txt[idx + len("Uptime:"):sp].strip()
        try:
            return (True, int(num))
        except ValueError:
            return (False, 0)


# ===================== 指标统计 =====================
class Metrics:
    def __init__(self):
        self.reset_cumulative()
        self.reset_window()

    def reset_cumulative(self):
        self.bytes_recv = 0
        self.frames_total = 0
        self.cksum_bad = 0
        self.tail_bad = 0       # 失步丢弃计数 (可供观察)
        self.dropped = 0        # 序号缺口累计丢帧
        self.dups = 0
        self.reboots = 0
        self.stream_disconnects = 0
        self.control_failures = 0
        self.stall_episodes = 0
        self.last_seq = None

    def reset_window(self):
        # 每小时窗口 (与累计分开)
        self.w_bytes = 0
        self.w_frames = 0
        self.w_cksum_bad = 0
        self.w_dropped = 0
        self.w_dups = 0
        self.w_reboots = 0
        self.w_disconnects = 0
        self.w_ctrl_fail = 0
        self.w_stall = 0

    def on_frame(self, seq, cksum_ok):
        self.frames_total += 1
        self.w_frames += 1
        if not cksum_ok:
            self.cksum_bad += 1
            self.w_cksum_bad += 1
            return
        s = seq
        if self.last_seq is None:
            self.last_seq = s
            return
        diff = (s - self.last_seq) & 0xFFFF
        if diff == 1:
            pass
        elif diff == 0:
            self.dups += 1
            self.w_dups += 1
        else:
            dropped = diff - 1
            if dropped > 100:
                # 大缺口 (长时间 stall/重连后), 单独记一次, 不逐帧展开
                self.dropped += dropped
                self.w_dropped += dropped
            else:
                self.dropped += dropped
                self.w_dropped += dropped
        self.last_seq = s


# ===================== 日志 =====================
class DailyLogger:
    def __init__(self, log_dir, tag="monitor"):
        self.log_dir = Path(log_dir)
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self.tag = tag
        self.fh = None
        self.cur_date = None

    def _ensure(self):
        d = datetime.now().strftime("%Y%m%d")
        if d != self.cur_date:
            if self.fh:
                self.fh.close()
            self.cur_date = d
            self.fh = open(self.log_dir / f"{self.tag}_{d}.log", "a", encoding="utf-8", buffering=1)

    def log(self, msg):
        self._ensure()
        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        line = f"[{ts}] {msg}"
        print(line, flush=True)
        if self.fh:
            self.fh.write(line + "\n")

    def close(self):
        if self.fh:
            self.fh.close()
            self.fh = None


# ===================== 主监测器 =====================
class Monitor:
    def __init__(self, args):
        self.ip = args.ip
        self.port = args.port
        self.max_hours = args.hours
        self.stall_timeout = args.stall_timeout
        self.ctrl_interval = args.ctrl_interval
        self.backoff_cap = args.backoff_cap
        self.status_json = args.status_json
        self.status_interval = args.status_interval

        self.logger = DailyLogger(args.log_dir, tag="monitor")
        self.metrics = Metrics()
        self.parser = FrameParser(self._on_frame)
        self.probe = ControlProbe(self.ip, self.port, timeout=3.0)

        self.running = True
        self.sock = None
        self.last_frame_ts = time.monotonic()
        self.last_ctrl_ts = 0.0
        self.last_status_ts = 0.0
        self.last_uptime = None
        self.in_stall = False
        self.cur_hour = datetime.now().hour
        self.start_ts = time.monotonic()
        self.backoff = 1.0

    # ---- 信号 ----
    def _install_signals(self):
        def _h(signum, frame):
            self.logger.log(f"SIGNAL {signum} received, shutting down gracefully...")
            self.running = False
        signal.signal(signal.SIGINT, _h)
        signal.signal(signal.SIGTERM, _h)

    # ---- 流连接 ----
    def _connect_stream(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(1.0)
        self.sock.connect((self.ip, self.port))
        self.sock.sendall(CMD_START)
        self.last_frame_ts = time.monotonic()
        self.metrics.last_seq = None  # 重连后不对比旧序号 (避免把重连缺口当丢帧)
        self.logger.log(f"[connect] stream connected to {self.ip}:{self.port}, START_STREAM sent")

    def _close_stream(self, reason=""):
        if self.sock is not None:
            try:
                self.sock.sendall(CMD_STOP)
            except OSError:
                pass
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None
        if reason:
            self.logger.log(f"[stream] closed ({reason})")

    def _on_frame(self, seq, cksum_ok):
        self.metrics.on_frame(seq, cksum_ok)
        self.last_frame_ts = time.monotonic()
        if self.in_stall:
            self.in_stall = False
            self.logger.log(f"[stall] RECOVERED, seq={seq}")

    # ---- 控制轮询 ----
    def _do_ctrl_poll(self):
        ok, uptime = self.probe.query_uptime_ms()
        if not ok:
            self.metrics.control_failures += 1
            self.metrics.w_ctrl_fail += 1
            self.logger.log("[ctrl] probe FAILED (connection/timeout/parse)")
            return
        # 重启检测: uptime 回退 (排除 49.7 天环绕)
        if self.last_uptime is not None:
            delta = (uptime - self.last_uptime) & 0xFFFFFFFF
            if delta > 0x80000000:  # 大幅回退 => 重启 (而非环绕)
                self.metrics.reboots += 1
                self.metrics.w_reboots += 1
                self.logger.log(f"[REBOOT] detected! uptime {self.last_uptime}ms -> {uptime}ms")
        elif self.metrics.stream_disconnects > 0:
            # 首次读到 uptime 且此前发生过断流, 若 uptime 很小也疑似重启
            if uptime < 60000:
                self.metrics.reboots += 1
                self.metrics.w_reboots += 1
                self.logger.log(f"[REBOOT] suspected after disconnect (uptime={uptime}ms)")
        self.last_uptime = uptime
        self.logger.log(f"[ctrl] uptime={uptime}ms, frames_total={self.metrics.frames_total}, dropped={self.metrics.dropped}")

    # ---- 每小时汇总 ----
    def _hourly_summary(self):
        m = self.metrics
        h = datetime.now().strftime("%Y-%m-%d %H:00")
        rate = (m.w_frames / 3600.0) if m.w_frames else 0
        block = (
            f"\n===== HOURLY SUMMARY {h} =====\n"
            f"  frames        : {m.w_frames} (~{rate:.2f}/s)\n"
            f"  cksum errors  : {m.w_cksum_bad}\n"
            f"  seq dropped   : {m.w_dropped}\n"
            f"  dups          : {m.w_dups}\n"
            f"  reboots       : {m.w_reboots}\n"
            f"  stream disc.  : {m.w_disconnects}\n"
            f"  ctrl failures : {m.w_ctrl_fail}\n"
            f"  stalls        : {m.w_stall}\n"
            f"  --- cumulative ---\n"
            f"  frames        : {m.frames_total}\n"
            f"  dropped total : {m.dropped}\n"
            f"  reboots total : {m.reboots}\n"
            f"==============================\n"
        )
        self.logger.log(block)
        m.reset_window()

    # ---- JSON 状态快照 ----
    def _write_status_json(self):
        if not self.status_json:
            return
        m = self.metrics
        snap = {
            "ts": datetime.now().isoformat(timespec="seconds"),
            "ip": self.ip,
            "uptime_s": (time.monotonic() - self.start_ts),
            "stream_connected": self.sock is not None,
            "last_uptime_ms": self.last_uptime,
            "frames_total": m.frames_total,
            "frames_window": m.w_frames,
            "cksum_bad": m.cksum_bad,
            "dropped": m.dropped,
            "dups": m.dups,
            "reboots": m.reboots,
            "stream_disconnects": m.stream_disconnects,
            "control_failures": m.control_failures,
            "stall_episodes": m.stall_episodes,
            "in_stall": self.in_stall,
            "backoff_s": self.backoff,
        }
        try:
            Path(self.status_json).write_text(json.dumps(snap, indent=2), encoding="utf-8")
        except OSError as e:
            self.logger.log(f"[status] write json failed: {e}")

    # ---- 主循环 ----
    def run(self):
        self._install_signals()
        self.logger.log(f"===== 7x24 monitor start: target {self.ip}:{self.port}, "
                        f"max_hours={self.max_hours} =====")
        while self.running:
            # 超时保护
            if self.max_hours > 0 and (time.monotonic() - self.start_ts) > self.max_hours * 3600:
                self.logger.log("[exit] reached max_hours, stopping")
                break

            # 建立/恢复流连接
            if self.sock is None:
                try:
                    self._connect_stream()
                    self.backoff = 1.0
                except OSError as e:
                    self.metrics.stream_disconnects += 1
                    self.metrics.w_disconnects += 1
                    self.logger.log(f"[stream] connect FAILED: {e}, retry in {self.backoff:.0f}s")
                    time.sleep(self.backoff)
                    self.backoff = min(self.backoff * 2, self.backoff_cap)
                    continue

            # 收流数据
            try:
                chunk = self.sock.recv(8192)
            except socket.timeout:
                chunk = b""
            except OSError as e:
                self.logger.log(f"[stream] recv error: {e}")
                self._close_stream("recv error")
                self.metrics.stream_disconnects += 1
                self.metrics.w_disconnects += 1
                time.sleep(self.backoff)
                self.backoff = min(self.backoff * 2, self.backoff_cap)
                continue

            if chunk == b"":
                # 对端关闭
                self.logger.log("[stream] peer closed")
                self._close_stream("peer closed")
                self.metrics.stream_disconnects += 1
                self.metrics.w_disconnects += 1
                time.sleep(self.backoff)
                self.backoff = min(self.backoff * 2, self.backoff_cap)
                continue

            self.metrics.bytes_recv += len(chunk)
            self.metrics.w_bytes += len(chunk)
            self.parser.feed(chunk)

            # stall 检测 (当前一直连着, 但无帧 => 背压/链路异常)
            now = time.monotonic()
            if (now - self.last_frame_ts) > self.stall_timeout:
                if not self.in_stall:
                    self.in_stall = True
                    self.metrics.stall_episodes += 1
                    self.metrics.w_stall += 1
                    self.logger.log(f"[stall] NO FRAME for >{self.stall_timeout:.0f}s (possible backpressure/link issue)")

            # 控制轮询
            if (now - self.last_ctrl_ts) >= self.ctrl_interval:
                self.last_ctrl_ts = now
                self._do_ctrl_poll()

            # 每小时汇总
            if datetime.now().hour != self.cur_hour:
                self.cur_hour = datetime.now().hour
                self._hourly_summary()

            # JSON 快照
            if self.status_json and (now - self.last_status_ts) >= self.status_interval:
                self.last_status_ts = now
                self._write_status_json()

        # 收尾
        self._close_stream("shutdown")
        self._hourly_summary()
        self._write_status_json()
        self.logger.log("===== 7x24 monitor stopped =====")
        self.logger.close()


# ===================== 入口 =====================
def main():
    p = argparse.ArgumentParser(description="DP83848 7x24 数据流长期监测守护脚本")
    p.add_argument("--ip", default="172.16.1.69")
    p.add_argument("--port", type=int, default=1030)
    p.add_argument("--hours", type=float, default=0.0,
                   help="最大运行小时数, 0=无限 (直到被信号终止)")
    p.add_argument("--log-dir", default="./logs")
    p.add_argument("--stall-timeout", type=float, default=DEFAULT_STALL_TIMEOUT)
    p.add_argument("--ctrl-interval", type=float, default=DEFAULT_CTRL_INTERVAL)
    p.add_argument("--backoff-cap", type=float, default=DEFAULT_BACKOFF_CAP,
                   help="断线重连退避上限(秒)")
    p.add_argument("--status-json", default=None,
                   help="可选: 周期写 JSON 状态快照的路径, 供外部监控抓取")
    p.add_argument("--status-interval", type=float, default=DEFAULT_STATUS_INTERVAL,
                   help="JSON 状态快照写入周期(秒)")
    args = p.parse_args()

    mon = Monitor(args)
    mon.run()


if __name__ == "__main__":
    main()
