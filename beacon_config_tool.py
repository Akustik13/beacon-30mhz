"""
Beacon 30 MHz — Configuration Tool  v3
UART: GET / SET <param> <val> / SAVE / LOAD / RESET / STATUS / HELP
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import threading, queue, re
from collections import deque

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    import sys, subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pyserial"])
    import serial
    import serial.tools.list_ports

# ─── Param definitions ───────────────────────────────────────────────────────
LED_CHOICES    = ["0=off", "1=init_only", "2=ble_status", "3=tx_pulse",
                  "4=heartbeat", "5=always_on"]
MODE_CHOICES   = ["0=off", "1=fixed", "2=ch_sweep", "3=pwr_sweep",
                  "4=full_sweep", "5=continuous"]
BOOL_CHOICES   = ["0=off", "1=on"]
CH_CHOICES     = ["0", "1", "2", "3"]
PWR_CHOICES    = ["1", "2", "3", "4"]
BLEPWR_CHOICES = ["0=-40dBm", "1=-20dBm", "2=-15dBm", "3=-10dBm",
                  "4=-5dBm",  "5=0dBm",   "6=+3dBm",  "7=+6dBm"]

# Groups: list of (key, label, wtype, lo, hi, choices, pin_hint)
GROUPS = [
    ("RF Transmitter", [
        ("rf_mode",      "Mode",          "combo",  None,  None,   MODE_CHOICES,  ""),
        ("rf_ch",        "Channel (0-3)", "combo",  None,  None,   CH_CHOICES,    "PA6=bit0  PB8=bit1"),
        ("rf_pwr",       "Power (1-4)",   "combo",  None,  None,   PWR_CHOICES,   "PA1=bit0  PA8=bit1"),
        ("rf_on_ms",     "Pulse ON (ms)", "spin",   10,    60000,  None,          "PB5=EN  (mode 1 only)"),
        ("rf_period_ms", "Period (ms)",   "spin",   100,   300000, None,          "on+off=period"),
    ]),
    ("LED", [
        ("led",          "Mode",          "combo",  None,  None,   LED_CHOICES,   "PB0 via 120 Ohm"),
    ]),
    ("BLE", [
        ("ble_adv_ms",   "Adv interval",  "spin",   100,   30000,  None,          "ms (0.625ms units)"),
        ("ble_pwr",      "TX power",      "combo",  None,  None,   BLEPWR_CHOICES,"PA_Level 0-7"),
        ("ble",          "Enabled",       "combo",  None,  None,   BOOL_CHOICES,  "sent LAST — applies adv_ms+pwr"),
    ]),
    ("Debug / Sensors", [
        ("dbg_batt",     "Battery divider always on", "combo", None, None, BOOL_CHOICES,
                         "PA12=LOW: on  PA7=ADC"),
        ("dbg_light",    "Light sensor always on",    "combo", None, None, BOOL_CHOICES,
                         "PB1=HIGH: on  PA5=ADC"),
        ("sensor_ms",    "Print period (ms)",         "spin",  100,  60000, None, ""),
    ]),
]

# key → (wtype, choices)  — flat lookup for parser
KEY_PARSER = {
    "led":          ("combo", LED_CHOICES),
    "rf_mode":      ("combo", MODE_CHOICES),
    "rf_ch":        ("combo", CH_CHOICES),
    "rf_pwr":       ("combo", PWR_CHOICES),
    "rf_on_ms":     ("spin",  None),
    "rf_period_ms": ("spin",  None),
    "dbg_batt":     ("combo", BOOL_CHOICES),
    "dbg_light":    ("combo", BOOL_CHOICES),
    "ble":          ("combo", BOOL_CHOICES),
    "ble_adv_ms":   ("spin",  None),
    "ble_pwr":      ("combo", BLEPWR_CHOICES),
    "sensor_ms":    ("spin",  None),
}

RF_STATE_NAMES = {0:"IDLE", 1:"FIXED_ON", 2:"FIXED_OFF",
                  3:"CH_SWEEP", 4:"PWR_SWEEP", 5:"CONTINUOUS"}
RF_MODE_NAMES  = {0:"off", 1:"fixed", 2:"ch_sweep",
                  3:"pwr_sweep", 4:"full_sweep", 5:"continuous"}

# ─── Color palette (light / professional) ────────────────────────────────────
BG     = "#f0f2f5"   # window background
BG2    = "#ffffff"   # input field / card background
BG3    = "#d0d5dd"   # button / border
FG     = "#111827"   # primary text
FG_DIM = "#6b7280"   # secondary / pin hints
GREEN  = "#166534"   # sensor values, success
BLUE   = "#1d4ed8"   # group headers, accents
RED    = "#991b1b"   # error, danger
YELLOW = "#92400e"   # warnings
MAUVE  = "#5b21b6"   # BLE status
ACCENT = "#2563eb"   # active accent (frame borders, highlights)


class BeaconConfigTool(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Beacon 30 MHz — Config Tool v3")
        self.configure(bg=BG)
        self.resizable(True, True)
        self.minsize(900, 650)

        self.serial    = None
        self.rx_queue  = queue.Queue()
        self.rx_thread = None
        self._stop_rx  = threading.Event()
        self.widgets      = {}      # key → tk.StringVar
        self._ack_labels  = {}      # key → tk.Label (status icon)
        self._ack_status  = {}      # key → "none"|"pending"|"ok"|"err"
        self._ack_pending = deque() # FIFO: keys waiting for ACK from MCU
        self._cpu2_state  = 0       # last known cpu2 state from STATUS
        self.ble_frame    = None    # BLE LabelFrame — dimmed when cpu2=0

        # Status StringVars
        self.sv = {k: tk.StringVar(value="—") for k in [
            "temp", "ax", "ay", "az", "mag", "light", "batt", "vdd",
            "cpu2", "ble_adv", "ble_conn",
            "rf_mode", "rf_state", "rf_step",
        ]}

        self._apply_style()
        self._build_ui()
        self._refresh_ports()
        self.after(100,  self._poll_queue)
        self.after(3000, self._auto_status)

    # ─── Style ───────────────────────────────────────────────────────────────
    def _apply_style(self):
        s = ttk.Style()
        s.theme_use("clam")
        s.configure(".",               background=BG,   foreground=FG,
                    font=("Segoe UI", 9))
        s.configure("TFrame",          background=BG)
        s.configure("TLabelframe",     background=BG,   foreground=BLUE,
                    bordercolor=ACCENT, lightcolor=BG3, darkcolor=BG3,
                    borderwidth=1)
        s.configure("TLabelframe.Label", background=BG, foreground=BLUE,
                    font=("Segoe UI", 9, "bold"))
        s.configure("TLabel",          background=BG,   foreground=FG)
        s.configure("TButton",         background=BG3,  foreground=FG,
                    padding=(6, 3), relief="flat", borderwidth=1)
        s.map("TButton",  background=[("active", "#b8c0cc")])
        s.configure("Accent.TButton",  background="#1d6f42", foreground="#fff",
                    padding=(6, 3))
        s.map("Accent.TButton",  background=[("active", "#165a35")])
        s.configure("Danger.TButton",  background="#b91c1c", foreground="#fff",
                    padding=(6, 3))
        s.map("Danger.TButton",  background=[("active", "#991b1b")])
        s.configure("Disconnect.TButton", background="#c2410c", foreground="#fff",
                    padding=(6, 3))
        s.map("Disconnect.TButton", background=[("active", "#9a3412")])
        # Global tk option defaults for native widgets
        self.option_add("*OptionMenu*relief",        "groove")
        self.option_add("*Listbox.background",       BG2)
        self.option_add("*Listbox.foreground",       FG)
        self.option_add("*Listbox.selectBackground", ACCENT)
        self.option_add("*Listbox.selectForeground", "#ffffff")

    # ─── UI Build ────────────────────────────────────────────────────────────
    def _build_ui(self):
        self.columnconfigure(0, weight=3, minsize=480)
        self.columnconfigure(1, weight=2, minsize=300)
        self.rowconfigure(1, weight=0)
        self.rowconfigure(2, weight=1)

        self._build_conn_bar()    # row 0
        self._build_left_panel()  # row 1, col 0
        self._build_right_panel() # row 1, col 1
        self._build_log_panel()   # row 2, col 0-1

    # ── Connection bar ───────────────────────────────────────────────────────
    def _build_conn_bar(self):
        f = tk.Frame(self, bg=BG3, pady=5)
        f.grid(row=0, column=0, columnspan=2, sticky="ew")
        f.columnconfigure(8, weight=1)

        def lbl(text): return tk.Label(f, text=text, bg=BG3, fg=FG)
        _cb_opts = dict(
            bg=BG2, fg=FG, activebackground=ACCENT, activeforeground="#fff",
            highlightthickness=1, highlightbackground=BG3, highlightcolor=ACCENT,
            relief="groove", font=("Segoe UI", 9), borderwidth=1,
        )

        lbl("  Port:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_cb  = tk.OptionMenu(f, self.port_var, "")
        self.port_cb.config(**_cb_opts)
        self.port_cb["menu"].config(bg=BG2, fg=FG, activebackground=ACCENT,
                                    activeforeground="#fff", font=("Segoe UI", 9))
        self.port_cb.pack(side="left", padx=(2, 8))

        lbl("Baud:").pack(side="left")
        _baud_opts = ["9600","19200","38400","57600","115200","230400"]
        self.baud_var = tk.StringVar(value="115200")
        _bm = tk.OptionMenu(f, self.baud_var, *_baud_opts)
        _bm.config(**_cb_opts)
        _bm["menu"].config(bg=BG2, fg=FG, activebackground=ACCENT,
                           activeforeground="#fff", font=("Segoe UI", 9))
        _bm.pack(side="left", padx=(2, 8))

        ttk.Button(f, text="Refresh", command=self._refresh_ports,
                   width=8).pack(side="left", padx=2)
        self.btn_connect = ttk.Button(f, text="Connect",
                                      command=self._toggle_connect,
                                      style="Accent.TButton", width=10)
        self.btn_connect.pack(side="left", padx=(4, 8))

        self.conn_lbl = tk.Label(f, text="● Disconnected",
                                 fg=RED, bg=BG3, font=("Segoe UI", 9, "bold"))
        self.conn_lbl.pack(side="left")

    # ── Left panel: config groups + action buttons ───────────────────────────
    def _build_left_panel(self):
        outer = tk.Frame(self, bg=BG)
        outer.grid(row=1, column=0, sticky="nsew", padx=(6,3), pady=6)
        outer.columnconfigure(0, weight=1)

        # Common widget config — native tk widgets respect these on Windows
        COMBO_OPTS = dict(
            bg=BG2, fg=FG, activebackground=ACCENT, activeforeground="#fff",
            highlightthickness=1, highlightbackground=BG3, highlightcolor=ACCENT,
            relief="groove", font=("Consolas", 9), borderwidth=1,
        )
        SPIN_OPTS = dict(
            bg=BG2, fg=FG, insertbackground=FG,
            activebackground="#e8edf5", selectbackground=ACCENT, selectforeground="#fff",
            buttonbackground=BG3, relief="groove",
            highlightthickness=1, highlightbackground=BG3, highlightcolor=ACCENT,
            font=("Consolas", 9),
        )

        for i, (group_name, params) in enumerate(GROUPS):
            gf = ttk.LabelFrame(outer, text=f"  {group_name}  ", padding=(8, 4))
            gf.grid(row=i, column=0, sticky="ew", pady=(0, 6))
            gf.columnconfigure(1, weight=1)
            if group_name == "BLE":
                self.ble_frame = gf

            for row_i, (key, label, wtype, lo, hi, choices, pin) in enumerate(params):
                tk.Label(gf, text=label, bg=BG, fg=FG, anchor="w", width=22
                         ).grid(row=row_i, column=0, sticky="w", padx=(2,4), pady=2)

                var = tk.StringVar()
                if wtype == "combo":
                    # tk.OptionMenu respects bg/fg on Windows (unlike ttk.Combobox)
                    var.set(choices[0] if choices else "")
                    w = tk.OptionMenu(gf, var, *choices)
                    w.config(**COMBO_OPTS)
                    w["menu"].config(
                        bg=BG2, fg=FG, activebackground=ACCENT, activeforeground="#fff",
                        font=("Consolas", 9), borderwidth=1,
                    )
                    w.grid(row=row_i, column=1, sticky="ew", padx=2, pady=2)
                else:
                    var.set(str(lo))
                    # tk.Spinbox respects bg/fg on Windows (unlike ttk.Spinbox)
                    w = tk.Spinbox(gf, textvariable=var, from_=lo, to=hi,
                                   width=16, **SPIN_OPTS)
                    w.grid(row=row_i, column=1, sticky="ew", padx=2, pady=2)

                if pin:
                    tk.Label(gf, text=pin, bg=BG, fg=FG_DIM,
                             font=("Consolas", 8), anchor="w"
                             ).grid(row=row_i, column=2, sticky="w", padx=(6, 2))

                self.widgets[key] = var

                # ACK status indicator (column 3)
                ack_lbl = tk.Label(gf, text="○", bg=BG, fg=FG_DIM,
                                   font=("Segoe UI", 11), width=2, anchor="center")
                ack_lbl.grid(row=row_i, column=3, padx=(2, 4), pady=2)
                self._ack_labels[key] = ack_lbl
                self._ack_status[key] = "none"

        # Action buttons
        bf = ttk.LabelFrame(outer, text="  Actions  ", padding=(6, 4))
        bf.grid(row=len(GROUPS), column=0, sticky="ew", pady=(0, 4))

        btns = [
            ("Read (GET)",    self._do_get,    None),
            ("Apply (SET)",   self._do_set_all,None),
            ("Save Flash",    self._do_save,   "Accent.TButton"),
            ("Load Flash",    self._do_load,   None),
            ("Defaults",      self._do_reset,  "Danger.TButton"),
            ("STATUS",        self._do_status, None),
        ]
        cols = 3
        for idx, (lbl, cmd, style) in enumerate(btns):
            kw = {"style": style} if style else {}
            ttk.Button(bf, text=lbl, command=cmd, width=14, **kw
                       ).grid(row=idx // cols, column=idx % cols,
                              padx=3, pady=2, sticky="ew")

    # ── Right panel: status displays ─────────────────────────────────────────
    def _build_right_panel(self):
        outer = tk.Frame(self, bg=BG)
        outer.grid(row=1, column=1, sticky="nsew", padx=(3, 6), pady=6)
        outer.columnconfigure(0, weight=1)

        # ── Sensors ──────────────────────────────────────────────────────────
        sf = ttk.LabelFrame(outer, text="  Sensors  ", padding=(8, 6))
        sf.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        sf.columnconfigure(1, weight=1)

        sensor_rows = [
            ("Temperature",   "temp",  "°C",  False),
            ("Accel X",       "ax",    "mg",   False),
            ("Accel Y",       "ay",    "mg",   False),
            ("Accel Z",       "az",    "mg",   False),
            ("|g| magnitude", "mag",   "mg",   False),
            ("Light",         "light", "raw",  False),
            ("Battery",       "batt",  "mV",   False),
            ("VDD",           "vdd",   "mV",   False),
        ]
        for r, (label, key, unit, _) in enumerate(sensor_rows):
            tk.Label(sf, text=label, bg=BG, fg=FG_DIM, anchor="w", width=14
                     ).grid(row=r, column=0, sticky="w", padx=2, pady=1)
            tk.Label(sf, textvariable=self.sv[key],
                     bg=BG2, fg=GREEN, font=("Consolas", 10, "bold"),
                     anchor="e", width=10, padx=4, relief="groove", borderwidth=1
                     ).grid(row=r, column=1, sticky="ew", padx=2, pady=1)
            tk.Label(sf, text=unit, bg=BG, fg=FG_DIM, anchor="w", width=4
                     ).grid(row=r, column=2, sticky="w", padx=(0,2))

        # ── RF Status ─────────────────────────────────────────────────────────
        rf = ttk.LabelFrame(outer, text="  RF Transmitter  ", padding=(8, 6))
        rf.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        rf.columnconfigure(1, weight=1)

        rf_rows = [
            ("Mode",   "rf_mode",  ""),
            ("State",  "rf_state", ""),
            ("Step",   "rf_step",  ""),
        ]
        for r, (label, key, unit) in enumerate(rf_rows):
            tk.Label(rf, text=label, bg=BG, fg=FG_DIM, anchor="w", width=8
                     ).grid(row=r, column=0, sticky="w", padx=2, pady=2)
            tk.Label(rf, textvariable=self.sv[key],
                     bg=BG2, fg=BLUE, font=("Consolas", 10, "bold"),
                     anchor="w", width=14, padx=4, relief="groove", borderwidth=1
                     ).grid(row=r, column=1, sticky="ew", padx=2, pady=2)

        # Pin state table
        tk.Label(rf, text="PB5=EN  PA6/PB8=CH  PA1/PA8=PWR",
                 bg=BG, fg=FG_DIM, font=("Consolas", 8)
                 ).grid(row=len(rf_rows), column=0, columnspan=3,
                        sticky="w", padx=2, pady=(4,0))

        # ── BLE Status ────────────────────────────────────────────────────────
        bl = ttk.LabelFrame(outer, text="  BLE  ", padding=(8, 6))
        bl.grid(row=2, column=0, sticky="ew", pady=(0, 6))
        bl.columnconfigure(1, weight=1)

        ble_rows = [
            ("CPU2",       "cpu2"),
            ("Advertising","ble_adv"),
            ("Connected",  "ble_conn"),
        ]
        for r, (label, key) in enumerate(ble_rows):
            tk.Label(bl, text=label, bg=BG, fg=FG_DIM, anchor="w", width=12
                     ).grid(row=r, column=0, sticky="w", padx=2, pady=2)
            tk.Label(bl, textvariable=self.sv[key],
                     bg=BG2, fg=MAUVE, font=("Consolas", 10, "bold"),
                     anchor="w", width=14, padx=4, relief="groove", borderwidth=1
                     ).grid(row=r, column=1, sticky="ew", padx=2, pady=2)

        # ── CPU2 & BLE Manual Control ─────────────────────────────────────────
        ctrl = ttk.LabelFrame(outer, text="  CPU2 & BLE Control  ", padding=(8, 6))
        ctrl.grid(row=3, column=0, sticky="ew")
        for c in range(4):
            ctrl.columnconfigure(c, weight=1)

        ctrl_btns = [
            ("CPU2 INIT",  "#1e40af", self._do_cpu2_init),
            ("CPU2 STOP",  "#7f1d1d", self._do_cpu2_stop),
            ("BLE START",  "#14532d", self._do_ble_start),
            ("BLE STOP",   "#7c2d12", self._do_ble_stop),
        ]
        for col, (lbl_text, color, cmd_fn) in enumerate(ctrl_btns):
            tk.Button(ctrl, text=lbl_text, command=cmd_fn,
                      bg=color, fg="#ffffff",
                      activebackground="#374151", activeforeground="#ffffff",
                      font=("Segoe UI", 9, "bold"),
                      relief="flat", padx=4, pady=5,
                      ).grid(row=0, column=col, padx=2, pady=2, sticky="ew")

    # ── Log panel ────────────────────────────────────────────────────────────
    def _build_log_panel(self):
        lf = ttk.LabelFrame(self, text="  UART Log  ", padding=(4, 4))
        lf.grid(row=2, column=0, columnspan=2,
                sticky="nsew", padx=6, pady=(0, 6))
        lf.columnconfigure(0, weight=1)
        lf.rowconfigure(0, weight=1)

        self.log = scrolledtext.ScrolledText(
            lf, height=9, state="disabled",
            font=("Consolas", 9), bg="#1e2330", fg="#e2e8f0",
            insertbackground=FG, selectbackground=BG3, relief="sunken", borderwidth=1)
        self.log.grid(row=0, column=0, columnspan=5, sticky="nsew")
        self.log.tag_configure("tx",  foreground="#60a5fa")   # blue  — sent
        self.log.tag_configure("rx",  foreground="#4ade80")   # green — received
        self.log.tag_configure("err", foreground="#f87171")   # red   — errors
        self.log.tag_configure("sys", foreground="#94a3b8")   # gray  — system

        bar = tk.Frame(lf, bg=BG)
        bar.grid(row=1, column=0, columnspan=5, sticky="ew", pady=(4, 0))
        tk.Label(bar, text="CMD:", bg=BG, fg=FG_DIM).pack(side="left")
        self.cmd_var = tk.StringVar()
        e = tk.Entry(bar, textvariable=self.cmd_var, width=40,
                     bg=BG2, fg=FG, insertbackground=FG,
                     font=("Consolas", 10), relief="groove", borderwidth=1)
        e.pack(side="left", padx=4)
        e.bind("<Return>", lambda _: self._send_manual())
        ttk.Button(bar, text="Send",  command=self._send_manual).pack(side="left")
        ttk.Button(bar, text="Clear", command=self._clear_log).pack(side="right", padx=4)

    # ─── Serial ──────────────────────────────────────────────────────────────
    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        menu = self.port_cb["menu"]
        menu.delete(0, "end")
        for p in ports:
            menu.add_command(label=p, command=lambda v=p: self.port_var.set(v))
        if ports:
            if not self.port_var.get() or self.port_var.get() not in ports:
                self.port_var.set(ports[0])

    def _toggle_connect(self):
        if self.serial and self.serial.is_open:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port = self.port_var.get()
        if not port:
            messagebox.showerror("Error", "Select a COM port"); return
        try:
            self.serial = serial.Serial(port, int(self.baud_var.get()), timeout=0.05)
            self._stop_rx.clear()
            self.rx_thread = threading.Thread(target=self._rx_worker, daemon=True)
            self.rx_thread.start()
            self.btn_connect.configure(text="Disconnect", style="Disconnect.TButton")
            self.conn_lbl.configure(text=f"● {port}", fg="#166534", bg=BG3)
            self._log(f"[CONN] {port} @ {self.baud_var.get()}\n", "sys")
            self._clear_ack_state()
            self.after(300, self._do_get)  # auto-read config on connect
        except Exception as e:
            messagebox.showerror("Connect error", str(e))

    def _disconnect(self):
        self._stop_rx.set()
        if self.serial:
            try: self.serial.close()
            except: pass
            self.serial = None
        self.btn_connect.configure(text="Connect", style="Accent.TButton")
        self.conn_lbl.configure(text="● Disconnected", fg=RED, bg=BG3)
        self._log("[CONN] disconnected\n", "sys")
        self._clear_ack_state()

    def _rx_worker(self):
        buf = b""
        while not self._stop_rx.is_set():
            try: data = self.serial.read(256)
            except: break
            if data:
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    self.rx_queue.put(line.rstrip(b"\r").decode("latin-1", errors="replace"))

    def _send(self, cmd: str):
        if not (self.serial and self.serial.is_open):
            self._log("[ERR] not connected\n", "err"); return
        self._log(f">>> {cmd}\n", "tx")
        self.serial.write((cmd + "\r\n").encode())

    # ─── Queue / auto-refresh ────────────────────────────────────────────────
    def _poll_queue(self):
        try:
            while True:
                line = self.rx_queue.get_nowait()
                self._log(f"<<< {line}\n", "rx")
                self._parse_line(line)
        except queue.Empty:
            pass
        self.after(50, self._poll_queue)

    def _auto_status(self):
        if self.serial and self.serial.is_open:
            self.serial.write(b"STATUS\r\n")  # silent — no >>> echo
        self.after(3000, self._auto_status)

    # ─── Parser ──────────────────────────────────────────────────────────────
    def _parse_line(self, line: str):
        s = line.strip()

        # Sensor line: T=X.X C  Ax=X Ay=X Az=X |g|=X mg  Light=X  Batt=X mV  VDD=X mV
        def grab(pat):
            m = re.search(pat, s)
            return m.group(1) if m else None

        v = grab(r"T=([-\d.]+)\s*C")
        if v: self.sv["temp"].set(v)

        v = grab(r"Ax=([-\d]+)");
        if v: self.sv["ax"].set(v)
        v = grab(r"Ay=([-\d]+)");
        if v: self.sv["ay"].set(v)
        v = grab(r"Az=([-\d]+)");
        if v: self.sv["az"].set(v)
        v = grab(r"\|g\|=(\d+)");
        if v: self.sv["mag"].set(v)
        v = grab(r"Light=(\d+)");
        if v: self.sv["light"].set(v)
        v = grab(r"Batt=(\d+)");
        if v: self.sv["batt"].set(v + " mV")
        v = grab(r"VDD=(\d+)");
        if v: self.sv["vdd"].set(v + " mV")

        # STATUS lines (now multi-line)
        v = grab(r"cpu2=(\d+)")
        if v:
            iv = int(v)
            self.sv["cpu2"].set({
                0:   "not started",
                1:   "OK (wireless)",
                2:   "FUS mode!",
                255: "timeout!",
            }.get(iv, f"err {iv}"))
            if iv != self._cpu2_state:
                self._cpu2_state = iv
                self._set_ble_frame_state(iv)

        v = grab(r"ble_st=(\d+)")
        BLE_ST_NAMES = {0:"IDLE", 1:"FAST_ADV", 2:"LP_ADV", 5:"CONNECTED"}
        if v: self.sv["ble_adv"].set(BLE_ST_NAMES.get(int(v), f"st={v}"))

        v = grab(r"ble_adv=(\d+)")
        if v: self.sv["ble_adv"].set("ON" if v == "1" else "off")
        v = grab(r"ble_conn=(\d+)")
        if v: self.sv["ble_conn"].set("CONNECTED" if v == "1" else "none")

        v = grab(r"rf_mode=(\d+)")
        if v: self.sv["rf_mode"].set(RF_MODE_NAMES.get(int(v), v))
        v = grab(r"rf_state=(\d+)")
        if v: self.sv["rf_state"].set(RF_STATE_NAMES.get(int(v), v))
        v = grab(r"rf_step=(\d+)")
        if v: self.sv["rf_step"].set(v)

        # GET response: KEY=VALUE (single word=integer)
        m = re.match(r"^(\w+)=(-?\d+)$", s)
        if m:
            key, val_str = m.group(1), m.group(2)
            if key in KEY_PARSER:
                wtype, choices = KEY_PARSER[key]
                var = self.widgets.get(key)
                if var is not None:
                    try:
                        ival = int(val_str)
                        if wtype == "combo" and choices:
                            match = next(
                                (c for c in choices if c.split("=")[0] == str(ival)),
                                str(ival))
                            var.set(match)
                        else:
                            var.set(str(ival))
                    except ValueError:
                        pass

        # ACK detection — bare "OK" = SET command confirmed by MCU
        if s == "OK":
            if self._ack_pending:
                self._update_ack_label(self._ack_pending.popleft(), "ok")
            return

        # Error response — pop pending and mark as error
        if s.startswith("ERR:") or s.startswith("ERR "):
            if self._ack_pending:
                self._update_ack_label(self._ack_pending.popleft(), "err")

    # ─── ACK & state helpers ─────────────────────────────────────────────────
    def _clear_ack_state(self):
        self._ack_pending.clear()
        for key in self._ack_labels:
            self._update_ack_label(key, "none")

    def _set_ble_frame_state(self, cpu2: int):
        if self.ble_frame is None:
            return
        if cpu2 == 0:
            self.ble_frame.configure(
                text="  BLE  [CPU2 off — edit values, apply after CPU2INIT]  ")
        else:
            self.ble_frame.configure(text="  BLE  ")

    def _update_ack_label(self, key: str, status: str):
        lbl = self._ack_labels.get(key)
        if lbl is None:
            return
        self._ack_status[key] = status
        if status == "none":
            lbl.configure(text="○", fg=FG_DIM)
        elif status == "pending":
            lbl.configure(text="⋯", fg=YELLOW)
        elif status == "ok":
            lbl.configure(text="✓", fg=GREEN)
        elif status == "err":
            lbl.configure(text="✗", fg=RED)

    def _send_set(self, key: str, val: str):
        if not (self.serial and self.serial.is_open):
            self._log("[ERR] not connected\n", "err")
            return
        self._update_ack_label(key, "pending")
        self._ack_pending.append(key)
        self._send(f"SET {key} {val}")

    # ─── Actions ─────────────────────────────────────────────────────────────
    def _do_get(self):
        self._send("GET")

    def _do_set_all(self):
        pairs = []
        for _, params in GROUPS:
            for key, *_ in params:
                raw = self.widgets[key].get()
                val = raw.split("=")[0]
                pairs.append((key, val))
        # reset all to "none" so stale ✓/✗ don't confuse
        for key, _ in pairs:
            self._update_ack_label(key, "none")

        def send_next(idx=0):
            if idx >= len(pairs):
                return
            key, val = pairs[idx]
            self._send_set(key, val)
            self.after(60, lambda: send_next(idx + 1))

        send_next()

    def _do_save(self):
        self._send("SAVE")

    def _do_load(self):
        self._send("LOAD")
        self.after(300, self._do_get)

    def _do_reset(self):
        if messagebox.askyesno("Reset defaults",
                               "Restore all factory defaults?\n(not written to Flash)"):
            self._send("RESET")
            self.after(200, self._do_get)

    def _do_status(self):
        self._send("STATUS")

    def _do_cpu2_init(self):
        self._send("CPU2INIT")

    def _do_cpu2_stop(self):
        if messagebox.askyesno("Stop CPU2",
                               "Shutdown CPU2 now?\n"
                               "MCU reset will be required to restart it."):
            self._send("CPU2STOP")

    def _do_ble_start(self):
        self._send("BLESTART")

    def _do_ble_stop(self):
        self._send("BLESTOP")

    def _send_manual(self):
        cmd = self.cmd_var.get().strip()
        if cmd:
            self._send(cmd)
            self.cmd_var.set("")

    # ─── Log ─────────────────────────────────────────────────────────────────
    def _log(self, text: str, tag: str = "rx"):
        self.log.configure(state="normal")
        self.log.insert("end", text, tag)
        self.log.see("end")
        self.log.configure(state="disabled")

    def _clear_log(self):
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def destroy(self):
        self._disconnect()
        super().destroy()


if __name__ == "__main__":
    app = BeaconConfigTool()
    app.mainloop()
