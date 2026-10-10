#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DP83848_Upgrade_GUI.py — 图形化一键 OTA 升级 + IP 切换（TFTP）

产线使用：双击 exe 打开窗口 →
    Tab「IP 配置」: 查询设备 IP 列表 / 切换 IP / 恢复默认 172.16.1.69
    Tab「固件升级」: 选择固件 → 选择目标槽 → 点"开始升级"
无需命令行，无需安装 Python。

依赖同目录 onekey_upgrade.py 的核心函数。

打包:
    python -m PyInstaller --onefile --windowed --name DP83848_Upgrade_GUI \
        DP83848_Upgrade_GUI.py
"""

import binascii
import socket
import threading
import time
from datetime import datetime
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from onekey_upgrade import (detect_slot, make_upload, read_active_slot,
                            send_enter_bootloader, tftp_put, tcp_query,
                            list_ips, select_ip, restore_default_ip,
                            start_stream, stop_stream, read_version_text,
                            parse_stream_frame, calc_channel_stats,
                            STREAM_FRAME_LEN, CMD_READ_VERSION)

DEFAULT_IP = "172.16.1.69"


class UpgradeGUI:
    def __init__(self, root):
        self.root = root
        root.title("三探头v3版本 OTA升级 + IP配置")
        root.geometry("720x600")
        self.worker = None
        self.ips = []          # 缓存 IP 列表 [(ip, valid)]

        # ---- 采集状态 ----
        self.acq_running = False
        self.acq_sock = None
        self.acq_buf = {0: [], 1: [], 2: []}   # 每通道最近 N 点
        self.acq_last_pkg = None
        self.acq_lost = 0
        self.acq_bad = 0
        self.acq_max_pts = 1000                 # 曲线显示窗口(点)
        self.acq_csv = None
        self.acq_csv_path = None
        self.acq_mode = "raw"                   # raw / first / stats
        self.acq_interval = 10
        self.acq_noise = {0: None, 1: None, 2: None}
        self.acq_trend = {0: [], 1: [], 2: []}
        self.acq_auto_vmin = None
        self.acq_auto_vmax = None

        self._build()
        root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _on_close(self):
        self._acq_stop()
        self.root.destroy()

    # ================= 界面 =================
    def _build(self):
        nb = ttk.Notebook(self.root)
        nb.pack(fill=tk.BOTH, expand=True)

        self._build_ip_tab(nb)
        self._build_upgrade_tab(nb)
        self._build_acq_tab(nb)

    # ---- Tab 1: IP 配置 ----
    def _build_ip_tab(self, nb):
        tab = ttk.Frame(nb, padding=10)
        nb.add(tab, text=" IP 配置 ")

        # 连接 IP + 查询
        row = ttk.Frame(tab)
        row.pack(fill=tk.X, pady=3)
        ttk.Label(row, text="连接 IP:").pack(side=tk.LEFT)
        self.cfg_ip_var = tk.StringVar(value=DEFAULT_IP)
        ttk.Entry(row, textvariable=self.cfg_ip_var, width=16).pack(side=tk.LEFT)
        ttk.Button(row, text="查询列表", command=self._query_ips).pack(side=tk.LEFT, padx=4)
        ttk.Button(row, text="恢复默认(172.16.1.69)", command=self._restore_default).pack(side=tk.LEFT)

        # IP 列表
        cols = ("idx", "ip", "status")
        self.tree = ttk.Treeview(tab, columns=cols, show="headings", height=8)
        self.tree.heading("idx", text="序号")
        self.tree.heading("ip", text="IP 地址")
        self.tree.heading("status", text="状态")
        self.tree.column("idx", width=60, anchor=tk.CENTER)
        self.tree.column("ip", width=160, anchor=tk.CENTER)
        self.tree.column("status", width=140, anchor=tk.CENTER)
        sb = ttk.Scrollbar(tab, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        self.tree.pack(fill=tk.BOTH, expand=True)
        sb.pack(side=tk.RIGHT, fill=tk.Y)

        ttk.Button(tab, text="切换到此 IP", command=self._switch_selected).pack(pady=6)

        # 日志
        self.cfg_log = tk.Text(tab, height=12, state=tk.DISABLED, font=("Consolas", 10))
        self.cfg_log.pack(fill=tk.BOTH, expand=True)

        ttk.Label(tab,
                  text="提示: 切换后设备 IP 立即变化; 目标网段需与 PC 网卡同段(172.16.x.x/24)",
                  foreground="gray").pack(anchor=tk.W)

    # ---- Tab 2: 固件升级 ----
    def _build_upgrade_tab(self, nb):
        tab = ttk.Frame(nb, padding=10)
        nb.add(tab, text=" 固件升级 ")

        # 设备 IP
        row = ttk.Frame(tab)
        row.pack(fill=tk.X, pady=3)
        ttk.Label(row, text="设备 IP:").pack(side=tk.LEFT)
        self.ip_var = tk.StringVar(value=DEFAULT_IP)
        ttk.Entry(row, textvariable=self.ip_var, width=16).pack(side=tk.LEFT)

        # 固件文件
        row = ttk.Frame(tab)
        row.pack(fill=tk.X, pady=3)
        ttk.Label(row, text="固件文件:").pack(side=tk.LEFT)
        self.fw_var = tk.StringVar()
        ttk.Entry(row, textvariable=self.fw_var).pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Button(row, text="浏览…", command=self._browse).pack(side=tk.LEFT, padx=4)

        # 目标槽模式
        row = ttk.Frame(tab)
        row.pack(fill=tk.X, pady=3)
        ttk.Label(row, text="目标槽:").pack(side=tk.LEFT)
        self.slot_var = tk.StringVar(value="auto")
        ttk.Radiobutton(row, text="自动", value="auto", variable=self.slot_var).pack(side=tk.LEFT)
        ttk.Radiobutton(row, text="Slot A", value="A", variable=self.slot_var).pack(side=tk.LEFT)
        ttk.Radiobutton(row, text="Slot B", value="B", variable=self.slot_var).pack(side=tk.LEFT)

        # 开始按钮
        self.btn = ttk.Button(tab, text="开始升级", command=self._start)
        self.btn.pack(pady=8)

        # 日志区
        self.log = tk.Text(tab, height=14, state=tk.DISABLED, font=("Consolas", 10))
        self.log.pack(fill=tk.BOTH, expand=True)

        ttk.Label(tab,
                  text="提示: 固件须链接到 spare 槽地址; 自动模式需板子已烧支持槽查询的固件",
                  foreground="gray").pack(anchor=tk.W)

    # ================= IP 配置页 =================
    def _cfg(self, msg):
        self.cfg_log.configure(state=tk.NORMAL)
        self.cfg_log.insert(tk.END, msg + "\n")
        self.cfg_log.see(tk.END)
        self.cfg_log.configure(state=tk.DISABLED)

    def _query_ips(self):
        if self.worker and self.worker.is_alive():
            return
        host = self.cfg_ip_var.get().strip()
        self._cfg(f"[*] 查询 {host} 的 IP 列表 ...")
        self.worker = threading.Thread(target=self._query_thread, args=(host,), daemon=True)
        self.worker.start()

    def _query_thread(self, host):
        try:
            count, active, slots = list_ips(host)
        except Exception as e:
            self._cfg(f"[FAIL] 查询失败: {e}")
            self._cfg("       请确认设备 IP 正确、网线连接、PC 网卡网段匹配")
            return
        self.ips = slots
        self.root.after(0, lambda: self._render_list(count, active, slots))

    def _render_list(self, count, active, slots):
        self.tree.delete(*self.tree.get_children())
        for i, (ip, valid) in enumerate(slots[:count]):
            st = "可用" if valid else "不可用"
            if i == active:
                st += "  ← 当前"
            self.tree.insert("", tk.END, values=(i, ip, st))
        self._cfg(f"[OK] 列表 count={count}, active={active} ({slots[active][0] if active < count else '?'})")

    def _switch_selected(self):
        sel = self.tree.selection()
        if not sel:
            self._cfg("[!] 请先在列表中选择一行")
            return
        if self.worker and self.worker.is_alive():
            return
        idx = int(self.tree.item(sel[0], "values")[0])
        target_ip = self.ips[idx][0]
        cur = self.cfg_ip_var.get().strip()
        self.worker = threading.Thread(target=self._switch_thread,
                                       args=(cur, idx, target_ip), daemon=True)
        self.worker.start()

    def _switch_thread(self, cur, idx, target_ip):
        self._cfg(f"[*] 发送 SELECT idx {idx} → {target_ip}")
        try:
            select_ip(cur, idx)
        except Exception as e:
            self._cfg(f"[FAIL] 发送 SELECT 失败: {e}")
            return
        self._cfg("[*] 设备正在切换 IP，自动重连验证 ...")
        for attempt in range(1, 4):
            time.sleep(2.0 if attempt == 1 else 1.5)
            try:
                count, active, _slots = list_ips(target_ip)
            except Exception:
                continue
                count, active, _slots = list_ips(target_ip)
            except Exception:
                continue
            if active == idx:
                self._cfg(f"[OK] 验证成功：{target_ip} 已是 active（第 {attempt} 次尝试）")
                self.root.after(0, lambda: self.ip_var.set(target_ip))
                self.root.after(0, lambda: self.cfg_ip_var.set(target_ip))
                self.root.after(0, lambda: self.acq_ip_var.set(target_ip))
                self._cfg(f"[*] 已同步「固件升级」页设备 IP → {target_ip}")
                self._cfg(f"[*] 已同步「数据采集」页设备 IP → {target_ip}")
                return
        self._cfg(f"[FAIL] 重连验证 {target_ip} 失败")
        self._cfg("       请确认 PC 网卡已配置 172.16.x.x 网段，再手动重试")

    def _restore_default(self):
        if self.worker and self.worker.is_alive():
            return
        cur = self.cfg_ip_var.get().strip()
        self._cfg(f"[*] 发送 RESTORE(0x0B) → 恢复默认 {DEFAULT_IP}")
        self.worker = threading.Thread(target=self._restore_thread, args=(cur,), daemon=True)
        self.worker.start()

    def _restore_thread(self, cur):
        try:
            restore_default_ip(cur)
        except Exception as e:
            self._cfg(f"[FAIL] 发送 RESTORE 失败: {e}")
            return
        self._cfg("[*] 设备正在恢复默认 IP，自动重连验证 ...")
        for attempt in range(1, 4):
            time.sleep(2.0 if attempt == 1 else 1.5)
            try:
                count, active, _ = list_ips(DEFAULT_IP)
            except Exception:
                continue
            self._cfg(f"[OK] 已恢复默认 IP {DEFAULT_IP} (count={count}, active={active})")
            self.root.after(0, lambda: self.ip_var.set(DEFAULT_IP))
            self.root.after(0, lambda: self.cfg_ip_var.set(DEFAULT_IP))
            self.root.after(0, lambda: self.acq_ip_var.set(DEFAULT_IP))
            return
        self._cfg(f"[FAIL] 重连验证 {DEFAULT_IP} 失败，请检查 PC 网卡网段")

    # ================= 固件升级页 =================
    def _browse(self):
        p = filedialog.askopenfilename(title="选择固件",
                                       filetypes=[("固件 BIN", "*.bin"), ("所有文件", "*.*")])
        if p:
            self.fw_var.set(p)

    def _log(self, msg):
        self.log.configure(state=tk.NORMAL)
        self.log.insert(tk.END, msg + "\n")
        self.log.see(tk.END)
        self.log.configure(state=tk.DISABLED)

    def _start(self):
        if self.worker and self.worker.is_alive():
            return
        self.btn.configure(state=tk.DISABLED)
        self._log("=" * 52)
        self.worker = threading.Thread(target=self._run, daemon=True)
        self.worker.start()

    def _run(self):
        ip = self.ip_var.get().strip()
        fw = self.fw_var.get().strip()
        mode = self.slot_var.get()
        self._log(f"[*] 设备 IP : {ip}")
        self._log(f"[*] 固件    : {fw}")

        if not fw:
            self._log("[FAIL] 未选择固件文件")
            self._finish(False)
            return

        try:
            fw_slot, reset_addr, img = detect_slot(fw)
        except FileNotFoundError:
            self._log("[FAIL] 固件文件不存在")
            self._finish(False)
            return
        if fw_slot is None:
            self._log(f"[FAIL] 无法识别固件链接槽 (Reset_Handler=0x{reset_addr:08X})")
            self._finish(False)
            return
        self._log(f"[*] 固件链接槽: Slot {fw_slot}")

        # ---- 确定目标 spare 槽 ----
        app_online = False
        if mode == "auto":
            active, _ = read_active_slot(ip)
            if active:
                app_online = True
                spare = "B" if active == "A" else "A"
                self._log(f"[*] active=Slot {active} → spare=Slot {spare} (自动)")
            else:
                self._log("[FAIL] 无法查询 active 槽（App 不在线或固件不支持槽查询）")
                self._log("       → 请手动选择 Slot A 或 Slot B")
                self._finish(False)
                return
        else:
            spare = mode
            app_online = tcp_query(ip, CMD_READ_VERSION) is not None
            self._log(f"[*] 目标 spare: Slot {spare} (显式)")

        # ---- 校验固件链接槽 == spare 槽 ----
        if fw_slot != spare:
            self._log(f"[FAIL] 固件链接槽 Slot {fw_slot} ≠ 目标 spare 槽 Slot {spare}")
            self._log(f"       请换用链接到 Slot {spare} 的固件，或改选目标槽 Slot {fw_slot}")
            self._finish(False)
            return
        self._log(f"[OK] 固件链接槽与目标 spare 槽匹配 (Slot {spare})")

        # ---- 发 ENTER_BOOTLOADER ----
        if app_online:
            self._log("[1/4] 发送 ENTER_BOOTLOADER(0x0C)，设备复位进 Bootloader ...")
            send_enter_bootloader(ip)
            self._log("[2/4] 等待 Bootloader 初始化 LwIP/TFTP ...")
            time.sleep(5)
        else:
            self._log("[1/4] App 不在线，假定设备已在 IAP 模式，直接上传 ...")

        # ---- TFTP 上传 ----
        crc = binascii.crc32(img) & 0xFFFFFFFF
        upload = make_upload(img)
        self._log(f"[*] CRC32 : 0x{crc:08X}")
        self._log(f"[*] 上传  : {len(upload)} B (固件 + 4B CRC)")
        self._log("[3/4] TFTP 上传中 ...")
        rc = tftp_put(ip, upload)

        if rc == 0:
            self._log("[OK] 上传完成，设备即将校验 CRC → 提交 → 复位运行新固件")
            self._finish(True)
        else:
            self._log(f"[FAIL] TFTP 上传失败 (code={rc})")
            self._finish(False)

    def _finish(self, ok):
        self.root.after(0, lambda: self.btn.configure(state=tk.NORMAL))
        if ok:
            self.root.after(0, lambda: messagebox.showinfo(
                "完成", "升级完成！设备即将复位并运行新固件。"))
        else:
            self.root.after(0, lambda: messagebox.showerror("失败", "升级失败，请查看日志。"))

    # ================= Tab 3: 数据采集 =================
    def _build_acq_tab(self, nb):
        tab = ttk.Frame(nb, padding=10)
        nb.add(tab, text=" 数据采集 ")

        # 设备 IP + 版本号 + 开始/停止
        row = ttk.Frame(tab)
        row.pack(fill=tk.X, pady=2)
        ttk.Label(row, text="设备 IP:").pack(side=tk.LEFT)
        self.acq_ip_var = tk.StringVar(value=DEFAULT_IP)
        ttk.Entry(row, textvariable=self.acq_ip_var, width=14).pack(side=tk.LEFT)
        ttk.Button(row, text="读版本号", command=self._acq_read_version).pack(side=tk.LEFT, padx=6)
        self.acq_btn_start = ttk.Button(row, text="开始采集", command=self._acq_start)
        self.acq_btn_start.pack(side=tk.LEFT, padx=4)
        self.acq_btn_stop = ttk.Button(row, text="停止采集", command=self._acq_stop, state=tk.DISABLED)
        self.acq_btn_stop.pack(side=tk.LEFT)
        self.acq_status = tk.StringVar(value="未采集")
        ttk.Label(row, textvariable=self.acq_status, foreground="blue").pack(side=tk.LEFT, padx=10)

        # 显示比例
        row = ttk.Frame(tab)
        row.pack(fill=tk.X, pady=2)
        ttk.Label(row, text="Y 轴比例:").pack(side=tk.LEFT)
        self.acq_yscale = tk.StringVar(value="自动")
        ttk.Combobox(row, textvariable=self.acq_yscale, values=("自动", "固定"),
                     state="readonly", width=6).pack(side=tk.LEFT)
        ttk.Label(row, text="范围 min:").pack(side=tk.LEFT, padx=(8, 0))
        self.acq_ymin_var = tk.StringVar(value="0")
        ttk.Entry(row, textvariable=self.acq_ymin_var, width=7).pack(side=tk.LEFT)
        ttk.Label(row, text="max:").pack(side=tk.LEFT)
        self.acq_ymax_var = tk.StringVar(value="65535")
        ttk.Entry(row, textvariable=self.acq_ymax_var, width=7).pack(side=tk.LEFT)

        # 三通道波形曲线（含坐标轴）
        self.canvas = tk.Canvas(tab, bg="white", height=200)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        leg = ttk.Frame(tab)
        leg.pack(fill=tk.X)
        for name, col in (("探头1 (SPI1)", "#d00"), ("探头2 (SPI2)", "#0a0"),
                          ("探头3 (SPI3)", "#00c")):
            ttk.Label(leg, text=name, foreground=col).pack(side=tk.LEFT, padx=10)

        # 噪声统计（每通道 均值/σ/峰峰值）
        nz = ttk.Frame(tab)
        nz.pack(fill=tk.X, pady=2)
        ttk.Label(nz, text="噪声:").pack(side=tk.LEFT)
        self.noise_labels = {}
        for ch, (name, col) in enumerate((("探头1", "#d00"), ("探头2", "#0a0"),
                                          ("探头3", "#00c"))):
            lbl = ttk.Label(nz, text=f"{name}: 均值- σ- 峰峰-", foreground=col)
            lbl.pack(side=tk.LEFT, padx=10)
            self.noise_labels[ch] = lbl

        # 噪声趋势曲线（σ）
        self.trend_canvas = tk.Canvas(tab, bg="white", height=70)
        self.trend_canvas.pack(fill=tk.X)
        ttk.Label(tab, text="σ 趋势 (上:波形 下:噪声标准差)", foreground="gray").pack(anchor=tk.W)

        # CSV 保存策略
        row = ttk.Frame(tab)
        row.pack(fill=tk.X, pady=2)
        self.csv_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(row, text="保存 CSV", variable=self.csv_var).pack(side=tk.LEFT)
        ttk.Label(row, text="策略:").pack(side=tk.LEFT, padx=(8, 0))
        self.acq_mode_var = tk.StringVar(value="间隔统计(avg/min/max)")
        ttk.Combobox(row, textvariable=self.acq_mode_var,
                     values=("原始全量", "每帧首点(12.5Hz)", "间隔统计(avg/min/max)"),
                     state="readonly", width=20).pack(side=tk.LEFT)
        ttk.Label(row, text="间隔:").pack(side=tk.LEFT, padx=(8, 0))
        self.acq_int_var = tk.StringVar(value="10s")
        ttk.Combobox(row, textvariable=self.acq_int_var, values=("1s", "10s", "60s"),
                     state="readonly", width=5).pack(side=tk.LEFT)
        self.csv_path_var = tk.StringVar()
        ttk.Entry(row, textvariable=self.csv_path_var).pack(side=tk.LEFT, fill=tk.X,
                                                            expand=True, padx=4)
        ttk.Button(row, text="选择路径", command=self._acq_choose_csv).pack(side=tk.LEFT)

        # 日志
        self.acq_log = tk.Text(tab, height=7, state=tk.DISABLED, font=("Consolas", 9))
        self.acq_log.pack(fill=tk.BOTH, expand=True)

    def _acq(self, msg):
        self.acq_log.configure(state=tk.NORMAL)
        self.acq_log.insert(tk.END, msg + "\n")
        self.acq_log.see(tk.END)
        self.acq_log.configure(state=tk.DISABLED)

    def _acq_choose_csv(self):
        p = filedialog.asksaveasfilename(title="保存 CSV", defaultextension=".csv",
                                         filetypes=[("CSV", "*.csv")])
        if p:
            self.csv_path_var.set(p)

    def _acq_read_version(self):
        if self.worker and self.worker.is_alive():
            return
        ip = self.acq_ip_var.get().strip()
        self.worker = threading.Thread(target=self._acq_version_thread, args=(ip,), daemon=True)
        self.worker.start()

    def _acq_version_thread(self, ip):
        try:
            text = read_version_text(ip)
        except Exception as e:
            self.root.after(0, lambda e=e: self._acq(f"[FAIL] 读版本号失败: {e}"))
            return
        self.root.after(0, lambda: self._acq(f"[版本] {text.strip()}"))

    # ---- 采集线程 ----
    def _acq_start(self):
        if self.acq_running:
            return
        ip = self.acq_ip_var.get().strip()
        self._acq(f"[*] 连接 {ip} 并请求开始采集 ...")

        # CSV 打开 + 模式解析
        csv_file = None
        if self.csv_var.get():
            path = self.csv_path_var.get().strip()
            if not path:
                self._acq("[!] 已勾选保存 CSV 但未选择路径，先点「选择路径」")
                return
            try:
                csv_file = open(path, "w", newline="")
            except OSError as e:
                self._acq(f"[FAIL] 无法创建 CSV: {e}")
                return
        mode_text = self.acq_mode_var.get()
        if "原始" in mode_text:
            mode = "raw"
        elif "首点" in mode_text:
            mode = "first"
        else:
            mode = "stats"
        try:
            interval = int(self.acq_int_var.get().replace("s", ""))
        except ValueError:
            interval = 10

        self.acq_csv = csv_file
        self.acq_csv_path = self.csv_path_var.get().strip() if csv_file else None
        self.acq_mode = mode
        self.acq_interval = interval
        self.acq_buf = {0: [], 1: [], 2: []}
        self.acq_last_pkg = None
        self.acq_lost = 0
        self.acq_bad = 0
        self.acq_noise = {0: None, 1: None, 2: None}
        self.acq_trend = {0: [], 1: [], 2: []}
        self.acq_auto_vmin = None
        self.acq_auto_vmax = None
        self._acq(f"[*] CSV 模式: {mode} 间隔: {interval}s" if mode == "stats"
                  else f"[*] CSV 模式: {mode}")

        self.acq_btn_start.configure(state=tk.DISABLED)
        self.acq_btn_stop.configure(state=tk.NORMAL)
        self.worker = threading.Thread(target=self._acq_worker, args=(ip,), daemon=True)
        self.worker.start()

    def _acq_worker(self, ip):
        import csv as _csv
        writer = None
        if self.acq_csv:
            hdr = ["timestamp", "pkg"]
            if self.acq_mode == "raw":
                for ch in ("ch1", "ch2", "ch3"):
                    hdr += [f"{ch}[{i}]" for i in range(80)]
            elif self.acq_mode == "first":
                hdr += ["ch1", "ch2", "ch3"]
            else:  # stats
                hdr = ["timestamp"]
                for ch in ("ch1", "ch2", "ch3"):
                    hdr += [f"{ch}_avg", f"{ch}_min", f"{ch}_max"]
            writer = _csv.writer(self.acq_csv)
            writer.writerow(hdr)

        # stats 模式聚合状态
        agg = None
        agg_t0 = time.time()

        def flush_stats():
            nonlocal agg, agg_t0
            if agg and writer:
                dt = datetime.fromtimestamp(agg_t0).strftime("%Y-%m-%d %H:%M:%S")
                row = [dt]
                for c in range(3):
                    if agg[c][3] > 0:
                        row += [round(agg[c][0] / agg[c][3], 3), agg[c][1], agg[c][2]]
                    else:
                        row += [0.0, agg[c][1], agg[c][2]]
                writer.writerow(row)
            agg = {c: [0, 1 << 30, -1, 0] for c in range(3)}
            agg_t0 = time.time()

        if self.acq_mode == "stats":
            agg = {c: [0, 1 << 30, -1, 0] for c in range(3)}

        sock = None
        try:
            sock = socket.create_connection((ip, 1030), timeout=5)
            sock.settimeout(1.0)
            sock.sendall(bytes([0x02, 0x01, 0x05, 0x00, 0x00, 0x00, 0xDD, 0xAA]))
            ack = sock.recv(8)
            if len(ack) != 8 or ack[2] != 0x05:
                self.root.after(0, lambda: self._acq(f"[FAIL] 开始采集 ACK 异常: {ack.hex()}"))
                return
        except Exception as e:
            self.root.after(0, lambda e=e: self._acq(f"[FAIL] 连接失败: {e}"))
            return
        self.acq_sock = sock
        self.acq_running = True
        self.root.after(0, lambda: self._acq("[OK] 采集已开始，接收数据中 ..."))

        buf = b""
        while self.acq_running:
            try:
                data = sock.recv(4096)
                if not data:
                    break
            except socket.timeout:
                continue
            except OSError:
                break
            buf += data
            while len(buf) >= STREAM_FRAME_LEN:
                r = parse_stream_frame(buf[:STREAM_FRAME_LEN])
                if r is None:
                    buf = buf[1:]          # 滑窗重对齐
                    self.acq_bad += 1
                    continue
                pkg, c1, c2, c3 = r
                buf = buf[STREAM_FRAME_LEN:]

                # 丢帧检测（uint16 回绕安全）
                if self.acq_last_pkg is not None:
                    diff = (pkg - self.acq_last_pkg - 1) & 0xFFFF
                    if diff:
                        self.acq_lost += diff
                self.acq_last_pkg = pkg

                # 更新曲线缓冲
                for ch, vals in ((0, c1), (1, c2), (2, c3)):
                    self.acq_buf[ch].extend(vals)
                    if len(self.acq_buf[ch]) > self.acq_max_pts * 2:
                        del self.acq_buf[ch][:len(self.acq_buf[ch]) - self.acq_max_pts]

                # 噪声统计 + 趋势
                stats = [calc_channel_stats(v) for v in (c1, c2, c3)]
                for ch in range(3):
                    self.acq_noise[ch] = stats[ch]
                    self.acq_trend[ch].append(stats[ch][1])     # σ
                    if len(self.acq_trend[ch]) > 200:
                        del self.acq_trend[ch][:-200]

                # CSV 写入（按模式）
                if writer:
                    if self.acq_mode == "raw":
                        writer.writerow([round(time.time(), 3), pkg]
                                        + list(c1) + list(c2) + list(c3))
                    elif self.acq_mode == "first":
                        writer.writerow([round(time.time(), 3), pkg,
                                         c1[0], c2[0], c3[0]])
                    else:
                        for c, vals in enumerate((c1, c2, c3)):
                            a = agg[c]
                            a[0] += sum(vals)
                            a[1] = min(a[1], min(vals))
                            a[2] = max(a[2], max(vals))
                            a[3] += len(vals)
                        if time.time() - agg_t0 >= self.acq_interval:
                            flush_stats()

                # UI 刷新（每帧）
                self.root.after(0, self._acq_refresh)

        # 结束
        if writer and self.acq_mode == "stats":
            flush_stats()
        self.acq_running = False
        try:
            sock.close()
        except Exception:
            pass
        if self.acq_csv:
            try:
                self.acq_csv.close()
            except Exception:
                pass
        self.acq_csv = None
        self.root.after(0, lambda: self._acq(
            f"[*] 采集结束: 末包={self.acq_last_pkg}, 丢帧={self.acq_lost}, 坏帧={self.acq_bad}"))
        self.root.after(0, self._acq_reset_ui)

    def _acq_refresh(self):
        self._acq_redraw()
        self._acq_redraw_trend()
        # 噪声统计标签
        for ch, lbl in self.noise_labels.items():
            st = self.acq_noise[ch]
            txt = f"探头{ch + 1}: 均值- σ- 峰峰-" if st is None else (
                f"探头{ch + 1}: 均值{st[0]:.1f} σ{st[1]:.2f} 峰峰{st[2]:.0f}")
            lbl.config(text=txt)
        # 状态栏
        self.acq_status.set(
            f"包号 {self.acq_last_pkg if self.acq_last_pkg is not None else '-'} | "
            f"丢帧 {self.acq_lost} | 坏帧 {self.acq_bad}"
            + (f" | 已存 {self.acq_csv_path}" if self.acq_csv_path else ""))

    # ---- 波形 Canvas（坐标轴 + 网格 + 比例）----
    def _acq_redraw(self):
        W = self.canvas.winfo_width()
        H = self.canvas.winfo_height()
        if W < 20 or H < 20:
            return
        self.canvas.delete("all")
        colors = ("#d00", "#0a0", "#00c")
        ml, mt, mr, mb = 46, 6, 8, 20          # 坐标边距
        pw, ph = W - ml - mr, H - mt - mb

        # 纵轴范围
        if self.acq_yscale.get() == "固定":
            try:
                vmin = float(self.acq_ymin_var.get())
                vmax = float(self.acq_ymax_var.get())
            except ValueError:
                vmin, vmax = 0.0, 65535.0
            if vmax <= vmin:
                vmax = vmin + 1
        else:                                   # 自动
            allv = [v for ch in range(3) for v in self.acq_buf[ch]]
            if not allv:
                return
            span = max(allv) - min(allv)
            pad = max(span * 0.05, 10.0)
            vmin, vmax = min(allv) - pad, max(allv) + pad
            if self.acq_auto_vmin is not None:  # 轻微平滑防跳动
                vmin = 0.5 * vmin + 0.5 * self.acq_auto_vmin
                vmax = 0.5 * vmax + 0.5 * self.acq_auto_vmax
            self.acq_auto_vmin, self.acq_auto_vmax = vmin, vmax

        def mx(i, n): return ml + (i / (n - 1)) * pw if n > 1 else ml
        def my(v): return mt + (vmax - v) / (vmax - vmin) * ph

        # Y 轴刻度 + 网格
        for k in range(5):
            v = vmin + (vmax - vmin) * k / 4
            y = my(v)
            self.canvas.create_line(ml, y, ml + pw, y, fill="#e0e0e0", dash=(2, 2))
            self.canvas.create_text(ml - 4, y, text=f"{int(v)}", anchor=tk.E,
                                    font=("Consolas", 7))
        # X 轴刻度（点序号 / 秒）
        for k in range(5):
            i = self.acq_max_pts * k / 4
            x = ml + (i / self.acq_max_pts) * pw
            self.canvas.create_line(x, mt + ph, x, mt + ph + 4, fill="#999")
            self.canvas.create_text(x, mt + ph + 6, text=f"{-self.acq_max_pts + int(i)}点",
                                    anchor=tk.N, font=("Consolas", 6))

        # 三通道曲线
        for ch in range(3):
            data = self.acq_buf[ch][-self.acq_max_pts:]
            n = len(data)
            if n < 2:
                continue
            coords = []
            for i, v in enumerate(data):
                coords.extend((mx(i, n), my(v)))
            self.canvas.create_line(*coords, fill=colors[ch], width=1)

    # ---- σ 趋势 Canvas ----
    def _acq_redraw_trend(self):
        W = self.trend_canvas.winfo_width()
        H = self.trend_canvas.winfo_height()
        if W < 20 or H < 20:
            return
        self.trend_canvas.delete("all")
        colors = ("#d00", "#0a0", "#00c")
        tmax = 200
        vm = 0.0
        for ch in range(3):
            if self.acq_trend[ch]:
                vm = max(vm, max(self.acq_trend[ch]))
        vmax = vm * 1.1 if vm > 0 else 1.0
        for ch in range(3):
            data = self.acq_trend[ch]
            n = len(data)
            if n < 2:
                continue
            coords = []
            for i, v in enumerate(data):
                x = (i / (tmax - 1)) * W
                y = H - 3 - (v / vmax) * (H - 6)
                coords.extend((x, y))
            self.trend_canvas.create_line(*coords, fill=colors[ch], width=1)
        self.trend_canvas.create_text(3, 2, text=f"max σ {vm:.1f}", anchor=tk.NW,
                                      font=("Consolas", 7), fill="#666")

    def _acq_stop(self):
        if not self.acq_running and self.acq_sock is None:
            return
        self.acq_running = False
        ip = self.acq_ip_var.get().strip()
        try:
            stop_stream(ip)          # 发 0x06 让设备停 TIM7
        except Exception:
            pass
        try:
            if self.acq_sock:
                self.acq_sock.close()
        except Exception:
            pass
        self.acq_sock = None
        self._acq_reset_ui()

    def _acq_reset_ui(self):
        self.acq_btn_start.configure(state=tk.NORMAL)
        self.acq_btn_stop.configure(state=tk.DISABLED)


def main():
    root = tk.Tk()
    UpgradeGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
