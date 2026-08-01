#!/usr/bin/env python3
"""
FlashExplorer — STM32WB1M Flash Memory GUI
Communicates with TX_Beacon firmware via UART (frd / ferase / fwrite commands).

Requirements:  pip install pyserial
Run:           python flash_explorer.py
"""
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import threading
import queue
import re
import struct
import time
import json
import os

try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False

# ══════════════════════════════════════════════════════════════════════════════
#  Flash layout
# ══════════════════════════════════════════════════════════════════════════════
FLASH_BASE = 0x08000000
PAGE_SIZE  = 2048        # STM32WB1M: 2KB pages
NUM_PAGES  = 160         # 320KB / 2KB = 160 pages (0-159)
BAUD       = 115200

GRID_COLS  = 16          # 16 cols × 10 rows = 160 pages
GRID_ROWS  = 10
CELL_W, CELL_H, CELL_GAP = 44, 34, 3
MAP_PX, MAP_PY = 18, 18
MAP_W = MAP_PX * 2 + GRID_COLS * (CELL_W + CELL_GAP)
MAP_H = MAP_PY * 2 + GRID_ROWS * (CELL_H + CELL_GAP)

# ══════════════════════════════════════════════════════════════════════════════
#  Preferences  (last COM port, etc.)
# ══════════════════════════════════════════════════════════════════════════════
_PREF_FILE = os.path.join(os.path.expanduser("~"), ".flash_explorer_prefs.json")

def _load_prefs() -> dict:
    try:
        with open(_PREF_FILE, "r") as f:
            return json.load(f)
    except Exception:
        return {}

def _save_prefs(data: dict):
    try:
        with open(_PREF_FILE, "w") as f:
            json.dump(data, f)
    except Exception:
        pass


def page_zone(p):
    """Returns (label, base_color, warn_level 0/1/2)."""
    if p <= 17:  return "App code",   "#B0BEC5", 2
    if p == 60:  return "Config",     "#FFF9C4", 1
    if p <= 139: return "CPU1 free",  "#DCEDC8", 0
    return               "CPU2/FUS",  "#FFCCBC", 2


STATUS_FILL = {
    "unknown": None,
    "erased":  "#81C784",
    "data":    "#64B5F6",
    "error":   "#EF9A9A",
}

WARN_MSG = {
    2: (
        "⚠  НЕБЕЗПЕЧНА ЗОНА",
        "Ця сторінка належить до захищеної зони:\n"
        "  • Сторінки 0–17   — код програми (вектори + прошивка)\n"
        "  • Сторінки 140–159 — область CPU2 / FUS\n\n"
        "Стирання або запис можуть зробити пристрій нестартовним!\n\n"
        "Продовжити?"
    ),
    1: (
        "⚠  Конфігурація",
        "Сторінка 60 (0x0801E000) містить збережену конфігурацію TX_Beacon.\n"
        "Стирання або запис замінять поточні налаштування.\n\n"
        "Продовжити?"
    ),
}


def cell_xy(page):
    c = page % GRID_COLS
    r = page // GRID_COLS
    return MAP_PX + c * (CELL_W + CELL_GAP), MAP_PY + r * (CELL_H + CELL_GAP)


def words_to_bytes(words):
    return b"".join(struct.pack("<I", w) for w in words)


def hex_dump_lines(data, base_offset=0):
    out = []
    for row in range(0, len(data), 16):
        chunk = data[row:row + 16]
        addr  = f"+{base_offset + row:04X}"
        hex_s = " ".join(f"{b:02X}" for b in chunk)
        if len(chunk) < 16:
            hex_s += "   " * (16 - len(chunk))
        parts = hex_s.split()
        if len(parts) >= 9:
            hex_s = " ".join(parts[:8]) + "  " + " ".join(parts[8:])
        asc = "".join(chr(b) if 32 <= b < 127 else "·" for b in chunk)
        out.append((addr, hex_s, asc, list(chunk)))
    return out


def _mini_color(val: int) -> str:
    """
    Pseudo-color for one 32-bit word in the mini map.
      0xFFFFFFFF  → dark gray   (стерто — all bits 1)
      0x00000000  → dark navy   (нулі)
      інше        → RGB з нижніх 3 байт, приглушено до 70-210
                    (кожна байтова зона chip дає унікальний колір)
    """
    if val == 0xFFFFFFFF:
        return "#3D5060"   # gray — erased
    if val == 0x00000000:
        return "#0D2B6B"   # navy — zeroed
    r = 70 + ((val >> 16) & 0xFF) * 140 // 255
    g = 70 + ((val >>  8) & 0xFF) * 140 // 255
    b = 70 + ( val        & 0xFF) * 140 // 255
    return f"#{r:02X}{g:02X}{b:02X}"


# ══════════════════════════════════════════════════════════════════════════════
#  Serial worker thread
# ══════════════════════════════════════════════════════════════════════════════
class SerialWorker(threading.Thread):
    def __init__(self, port, baud, rx_q, evt_q):
        super().__init__(daemon=True)
        self.port  = port
        self.baud  = baud
        self.rx_q  = rx_q
        self.evt_q = evt_q
        self.tx_q  = queue.Queue()
        self._stop = threading.Event()
        self.ser   = None

    def send(self, text):
        self.tx_q.put(text)

    def stop(self):
        self._stop.set()

    def run(self):
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.05)
            self.evt_q.put("connected")
            buf = b""
            while not self._stop.is_set():
                while not self.tx_q.empty():
                    try:
                        self.ser.write((self.tx_q.get_nowait() + "\r\n").encode())
                    except queue.Empty:
                        break
                raw = self.ser.read(512)
                if raw:
                    buf += raw
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        self.rx_q.put(line.decode("utf-8", errors="replace").rstrip("\r"))
        except Exception as e:
            self.evt_q.put(f"error:{e}")
        finally:
            try:
                if self.ser and self.ser.is_open:
                    self.ser.close()
            except Exception:
                pass
            self.evt_q.put("disconnected")


# ══════════════════════════════════════════════════════════════════════════════
#  Main application
# ══════════════════════════════════════════════════════════════════════════════
class FlashExplorer:
    def __init__(self, root):
        self.root = root
        root.title("FlashExplorer — STM32WB1M  (TX_Beacon)")
        root.configure(bg="#ECEFF1")
        root.minsize(1320, 780)

        self.worker: SerialWorker | None = None
        self.rx_q   = queue.Queue()
        self.evt_q  = queue.Queue()

        self.page_status = ["unknown"] * NUM_PAGES
        self.page_words  = [None]      * NUM_PAGES

        self.selected_page = tk.IntVar(value=60)
        self._sel_set      = {60}        # set of selected page numbers
        self._drag_anchor  = 60          # anchor page for drag operations
        self._hover_page   = None

        self.cmd_busy      = False
        self.pending_page  = None
        self.pending_cmd   = None
        self.frd_buf       = []
        self._on_done_cb   = None
        self._range_queue  = []

        self._build_ui()
        root.bind("<Escape>", self._on_map_rclick)
        self._poll()

    # ── Selection helpers ─────────────────────────────────────────────────────
    def _sel_lo(self) -> int:
        return min(self._sel_set) if self._sel_set else self.selected_page.get()

    def _sel_hi(self) -> int:
        return max(self._sel_set) if self._sel_set else self.selected_page.get()

    def _sel_count(self) -> int:
        return len(self._sel_set)

    def _update_sel_info(self):
        if not self._sel_set:
            self.sel_info_var.set("—")
            return
        s   = sorted(self._sel_set)
        cnt = len(s)
        lo, hi = s[0], s[-1]
        icons  = ["✓", "⚠", "⛔"]
        if cnt == 1:
            addr  = FLASH_BASE + lo * PAGE_SIZE
            label, _, warn = page_zone(lo)
            self.sel_info_var.set(
                f"Стор {lo}  |  0x{addr:08X}  |  {icons[warn]} {label}")
        else:
            kb   = cnt * PAGE_SIZE // 1024
            cont = (s == list(range(lo, hi + 1)))
            if cont:
                a0 = FLASH_BASE + lo * PAGE_SIZE
                a1 = FLASH_BASE + hi * PAGE_SIZE + PAGE_SIZE - 1
                self.sel_info_var.set(
                    f"Стор {lo}–{hi}  ({cnt} × 2KB = {kb} KB)"
                    f"   0x{a0:08X}–0x{a1:08X}")
            else:
                self.sel_info_var.set(
                    f"{cnt} стор.  ({kb} KB)  [несуміжні: {lo}…{hi}]")

    # ── UI construction ───────────────────────────────────────────────────────
    def _build_ui(self):
        root = self.root

        # ── Top bar ───────────────────────────────────────────────────────────
        bar = tk.Frame(root, bg="#37474F", pady=6, padx=10)
        bar.pack(fill=tk.X)

        tk.Label(bar, text="STM32WB FlashExplorer", font=("Segoe UI", 12, "bold"),
                 bg="#37474F", fg="white").pack(side=tk.LEFT, padx=(0, 20))

        tk.Label(bar, text="Port:", bg="#37474F", fg="#B0BEC5").pack(side=tk.LEFT)
        self.port_cb = ttk.Combobox(bar, width=10, state="readonly")
        self.port_cb.pack(side=tk.LEFT, padx=(2, 8))
        self._refresh_ports()

        ttk.Button(bar, text="↺", width=2, command=self._refresh_ports).pack(side=tk.LEFT)

        tk.Label(bar, text="Baud:", bg="#37474F", fg="#B0BEC5").pack(side=tk.LEFT, padx=(10, 2))
        self.baud_cb = ttk.Combobox(bar, width=8,
                                    values=["115200", "9600", "57600", "230400"],
                                    state="readonly")
        self.baud_cb.set(str(BAUD))
        self.baud_cb.pack(side=tk.LEFT, padx=(0, 10))

        self.conn_btn = ttk.Button(bar, text="▶ Connect", command=self._toggle_connect)
        self.conn_btn.pack(side=tk.LEFT)

        self.conn_led = tk.Canvas(bar, width=14, height=14, bg="#37474F",
                                  highlightthickness=0)
        self.conn_led.pack(side=tk.LEFT, padx=6)
        self._led_oval = self.conn_led.create_oval(2, 2, 12, 12, fill="#EF5350", outline="")

        self.conn_lbl = tk.Label(bar, text="Disconnected", bg="#37474F",
                                 fg="#EF5350", font=("Segoe UI", 9))
        self.conn_lbl.pack(side=tk.LEFT)

        # ── Main area ─────────────────────────────────────────────────────────
        main = tk.Frame(root, bg="#ECEFF1")
        main.pack(fill=tk.BOTH, expand=True, padx=8, pady=6)

        left = tk.Frame(main, bg="#ECEFF1", width=240)
        left.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 6))
        left.pack_propagate(False)
        self._build_ops_panel(left)

        center = tk.Frame(main, bg="#ECEFF1")
        center.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self._build_map_panel(center)

        right = tk.Frame(main, bg="#ECEFF1", width=560)
        right.pack(side=tk.LEFT, fill=tk.Y, padx=(6, 0))
        right.pack_propagate(False)
        self._build_hex_panel(right)

        # ── UART log (bottom) ─────────────────────────────────────────────────
        self._build_log(root)

        # ── Status bar ───────────────────────────────────────────────────────
        self.status_var = tk.StringVar(value="Готово. Підключіть пристрій.")
        sb = tk.Label(root, textvariable=self.status_var, anchor=tk.W,
                      font=("Segoe UI", 8), bg="#CFD8DC", padx=8, pady=2)
        sb.pack(fill=tk.X)

    # ── Ops panel ─────────────────────────────────────────────────────────────
    def _build_ops_panel(self, parent):
        # Scrollable canvas wrapper — left panel scrolls if content > window height
        _cv = tk.Canvas(parent, bg="#ECEFF1", highlightthickness=0)
        _sb = ttk.Scrollbar(parent, orient="vertical", command=_cv.yview)
        _cv.configure(yscrollcommand=_sb.set)
        _sb.pack(side=tk.RIGHT, fill=tk.Y)
        _cv.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        inner = tk.Frame(_cv, bg="#ECEFF1")
        _win  = _cv.create_window((0, 0), window=inner, anchor="nw")
        inner.bind("<Configure>",
                   lambda e: _cv.configure(scrollregion=_cv.bbox("all")))
        _cv.bind("<Configure>",
                 lambda e: _cv.itemconfig(_win, width=e.width))

        def _bind_wheel(widget):
            widget.bind("<MouseWheel>",
                        lambda e: _cv.yview_scroll(int(-1*(e.delta/120)), "units"))
            for child in widget.winfo_children():
                _bind_wheel(child)

        inner.bind("<Configure>", lambda e: (
            _cv.configure(scrollregion=_cv.bbox("all")),
            _bind_wheel(inner)
        ))

        def section(text):
            f = tk.LabelFrame(inner, text=f" {text} ", bg="#ECEFF1",
                              font=("Segoe UI", 8, "bold"), padx=6, pady=4)
            f.pack(fill=tk.X, pady=(0, 6))
            return f

        # ── Selection info ─────────────────────────────────────────────────
        si = section("Виділення")
        self.sel_info_var = tk.StringVar(value="—")
        tk.Label(si, textvariable=self.sel_info_var, bg="#ECEFF1",
                 font=("Segoe UI", 8), fg="#1565C0", wraplength=220,
                 justify=tk.LEFT).pack(anchor=tk.W)
        ttk.Button(si, text="✕ Зняти виділення (ESC)",
                   command=self._on_map_rclick).pack(fill=tk.X, pady=(4, 0))

        # ── Page selector ─────────────────────────────────────────────────
        ps = section("Вибір сторінки")
        tk.Label(ps, text="Сторінка (0–159):", bg="#ECEFF1",
                 font=("Segoe UI", 8)).grid(row=0, column=0, sticky=tk.W)
        self.page_spin = ttk.Spinbox(ps, from_=0, to=159,
                                     textvariable=self.selected_page,
                                     width=6, command=self._on_spin_page)
        self.page_spin.grid(row=0, column=1, padx=4)
        self.page_spin.bind("<Return>", lambda e: self._on_spin_page())

        self.page_addr_lbl = tk.Label(ps, text="0x0801E000", bg="#ECEFF1",
                                      font=("Consolas", 8), fg="#607D8B")
        self.page_addr_lbl.grid(row=1, column=0, columnspan=2, sticky=tk.W, pady=(2, 0))
        self.zone_lbl = tk.Label(ps, text="Config ●", bg="#ECEFF1",
                                 font=("Segoe UI", 8, "bold"), fg="#F57F17")
        self.zone_lbl.grid(row=2, column=0, columnspan=2, sticky=tk.W)

        # ── Read ──────────────────────────────────────────────────────────
        rs = section("Читання")
        ttk.Button(rs, text="📖 Читати сторінку",
                   command=self._cmd_read).pack(fill=tk.X, pady=1)
        ttk.Button(rs, text="📖 Читати виділений діапазон",
                   command=self._cmd_read_sel).pack(fill=tk.X, pady=1)

        rf = tk.Frame(rs, bg="#ECEFF1")
        rf.pack(fill=tk.X, pady=(4, 0))
        tk.Label(rf, text="Від:", bg="#ECEFF1",
                 font=("Segoe UI", 8)).grid(row=0, column=0, sticky=tk.W)
        self.range_from = ttk.Spinbox(rf, from_=0, to=159, width=4)
        self.range_from.set("18")
        self.range_from.grid(row=0, column=1, padx=2)
        tk.Label(rf, text="до:", bg="#ECEFF1",
                 font=("Segoe UI", 8)).grid(row=0, column=2)
        self.range_to = ttk.Spinbox(rf, from_=0, to=159, width=4)
        self.range_to.set("70")
        self.range_to.grid(row=0, column=3, padx=2)
        ttk.Button(rs, text="📖 Читати діапазон (spinbox)",
                   command=self._cmd_read_range).pack(fill=tk.X, pady=1)
        ttk.Button(rs, text="💾 Зберегти виділені сторінки → файл",
                   command=self._cmd_save_selection).pack(fill=tk.X, pady=(4, 1))

        # ── Erase ─────────────────────────────────────────────────────────
        es = section("Стирання")
        ttk.Button(es, text="🗑 Стерти сторінку", command=self._cmd_erase,
                   style="Danger.TButton").pack(fill=tk.X, pady=1)

        # ── Write ─────────────────────────────────────────────────────────
        ws = section("Запис (тест)")
        tk.Label(ws, text="Hex-патерн (32 біт):", bg="#ECEFF1",
                 font=("Segoe UI", 8)).pack(anchor=tk.W)

        wf = tk.Frame(ws, bg="#ECEFF1")
        wf.pack(fill=tk.X)
        self.write_entry = ttk.Entry(wf, width=11, font=("Consolas", 10))
        self.write_entry.insert(0, "DEADBEEF")
        self.write_entry.pack(side=tk.LEFT)
        tk.Label(wf, text="×2", bg="#ECEFF1", fg="#90A4AE",
                 font=("Segoe UI", 8)).pack(side=tk.LEFT, padx=2)

        tk.Label(ws, text="Швидкі патерни:", bg="#ECEFF1",
                 font=("Segoe UI", 7), fg="#607D8B").pack(anchor=tk.W, pady=(4, 1))
        pats = tk.Frame(ws, bg="#ECEFF1")
        pats.pack(fill=tk.X)
        for pat in ["DEADBEEF", "CAFEBABE", "12345678", "AAAAAAAA", "55555555"]:
            ttk.Button(pats, text=pat[:8], width=8,
                       command=lambda p=pat: self.write_entry.delete(0, tk.END) or
                               self.write_entry.insert(0, p)
                       ).pack(side=tk.LEFT, padx=1, pady=1)

        ttk.Button(ws, text="✏ Записати в сторінку",
                   command=self._cmd_write).pack(fill=tk.X, pady=(4, 1))

        # ── Legend ────────────────────────────────────────────────────────
        leg = section("Легенда")
        items = [
            ("#B0BEC5", "App code (0–17) — небезп."),
            ("#FFF9C4", "Config (60, 0x0801E000)"),
            ("#DCEDC8", "CPU1 free (18–139)"),
            ("#FFCCBC", "CPU2/FUS (140–159) — небезп."),
            ("#81C784", "Стерта (всі 0xFF)"),
            ("#64B5F6", "Є дані"),
            ("#EF9A9A", "Помилка"),
        ]
        for color, label in items:
            row = tk.Frame(leg, bg="#ECEFF1")
            row.pack(anchor=tk.W, fill=tk.X, pady=1)
            c = tk.Canvas(row, width=16, height=16, bg="#ECEFF1", highlightthickness=0)
            c.create_rectangle(1, 1, 15, 15, fill=color, outline="#90A4AE")
            c.pack(side=tk.LEFT, padx=(0, 4))
            tk.Label(row, text=label, bg="#ECEFF1",
                     font=("Segoe UI", 7), fg="#37474F").pack(side=tk.LEFT)

        # ── Result indicator ──────────────────────────────────────────────
        ri = section("Результат")
        self.result_lbl = tk.Label(ri, text="—", font=("Segoe UI", 12, "bold"),
                                   bg="#ECEFF1", fg="#607D8B")
        self.result_lbl.pack()
        self.result_detail = tk.Label(ri, text="", font=("Segoe UI", 8),
                                      bg="#ECEFF1", fg="#607D8B", wraplength=210,
                                      justify=tk.LEFT)
        self.result_detail.pack(anchor=tk.W)

        style = ttk.Style()
        style.configure("Danger.TButton", foreground="darkred")

        self._update_page_info()
        self._update_sel_info()

    # ── Flash map ─────────────────────────────────────────────────────────────
    def _build_map_panel(self, parent):
        tk.Label(parent,
                 text="Flash Map — STM32WB1M  (320 KB / 160 сторінок × 2 KB)\n"
                      "ЛКМ — виділити  |  Тягнути — діапазон  |  ПКМ / ESC — зняти виділення",
                 bg="#ECEFF1", font=("Segoe UI", 8), fg="#37474F",
                 justify=tk.LEFT).pack(anchor=tk.W)

        hdr = tk.Frame(parent, bg="#ECEFF1")
        hdr.pack(fill=tk.X)
        tk.Label(hdr, text=" Рядок\\Стор→", bg="#ECEFF1",
                 font=("Segoe UI", 7), fg="#90A4AE", width=9).pack(side=tk.LEFT)
        for c in range(GRID_COLS):
            tk.Label(hdr, text=f"+{c}", width=5, bg="#ECEFF1",
                     font=("Segoe UI", 7), fg="#90A4AE").pack(side=tk.LEFT)

        frame = tk.Frame(parent, bg="#ECEFF1")
        frame.pack(fill=tk.BOTH, expand=True)

        self.map_canvas = tk.Canvas(frame, width=MAP_W, height=MAP_H,
                                    bg="#FAFAFA", highlightthickness=1,
                                    highlightbackground="#B0BEC5")
        self.map_canvas.pack(padx=2, pady=4)

        self.map_canvas.bind("<ButtonPress-1>",          self._on_map_press)
        self.map_canvas.bind("<B1-Motion>",              self._on_map_drag)
        self.map_canvas.bind("<ButtonRelease-1>",        self._on_map_release)
        self.map_canvas.bind("<Button-3>",               self._on_map_rclick)
        self.map_canvas.bind("<Control-ButtonPress-1>",  self._on_map_ctrl_click)
        self.map_canvas.bind("<Motion>",                 self._on_map_hover)
        self.map_canvas.bind("<Leave>",                  self._on_map_leave)

        self.cell_rects = []
        self.cell_texts = []

        for p in range(NUM_PAGES):
            x, y = cell_xy(p)
            _, base, _ = page_zone(p)
            r = self.map_canvas.create_rectangle(
                x, y, x + CELL_W, y + CELL_H,
                fill=base, outline="#78909C", width=1)
            t = self.map_canvas.create_text(
                x + CELL_W // 2, y + CELL_H // 2,
                text=str(p), font=("Consolas", 8, "bold"), fill="#37474F")
            self.cell_rects.append(r)
            self.cell_texts.append(t)

        self.map_tip = tk.Label(frame, text="", bg="#FFF9C4", relief=tk.SOLID,
                                font=("Segoe UI", 8), padx=4, pady=2,
                                borderwidth=1)
        self._draw_selected()

    # ── Hex dump panel ────────────────────────────────────────────────────────
    def _build_hex_panel(self, parent):
        tk.Label(parent, text="Hex Dump", bg="#ECEFF1",
                 font=("Segoe UI", 9, "bold"), fg="#37474F").pack(anchor=tk.W)

        self.hex_page_lbl = tk.Label(parent, text="— виберіть сторінку —",
                                     bg="#ECEFF1", font=("Segoe UI", 8), fg="#607D8B")
        self.hex_page_lbl.pack(anchor=tk.W)

        hex_frame = tk.Frame(parent, bg="#ECEFF1")
        hex_frame.pack(fill=tk.BOTH, expand=True)

        self.hex_txt = tk.Text(hex_frame, width=78, font=("Consolas", 9),
                               bg="#0F1923", fg="#CDD6E0",
                               insertbackground="white",
                               state=tk.DISABLED, relief=tk.FLAT, padx=4, pady=4,
                               wrap=tk.NONE)
        hs = ttk.Scrollbar(hex_frame, orient=tk.HORIZONTAL, command=self.hex_txt.xview)
        vs = ttk.Scrollbar(hex_frame, command=self.hex_txt.yview)
        self.hex_txt.configure(xscrollcommand=hs.set, yscrollcommand=vs.set)
        vs.pack(side=tk.RIGHT, fill=tk.Y)
        self.hex_txt.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        hs.pack(side=tk.TOP, fill=tk.X)

        self.hex_txt.tag_config("addr",  foreground="#607D8B")
        self.hex_txt.tag_config("ff",    foreground="#455A64")   # dark — erased byte
        self.hex_txt.tag_config("zero",  foreground="#5C7AEA")   # blue — zero byte
        self.hex_txt.tag_config("data",  foreground="#80DEEA")   # cyan — real data
        self.hex_txt.tag_config("magic", foreground="#FFB74D")   # orange — magic bytes
        self.hex_txt.tag_config("asc",   foreground="#A5D6A7")   # green — ASCII
        self.hex_txt.tag_config("sep",   foreground="#263238")

        # Mini map label
        mini_lbl = tk.Label(parent,
                            text="Сторінка — блоки по 4 байти:"
                                 "  ▪ темний = 0xFF (стерто)  ▪ синій = 0x00"
                                 "  ▪ інший = RGB з даних (візуальний відбиток вмісту)",
                            bg="#ECEFF1", font=("Segoe UI", 7), fg="#607D8B",
                            wraplength=540, justify=tk.LEFT)
        mini_lbl.pack(anchor=tk.W, pady=(4, 0))

        mini_frame = tk.Frame(parent, bg="#ECEFF1")
        mini_frame.pack(fill=tk.X)
        self.mini_canvas = tk.Canvas(mini_frame, height=28, bg="#0F1923",
                                     highlightthickness=1,
                                     highlightbackground="#B0BEC5")
        self.mini_canvas.pack(fill=tk.X)
        self.mini_canvas.bind("<Configure>", self._draw_mini)
        self.mini_canvas.bind("<Motion>",    self._on_mini_hover)
        self.mini_canvas.bind("<Leave>",     self._on_mini_leave)

        self._mini_tip = tk.Label(mini_frame, text="", bg="#FFF9C4",
                                  relief=tk.SOLID, font=("Consolas", 8),
                                  padx=3, pady=1, borderwidth=1)

    # ── UART Log ──────────────────────────────────────────────────────────────
    def _build_log(self, root):
        log_outer = tk.LabelFrame(root, text=" UART Log ", bg="#ECEFF1",
                                  font=("Segoe UI", 8))
        log_outer.pack(fill=tk.X, padx=8, pady=(0, 4))

        self.log_txt = tk.Text(log_outer, height=7, font=("Consolas", 9),
                               bg="#0F1923", fg="#CDD6E0",
                               insertbackground="white",
                               state=tk.DISABLED, relief=tk.FLAT)
        log_sb = ttk.Scrollbar(log_outer, command=self.log_txt.yview)
        self.log_txt.configure(yscrollcommand=log_sb.set)
        self.log_txt.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        log_sb.pack(side=tk.LEFT, fill=tk.Y)

        self.log_txt.tag_config("tx",  foreground="#4DD0E1")   # cyan  — sent
        self.log_txt.tag_config("ok",  foreground="#81C784")   # green — ok
        self.log_txt.tag_config("err", foreground="#EF9A9A")   # red   — error
        self.log_txt.tag_config("rx",  foreground="#CDD6E0")   # light — received

        # CMD bar below log
        cmd_bar = tk.Frame(log_outer, bg="#ECEFF1")
        cmd_bar.pack(side=tk.BOTTOM, fill=tk.X, pady=(2, 0))

        tk.Label(cmd_bar, text="CMD:", bg="#ECEFF1", fg="#37474F",
                 font=("Segoe UI", 8)).pack(side=tk.LEFT, padx=(2, 2))
        self.uart_cmd_var = tk.StringVar()
        e = tk.Entry(cmd_bar, textvariable=self.uart_cmd_var, width=40,
                     bg="#1A2535", fg="#E2E8F0", insertbackground="white",
                     font=("Consolas", 9), relief=tk.GROOVE, borderwidth=1)
        e.pack(side=tk.LEFT, padx=2)
        e.bind("<Return>", lambda _: self._send_uart_cmd())
        ttk.Button(cmd_bar, text="Send",  command=self._send_uart_cmd,
                   width=6).pack(side=tk.LEFT, padx=2)
        ttk.Button(cmd_bar, text="🗑 Clear log", command=self._clear_log,
                   ).pack(side=tk.RIGHT, padx=4)

    # ── Flash map interaction ─────────────────────────────────────────────────
    def _page_at(self, ex, ey):
        for p in range(NUM_PAGES):
            x, y = cell_xy(p)
            if x <= ex <= x + CELL_W and y <= ey <= y + CELL_H:
                return p
        return None

    def _draw_cell(self, page):
        r    = self.cell_rects[page]
        _, base, _ = page_zone(page)
        st   = self.page_status[page]
        fill = STATUS_FILL.get(st) or base
        if page in self._sel_set:
            outline, width = "#E040FB", 3
        else:
            outline, width = "#78909C", 1
        self.map_canvas.itemconfig(r, fill=fill, outline=outline, width=width)

    def _draw_selected(self):
        for p in range(NUM_PAGES):
            self._draw_cell(p)

    def _on_map_press(self, event):
        if event.state & 0x4:            # Ctrl held — let ctrl-click handler run
            return
        p = self._page_at(event.x, event.y)
        if p is None:
            return
        self._drag_anchor = p
        self.selected_page.set(p)
        self._sel_set = {p}
        self._draw_selected()
        self._update_page_info()
        self._update_sel_info()
        self._show_hex(p)

    def _on_map_drag(self, event):
        p = self._page_at(event.x, event.y)
        if p is None:
            return
        lo = min(self._drag_anchor, p)
        hi = max(self._drag_anchor, p)
        if event.state & 0x4:            # Ctrl+drag — ADD range to existing set
            self._sel_set |= set(range(lo, hi + 1))
        else:                            # plain drag — REPLACE set with range
            self._sel_set = set(range(lo, hi + 1))
        self._draw_selected()
        self._update_sel_info()
        self.range_from.set(str(self._sel_lo()))
        self.range_to.set(str(self._sel_hi()))

    def _on_map_release(self, event):
        pass

    def _on_map_ctrl_click(self, event):
        """Ctrl+click: toggle single page in selection set."""
        p = self._page_at(event.x, event.y)
        if p is None:
            return
        self._drag_anchor = p
        if p in self._sel_set:
            if len(self._sel_set) > 1:   # keep at least one page selected
                self._sel_set.discard(p)
                # move primary to nearest remaining page
                self.selected_page.set(min(self._sel_set,
                                           key=lambda x: abs(x - p)))
        else:
            self._sel_set.add(p)
            self.selected_page.set(p)
            self._show_hex(p)
            self._update_page_info()
        self._draw_selected()
        self._update_sel_info()

    def _on_map_rclick(self, event=None):
        """RMB / ESC: collapse selection to single primary page."""
        p = self.selected_page.get()
        self._sel_set = {p}
        self._drag_anchor = p
        self._draw_selected()
        self._update_sel_info()

    def _on_map_hover(self, event):
        p = self._page_at(event.x, event.y)
        if p is None:
            self._on_map_leave(None)
            return
        if self._hover_page == p:
            return
        self._hover_page = p
        label, _, warn = page_zone(p)
        addr = FLASH_BASE + p * PAGE_SIZE
        st   = self.page_status[p]
        warn_ico = ["✓ безпечно", "⚠ конфіг", "⛔ небезпечно"][warn]
        tip = (f"Стор {p}   0x{addr:08X}\n"
               f"Зона: {label}   {warn_ico}\n"
               f"Стан: {st}")
        self.map_tip.config(text=tip)
        self.map_tip.place(x=event.x + 10, y=event.y - 10)

    def _on_map_leave(self, _):
        self._hover_page = None
        self.map_tip.place_forget()

    # ── Mini map ──────────────────────────────────────────────────────────────
    def _draw_mini(self, _event):
        if not hasattr(self, "mini_canvas"):
            return
        page  = self.selected_page.get()
        words = self.page_words[page]
        c     = self.mini_canvas
        c.delete("all")
        w_total = c.winfo_width() or 380
        if words is None:
            c.create_text(w_total // 2, 14, text="немає даних — натисніть «Читати»",
                          fill="#546E7A", font=("Segoe UI", 8))
            return
        n  = len(words)
        bw = max(2, (w_total - 4) // n)
        for i, val in enumerate(words):
            x0 = 2 + i * bw
            x1 = x0 + bw - 1
            c.create_rectangle(x0, 2, x1, 26, fill=_mini_color(val), outline="")
        # Tick every 8 words (= 32 bytes)
        for i in range(0, n, 8):
            x = 2 + i * bw
            c.create_line(x, 20, x, 27, fill="#546E7A")
            c.create_text(x + 2, 27, text=f"+{i*4:X}", anchor=tk.NW,
                          font=("Consolas", 6), fill="#78909C")

    def _on_mini_hover(self, event):
        page  = self.selected_page.get()
        words = self.page_words[page]
        if words is None:
            return
        w_total = self.mini_canvas.winfo_width() or 380
        n  = len(words)
        bw = max(2, (w_total - 4) // n)
        idx = (event.x - 2) // bw
        if 0 <= idx < n:
            val  = words[idx]
            addr = FLASH_BASE + page * PAGE_SIZE + idx * 4
            self._mini_tip.config(
                text=f"+{idx*4:04X}  [{idx}]  0x{val:08X}  ({val})")
            self._mini_tip.place(x=event.x, y=0)
        else:
            self._on_mini_leave(None)

    def _on_mini_leave(self, _):
        self._mini_tip.place_forget()

    # ── Hex dump display ──────────────────────────────────────────────────────
    def _show_hex(self, page):
        words = self.page_words[page]
        addr  = FLASH_BASE + page * PAGE_SIZE

        self.hex_page_lbl.config(
            text=f"Стор {page}   @ 0x{addr:08X}   {page_zone(page)[0]}"
        )

        self.hex_txt.config(state=tk.NORMAL)
        self.hex_txt.delete("1.0", tk.END)

        if words is None:
            self.hex_txt.insert(tk.END,
                "\n  Дані не зчитано.\n  Натисніть «Читати сторінку».\n",
                "addr")
            self.hex_txt.config(state=tk.DISABLED)
            self._draw_mini(None)
            return

        data  = words_to_bytes(words)
        lines = hex_dump_lines(data, base_offset=0)

        hdr = "Offset   00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  ASCII\n"
        self.hex_txt.insert(tk.END, hdr, "addr")
        self.hex_txt.insert(tk.END, "─" * 67 + "\n", "sep")

        for addr_str, hex_str, asc_str, byte_list in lines:
            self.hex_txt.insert(tk.END, f"{addr_str}:  ", "addr")
            for i, b in enumerate(byte_list):
                txt = f"{b:02X} "
                if i == 8:
                    self.hex_txt.insert(tk.END, " ")
                if b == 0xFF:
                    tag = "ff"
                elif b == 0x00:
                    tag = "zero"
                elif b in (0xBE, 0xAC, 0x01):
                    tag = "magic"
                else:
                    tag = "data"
                self.hex_txt.insert(tk.END, txt, tag)
            self.hex_txt.insert(tk.END, f" {asc_str}\n", "asc")

        self.hex_txt.config(state=tk.DISABLED)
        self._draw_mini(None)

    # ── Page info ─────────────────────────────────────────────────────────────
    def _update_page_info(self):
        p    = self.selected_page.get()
        addr = FLASH_BASE + p * PAGE_SIZE
        label, _, warn = page_zone(p)
        warn_colors = ["#2E7D32", "#F57F17", "#C62828"]
        warn_icons  = ["✓", "⚠", "⛔"]
        self.page_addr_lbl.config(text=f"0x{addr:08X}")
        self.zone_lbl.config(text=f"{warn_icons[warn]} {label}",
                             fg=warn_colors[warn])

    def _on_spin_page(self):
        try:
            p = int(self.page_spin.get())
            self.selected_page.set(max(0, min(159, p)))
        except ValueError:
            pass
        p = self.selected_page.get()
        self._sel_set     = {p}
        self._drag_anchor = p
        self._update_page_info()
        self._draw_selected()
        self._update_sel_info()
        self._show_hex(p)

    # ── Commands ──────────────────────────────────────────────────────────────
    def _check_connected(self):
        if not self.worker:
            messagebox.showwarning("Не підключено",
                                   "Спочатку підключіться до пристрою.")
            return False
        return True

    def _safety_check(self, page, operation="Операція"):
        _, _, warn = page_zone(page)
        if warn == 0:
            return True
        title, msg = WARN_MSG.get(warn,
            ("Увага", f"{operation} на сторінці {page}. Продовжити?"))
        return messagebox.askyesno(title, msg, icon="warning", default="no")

    def _cmd_read(self):
        if not self._check_connected() or self.cmd_busy:
            return
        p = self.selected_page.get()
        self._send_cmd("frd", p, f"frd {p}")

    def _cmd_read_sel(self):
        """Read all pages in the current selection set."""
        if not self._check_connected() or self.cmd_busy:
            return
        pages = sorted(self._sel_set)
        if len(pages) == 1:
            self._cmd_read()
            return
        self._range_queue = pages
        self._set_status(f"Читання {len(pages)} сторінок ({pages[0]}–{pages[-1]})…")
        self._read_next_in_range()

    def _cmd_read_range(self):
        if not self._check_connected() or self.cmd_busy:
            return
        try:
            fr = int(self.range_from.get())
            to = int(self.range_to.get())
        except ValueError:
            messagebox.showerror("Помилка", "Невірний діапазон.")
            return
        fr, to = max(0, min(159, fr)), max(0, min(159, to))
        if fr > to:
            fr, to = to, fr
        self._range_queue = list(range(fr, to + 1))
        self._set_status(f"Читання {len(self._range_queue)} сторінок ({fr}–{to})…")
        self._read_next_in_range()

    def _range_queue_cb(self):
        if not self._range_queue or self.cmd_busy:
            return
        p = self._range_queue.pop(0)
        self._send_cmd("frd", p, f"frd {p}", on_done=self._read_next_in_range)

    def _read_next_in_range(self):
        if hasattr(self, "_range_queue") and self._range_queue:
            self.root.after(120, self._range_queue_cb)

    def _cmd_erase(self):
        if not self._check_connected() or self.cmd_busy:
            return
        p = self.selected_page.get()
        if not self._safety_check(p, "Стирання"):
            return
        self._send_cmd("ferase", p, f"ferase {p}")

    def _cmd_write(self):
        if not self._check_connected() or self.cmd_busy:
            return
        p = self.selected_page.get()
        if not self._safety_check(p, "Запис"):
            return
        raw = self.write_entry.get().strip().upper()
        try:
            val = int(raw, 16)
            if not (0 <= val <= 0xFFFFFFFF):
                raise ValueError
        except ValueError:
            messagebox.showerror("Помилка", f"Невірний hex32: '{raw}'")
            return
        self._send_cmd("fwrite", p, f"fwrite {p} {raw}")

    def _cmd_save_selection(self):
        pages   = sorted(self._sel_set)
        lo, hi  = pages[0], pages[-1]
        missing = [p for p in pages if self.page_words[p] is None]

        if missing:
            msg = (f"{len(missing)} сторінок не прочитані"
                   f" ({missing[0]}{'…'+str(missing[-1]) if len(missing)>1 else ''}).\n"
                   "Зберегти прочитані, а пропущені заповнити 0xFF?")
            if not messagebox.askyesno("Деякі сторінки не зчитані", msg):
                return

        readable = [p for p in pages if self.page_words[p] is not None]
        if not readable:
            messagebox.showwarning("Немає даних",
                                   "Жодна сторінка не зчитана.\n"
                                   "Спочатку натисніть «Читати виділений діапазон».")
            return

        lo_r, hi_r = min(readable), max(readable)
        fname = filedialog.asksaveasfilename(
            title="Зберегти дамп flash",
            defaultextension=".bin",
            filetypes=[("Binary dump", "*.bin"),
                       ("All files", "*.*")],
            initialfile=f"flash_p{lo}-{hi}.bin"
        )
        if not fname:
            return

        data = bytearray()
        for p in pages:
            if self.page_words[p] is not None:
                data += words_to_bytes(self.page_words[p])
            else:
                data += b"\xFF" * PAGE_SIZE   # fill unread pages with 0xFF

        with open(fname, "wb") as f:
            f.write(data)

        kb = len(data) // 1024
        self._set_status(f"Збережено {kb} KB ({len(pages)} стор.) → {fname}")
        self._log(f"[SAVE] {len(pages)} pages → {fname}", "ok")
        messagebox.showinfo("Збережено",
                            f"Збережено {len(pages)} сторінок ({kb} KB)\n\n{fname}")

    # ── Serial send / receive ─────────────────────────────────────────────────
    def _send_cmd(self, kind, page, uart_str, on_done=None):
        self.cmd_busy    = True
        self.pending_cmd = kind
        self.pending_page = page
        self.frd_buf     = []
        self._on_done_cb = on_done
        self.worker.send(uart_str)
        self._log(f">>> {uart_str}", "tx")
        self._set_status(f"Очікую відповідь: {uart_str} …")
        self._result("…", "#607D8B", "")
        self._cmd_timeout = self.root.after(8000, self._on_timeout)

    def _send_uart_cmd(self):
        text = self.uart_cmd_var.get().strip()
        if not text:
            return
        if not self.worker:
            messagebox.showwarning("Не підключено", "Спочатку підключіться.")
            return
        self.worker.send(text)
        self._log(f">>> {text}", "tx")
        self.uart_cmd_var.set("")

    def _on_timeout(self):
        if self.cmd_busy:
            self.cmd_busy = False
            self._result("✗ TIMEOUT", "#EF5350", "Немає відповіді від пристрою за 8 с.")
            self._set_status("Таймаут команди")

    def _cancel_timeout(self):
        if hasattr(self, "_cmd_timeout") and self._cmd_timeout:
            self.root.after_cancel(self._cmd_timeout)
            self._cmd_timeout = None

    # ── UART polling ──────────────────────────────────────────────────────────
    def _poll(self):
        while not self.evt_q.empty():
            self._handle_event(self.evt_q.get_nowait())
        while not self.rx_q.empty():
            line = self.rx_q.get_nowait()
            self._log(f"    {line}", "rx")
            if self.cmd_busy:
                self._parse_response(line)
        self.root.after(40, self._poll)

    def _parse_response(self, line):
        p = self.pending_page

        if self.pending_cmd == "frd":
            m = re.match(
                r"\s*\+(\d+):\s+([\dA-Fa-f]{8})\s+([\dA-Fa-f]{8})"
                r"\s+([\dA-Fa-f]{8})\s+([\dA-Fa-f]{8})", line)
            if m:
                for g in m.groups()[1:]:
                    self.frd_buf.append(int(g, 16))
            if len(self.frd_buf) >= 32:
                self._cancel_timeout()
                self.page_words[p] = self.frd_buf[:32]
                ff = all(w == 0xFFFFFFFF for w in self.frd_buf[:32])
                self.page_status[p] = "erased" if ff else "data"
                self._draw_cell(p)
                self._show_hex(p)
                self._result("✓ OK", "#66BB6A",
                             f"Стор {p}: {'стерта (FF)' if ff else 'є дані'}")
                self._set_status(f"Зчитано сторінку {p}")
                self.cmd_busy = False
                if self._on_done_cb:
                    self.root.after(50, self._on_done_cb)
            return

        if self.pending_cmd == "ferase":
            if "erased OK" in line:
                self._cancel_timeout()
                self.page_status[p] = "erased"
                self.page_words[p]  = None
                self._draw_cell(p)
                self._result("✓ OK", "#66BB6A", f"Сторінку {p} стерто.")
                self._set_status(f"Стор {p} стерта")
                self.cmd_busy = False
                if self._on_done_cb:
                    self.root.after(50, self._on_done_cb)
            elif "FAILED" in line and "FERASE" in line:
                self._cancel_timeout()
                self.page_status[p] = "error"
                self._draw_cell(p)
                self._result("✗ ПОМИЛКА", "#EF5350",
                             f"Стирання {p} не вдалося.")
                self._set_status(f"Помилка стирання {p}")
                self.cmd_busy = False
            return

        if self.pending_cmd == "fwrite":
            if "FWRITE" in line and "OK" in line:
                self._cancel_timeout()
                self.page_words[p]  = None
                self.page_status[p] = "data"
                self._draw_cell(p)
                self._result("✓ OK", "#66BB6A",
                             f"Записано в {p}. Читайте для перевірки.")
                self._set_status(f"Запис {p} OK")
                self.cmd_busy = False
                if self._on_done_cb:
                    self.root.after(50, self._on_done_cb)
            elif "FWRITE" in line and "FAILED" in line:
                self._cancel_timeout()
                self.page_status[p] = "error"
                self._draw_cell(p)
                self._result("✗ ПОМИЛКА", "#EF5350", f"Запис {p} не вдався.")
                self._set_status(f"Помилка запису {p}")
                self.cmd_busy = False

    # ── Helpers ───────────────────────────────────────────────────────────────
    def _result(self, icon, color, detail):
        self.result_lbl.config(text=icon, fg=color)
        self.result_detail.config(text=detail,
                                  fg=color if "ПОМИЛКА" in icon else "#37474F")

    def _set_status(self, text):
        self.status_var.set(text)

    def _log(self, line, tag="rx"):
        self.log_txt.config(state=tk.NORMAL)
        self.log_txt.insert(tk.END, line + "\n", tag)
        self.log_txt.see(tk.END)
        # trim if too long
        if int(self.log_txt.index("end-1c").split(".")[0]) > 500:
            self.log_txt.delete("1.0", "80.0")
        self.log_txt.config(state=tk.DISABLED)

    def _clear_log(self):
        self.log_txt.config(state=tk.NORMAL)
        self.log_txt.delete("1.0", tk.END)
        self.log_txt.config(state=tk.DISABLED)

    # ── Connection ────────────────────────────────────────────────────────────
    def _refresh_ports(self):
        ports = ([p.device for p in serial.tools.list_ports.comports()]
                 if HAS_SERIAL else [])
        self.port_cb["values"] = ports
        if ports:
            prefs    = _load_prefs()
            last     = prefs.get("port", "")
            if last in ports:
                self.port_cb.set(last)
            else:
                self.port_cb.set(ports[0])

    def _toggle_connect(self):
        if self.worker:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        if not HAS_SERIAL:
            messagebox.showerror("pyserial відсутній",
                                 "Встановіть:\n  pip install pyserial")
            return
        port = self.port_cb.get()
        if not port:
            messagebox.showwarning("Порт", "Виберіть COM-порт зі списку.")
            return
        try:
            baud = int(self.baud_cb.get())
        except ValueError:
            baud = BAUD
        _save_prefs({"port": port})   # remember for next launch
        self.worker = SerialWorker(port, baud, self.rx_q, self.evt_q)
        self.worker.start()
        self.conn_btn.config(text="■ Відключити")
        self._set_status(f"Підключення до {port} @ {baud}…")

    def _disconnect(self):
        if self.worker:
            self.worker.stop()
            self.worker = None
        self.cmd_busy = False
        self.conn_btn.config(text="▶ Connect")

    def _handle_event(self, ev):
        if ev == "connected":
            self.conn_led.itemconfig(self._led_oval, fill="#66BB6A")
            self.conn_lbl.config(text="Connected", fg="#66BB6A")
            self._set_status(f"Підключено → {self.port_cb.get()}")
        elif ev == "disconnected":
            self.conn_led.itemconfig(self._led_oval, fill="#EF5350")
            self.conn_lbl.config(text="Disconnected", fg="#EF5350")
            self.worker = None
            self.conn_btn.config(text="▶ Connect")
            self._set_status("Відключено.")
        elif ev.startswith("error:"):
            self.conn_led.itemconfig(self._led_oval, fill="#FF9800")
            self.conn_lbl.config(text="Error", fg="#FF9800")
            self._set_status(f"Помилка порту: {ev[6:]}")
            self.worker = None
            self.conn_btn.config(text="▶ Connect")


# ══════════════════════════════════════════════════════════════════════════════
def main():
    root = tk.Tk()
    root.tk_setPalette(background="#ECEFF1")
    app  = FlashExplorer(root)
    root.protocol("WM_DELETE_WINDOW", lambda: (app._disconnect(), root.destroy()))
    root.mainloop()


if __name__ == "__main__":
    main()
