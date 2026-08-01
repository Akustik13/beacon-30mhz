#!/usr/bin/env python3
"""TX Beacon 30 MHz — GUI v2  (PyQt6 + pyserial + pyqtgraph)"""
import sys, os, re, json, queue, threading, datetime, csv, subprocess
from collections import deque

def _pip(pkg):
    subprocess.check_call([sys.executable, '-m', 'pip', 'install', pkg, '-q'])

try:
    from PyQt6.QtWidgets import *
    from PyQt6.QtCore import *
    from PyQt6.QtGui import *
except ImportError:
    _pip('PyQt6'); from PyQt6.QtWidgets import *; from PyQt6.QtCore import *; from PyQt6.QtGui import *

try:
    import pyqtgraph as pg
    _HAS_PG = True
except ImportError:
    _pip('pyqtgraph')
    try: import pyqtgraph as pg; _HAS_PG = True
    except: _HAS_PG = False

try:
    import serial, serial.tools.list_ports
except ImportError:
    _pip('pyserial'); import serial, serial.tools.list_ports

# ── Paths ─────────────────────────────────────────────────────────────────────
_DIR           = os.path.dirname(os.path.abspath(__file__))
PROFILES_FILE  = os.path.join(_DIR, 'tx_beacon_profiles.json')
SETTINGS_FILE  = os.path.join(_DIR, 'tx_beacon_settings.json')

# ── Colours ───────────────────────────────────────────────────────────────────
C = {
    'bg':         '#0f1117',
    'surface':    '#1a1d27',
    'surface2':   '#242736',
    'border':     '#2e3147',
    'accent':     '#4f8ef7',
    'accent2':    '#7c3aed',
    'success':    '#22c55e',
    'warning':    '#f59e0b',
    'danger':     '#ef4444',
    'text':       '#e2e8f0',
    'text_dim':   '#94a3b8',
    'text_muted': '#475569',
    'purple':     '#a78bfa',
    'cyan':       '#34d399',
    'blue':       '#60a5fa',
    'orange':     '#fb923c',
    'yellow':     '#fbbf24',
}

MONO = 'Cascadia Code, Consolas, Courier New'

# ── Global stylesheet ─────────────────────────────────────────────────────────
APP_SS = f"""
QWidget {{ background:{C['bg']}; color:{C['text']}; font-family:'Segoe UI',Arial; font-size:10px; }}
QMainWindow {{ background:{C['bg']}; }}
QTabWidget::pane {{ border:1px solid {C['border']}; border-radius:6px; background:{C['surface']}; }}
QTabBar::tab {{ background:{C['surface2']}; color:{C['text_dim']}; padding:6px 14px; border-radius:4px 4px 0 0; margin-right:2px; }}
QTabBar::tab:selected {{ background:{C['surface']}; color:{C['text']}; font-weight:600; }}
QTabBar::tab:hover {{ background:{C['border']}; color:{C['text']}; }}
QGroupBox {{ border:1px solid {C['border']}; border-radius:6px; margin-top:10px; padding-top:6px; color:{C['text_dim']}; font-size:10px; }}
QGroupBox::title {{ subcontrol-origin:margin; left:10px; padding:0 4px; color:{C['accent']}; font-weight:600; }}
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {{
    background:{C['surface2']}; border:1px solid {C['border']}; border-radius:4px;
    padding:3px 6px; color:{C['text']}; font-family:Consolas,monospace;
}}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {{ border-color:{C['accent']}; }}
QComboBox::drop-down {{ border:none; }}
QComboBox QAbstractItemView {{ background:{C['surface2']}; border:1px solid {C['border']}; selection-background-color:{C['accent']}; }}
QScrollBar:vertical {{ background:{C['surface']}; width:8px; border-radius:4px; }}
QScrollBar::handle:vertical {{ background:{C['border']}; border-radius:4px; min-height:20px; }}
QScrollBar::handle:vertical:hover {{ background:{C['accent']}; }}
QScrollBar:horizontal {{ background:{C['surface']}; height:8px; border-radius:4px; }}
QScrollBar::handle:horizontal {{ background:{C['border']}; border-radius:4px; }}
QProgressBar {{ background:{C['surface2']}; border:1px solid {C['border']}; border-radius:4px; height:8px; text-align:center; }}
QProgressBar::chunk {{ background:{C['success']}; border-radius:4px; }}
QCheckBox {{ spacing:6px; color:{C['text']}; }}
QCheckBox::indicator {{ width:14px; height:14px; border:1px solid {C['border']}; border-radius:3px; background:{C['surface2']}; }}
QCheckBox::indicator:checked {{ background:{C['accent']}; border-color:{C['accent']}; }}
QRadioButton {{ spacing:6px; color:{C['text']}; }}
QRadioButton::indicator {{ width:14px; height:14px; border:1px solid {C['border']}; border-radius:7px; background:{C['surface2']}; }}
QRadioButton::indicator:checked {{ background:{C['accent']}; border-color:{C['accent']}; }}
QTableWidget {{ background:{C['surface']}; gridline-color:{C['border']}; border:none; }}
QTableWidget::item {{ padding:3px 6px; }}
QTableWidget::item:selected {{ background:{C['accent']}; color:white; }}
QHeaderView::section {{ background:{C['surface2']}; color:{C['text_dim']}; border:none; border-bottom:1px solid {C['border']}; padding:4px 6px; }}
QSplitter::handle {{ background:{C['border']}; }}
QToolTip {{ background:{C['surface2']}; color:{C['text']}; border:1px solid {C['border']}; padding:4px; }}
"""

BTN_PRIMARY = f"""QPushButton{{background:{C['accent']};color:white;border:none;border-radius:6px;padding:6px 14px;font-weight:600;}}
QPushButton:hover{{background:#6ba3f9;}}QPushButton:pressed{{background:#3b7de8;}}QPushButton:disabled{{background:{C['border']};color:{C['text_muted']};}}"""
BTN_SUCCESS = f"""QPushButton{{background:{C['success']};color:white;border:none;border-radius:6px;padding:6px 14px;font-weight:600;}}
QPushButton:hover{{background:#4ade80;}}QPushButton:pressed{{background:#16a34a;}}QPushButton:disabled{{background:{C['border']};color:{C['text_muted']};}}"""
BTN_DANGER  = f"""QPushButton{{background:{C['danger']};color:white;border:none;border-radius:6px;padding:6px 14px;font-weight:600;}}
QPushButton:hover{{background:#f87171;}}QPushButton:pressed{{background:#dc2626;}}"""
BTN_WARN    = f"""QPushButton{{background:{C['warning']};color:white;border:none;border-radius:6px;padding:6px 14px;font-weight:600;}}
QPushButton:hover{{background:#fbbf24;}}QPushButton:pressed{{background:#d97706;}}"""
BTN_NEUTRAL = f"""QPushButton{{background:{C['surface2']};color:{C['text']};border:1px solid {C['border']};border-radius:6px;padding:6px 14px;}}
QPushButton:hover{{background:{C['border']};}}QPushButton:pressed{{background:{C['surface']};}}"""

CARD_SS = f"background:{C['surface']};border:1px solid {C['border']};border-radius:8px;padding:8px;"

# ── Preset profiles ───────────────────────────────────────────────────────────
_MONTH_NAMES = ["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"]
_DAY_NAMES   = ["Mon","Tue","Wed","Thu","Fri","Sat","Sun"]

PRESET_PROFILES = {
    "Always active": {
        "mode":"pulse","ch":0,"pwr":3,"pulse_ms":23,"period_ms":2000,
        "led_mode":"tx","sched_enabled":False,"hours":[],"days":[],"months":[]
    },
    "Night (22–06h)": {
        "mode":"pulse","ch":0,"pwr":3,"pulse_ms":23,"period_ms":2000,
        "led_mode":"tx","sched_enabled":True,
        "hours":[22,23,0,1,2,3,4,5,6],"days":[],"months":[]
    },
    "Day (08–20h)": {
        "mode":"pulse","ch":0,"pwr":3,"pulse_ms":23,"period_ms":2000,
        "led_mode":"tx","sched_enabled":True,
        "hours":list(range(8,21)),"days":[],"months":[]
    },
    "Eco night": {
        "mode":"eco","ch":0,"pwr":2,"pulse_ms":50,"period_ms":5000,
        "led_mode":"off","sched_enabled":True,
        "hours":[22,23,0,1,2,3,4,5],"days":[],"months":[]
    },
}

# ── Helpers ───────────────────────────────────────────────────────────────────
def btn(text, style=BTN_NEUTRAL, parent=None, tip=''):
    b = QPushButton(text, parent)
    b.setStyleSheet(style)
    if tip: b.setToolTip(tip)
    return b

def lbl(text, color=None, bold=False, mono=False, size=10, parent=None):
    w = QLabel(text, parent)
    css = f"color:{color or C['text']};"
    if bold: css += "font-weight:600;"
    if mono: css += "font-family:Consolas,monospace;"
    css += f"font-size:{size}px;"
    w.setStyleSheet(css)
    return w

def val_lbl(text='—', color=None):
    w = QLabel(text)
    c = color or C['accent']
    w.setStyleSheet(f"color:{c};font-family:Consolas,monospace;font-size:12px;font-weight:600;"
                    f"background:{C['surface2']};border:1px solid {C['border']};"
                    f"border-radius:4px;padding:2px 8px;")
    w.setMinimumWidth(70)
    return w

def sep_h():
    w = QFrame()
    w.setFrameShape(QFrame.Shape.HLine)
    w.setStyleSheet(f"color:{C['border']};background:{C['border']};max-height:1px;")
    return w

def group(title):
    g = QGroupBox(title)
    return g

def spin(lo, hi, val, suffix='', step=1):
    s = QSpinBox()
    s.setRange(lo, hi); s.setValue(val); s.setSingleStep(step)
    if suffix: s.setSuffix(suffix)
    s.setFixedWidth(80)
    return s

def dspin(lo, hi, val, dec=1, suffix='', step=0.1):
    s = QDoubleSpinBox()
    s.setRange(lo, hi); s.setValue(val); s.setDecimals(dec)
    s.setSingleStep(step)
    if suffix: s.setSuffix(suffix)
    s.setFixedWidth(80)
    return s

# ── UART worker ───────────────────────────────────────────────────────────────
class SerialWorker(QThread):
    line_received  = pyqtSignal(str)
    connected_sig  = pyqtSignal()
    disconnected_sig = pyqtSignal()

    def __init__(self):
        super().__init__()
        self._port    = None
        self._running = False
        self._tx_q    = queue.Queue()

    def connect_port(self, port, baud=115200):
        try:
            self._port = serial.Serial(port, baud, timeout=0.05)
            self._running = True
            self.start()
            self.connected_sig.emit()
            return True
        except Exception as e:
            return str(e)

    def disconnect_port(self):
        self._running = False
        if self._port:
            try: self._port.close()
            except: pass
            self._port = None
        self.disconnected_sig.emit()

    def send(self, text):
        if self._port and self._port.is_open:
            self._tx_q.put((text + '\r\n').encode())

    def is_open(self):
        return bool(self._port and self._port.is_open)

    def run(self):
        buf = b''
        while self._running:
            try:
                while not self._tx_q.empty():
                    self._port.write(self._tx_q.get_nowait())
                data = self._port.read(256)
            except Exception:
                break
            if data:
                buf += data
                while b'\n' in buf:
                    line, buf = buf.split(b'\n', 1)
                    text = line.rstrip(b'\r').decode('latin-1', errors='replace')
                    if text:
                        self.line_received.emit(text)
        self._running = False
        self.disconnected_sig.emit()

# ── Log download thread ───────────────────────────────────────────────────────
class LogDownloadThread(QThread):
    progress       = pyqtSignal(int)
    record_received = pyqtSignal(dict)
    finished_dl    = pyqtSignal(int)
    error          = pyqtSignal(str)

    def __init__(self, worker, cmd='log dump', expected=0):
        super().__init__()
        self._worker   = worker
        self._cmd      = cmd
        self._expected = expected

    def run(self):
        self._worker.send(self._cmd)

# ── Main Window ───────────────────────────────────────────────────────────────
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle('TX Beacon 30 MHz  v2')
        self.resize(1200, 800)
        self.setMinimumSize(900, 650)

        self._worker = SerialWorker()
        self._worker.line_received.connect(self._dispatch)
        self._worker.connected_sig.connect(self._on_connected)
        self._worker.disconnected_sig.connect(self._on_disconnected)

        self._settings        = self._load_settings()
        self._custom_profiles = self._load_profiles()

        # Live sensor state
        self._temp_c   = '—'
        self._vdda_mv  = '—'
        self._batt_mv  = '—'
        self._batt_pct = '—'
        self._batt_raw = '—'
        self._batt_vref= '—'
        self._light_raw= '—'
        self._light_lux= '—'
        self._rtc_date = '—'
        self._rtc_time = '—'
        self._sched_status = '—'

        # Chart buffers
        N = self._settings.get('chart_points', 120)
        self._temp_buf  = deque(maxlen=N)
        self._bat_buf   = deque(maxlen=N)
        self._t_buf     = deque(maxlen=N)
        self._t0        = datetime.datetime.now()

        # Flash log state
        self._log_in_dump   = False
        self._log_csv_buf   = []
        self._log_used      = 0
        self._log_free      = 768
        self._log_total     = 768
        self._verify_active   = False
        self._verify_snapshot = {}
        self._verify_received = {}

        # Schedule state
        self._hour_states  = [False] * 24
        self._day_states   = [False] * 7
        self._month_states = [False] * 12
        self._sched_enabled = False

        self._build_ui()
        self._refresh_ports()

        self._live_timer = QTimer(self)
        self._live_timer.setInterval(1000)
        self._live_timer.timeout.connect(self._on_live_tick)

    # ── Build UI ──────────────────────────────────────────────────────────────
    def _build_ui(self):
        self.setStyleSheet(APP_SS)
        central = QWidget(); self.setCentralWidget(central)
        root = QVBoxLayout(central); root.setSpacing(0); root.setContentsMargins(0,0,0,0)

        self._build_conn_bar(root)

        self._tabs = QTabWidget()
        root.addWidget(self._tabs)

        self._build_tab_dashboard()
        self._build_tab_transmitter()
        self._build_tab_flash_log()
        self._build_tab_settings()
        self._build_tab_terminal()

        self.statusBar().setStyleSheet(f"background:{C['surface']};color:{C['text_dim']};")
        self.statusBar().showMessage('Disconnected')

        idx = self._settings.get('active_tab', 0)
        self._tabs.setCurrentIndex(idx)
        self._tabs.currentChanged.connect(lambda i: self._settings.update({'active_tab': i}))

    # ── Connection bar ────────────────────────────────────────────────────────
    def _build_conn_bar(self, root):
        bar = QWidget()
        bar.setStyleSheet(f"background:{C['surface']};border-bottom:1px solid {C['border']};")
        bar.setFixedHeight(46)
        h = QHBoxLayout(bar); h.setContentsMargins(12,4,12,4); h.setSpacing(8)

        self._dot = QLabel('●')
        self._dot.setStyleSheet(f"color:{C['danger']};font-size:16px;")
        h.addWidget(self._dot)

        self._port_cb = QComboBox(); self._port_cb.setFixedWidth(110)
        h.addWidget(self._port_cb)

        b_ref = btn('↻', BTN_NEUTRAL); b_ref.setFixedWidth(32)
        b_ref.clicked.connect(self._refresh_ports); h.addWidget(b_ref)

        self._btn_conn = btn('Connect', BTN_PRIMARY)
        self._btn_conn.setFixedWidth(100)
        self._btn_conn.clicked.connect(self._toggle_connect); h.addWidget(self._btn_conn)

        h.addStretch(1)
        title = lbl('TX Beacon 30 MHz', bold=True, size=13)
        title.setAlignment(Qt.AlignmentFlag.AlignCenter); h.addWidget(title)
        h.addStretch(1)

        self._badge_temp = lbl('—', color=C['text_dim'], size=10)
        self._badge_temp.setStyleSheet(f"color:{C['text_dim']};background:{C['surface2']};"
                                        f"border:1px solid {C['border']};border-radius:4px;padding:2px 8px;")
        h.addWidget(self._badge_temp)

        self._badge_bat = lbl('—', color=C['text_dim'], size=10)
        self._badge_bat.setStyleSheet(f"color:{C['text_dim']};background:{C['surface2']};"
                                       f"border:1px solid {C['border']};border-radius:4px;padding:2px 8px;")
        h.addWidget(self._badge_bat)

        self._badge_tx = lbl('TX —', color=C['text_dim'], size=10)
        self._badge_tx.setStyleSheet(f"color:{C['text_dim']};background:{C['surface2']};"
                                      f"border:1px solid {C['border']};border-radius:4px;padding:2px 8px;")
        h.addWidget(self._badge_tx)

        root.addWidget(bar)

    # ── Tab 0: Dashboard ──────────────────────────────────────────────────────
    def _build_tab_dashboard(self):
        w = QWidget(); self._tabs.addTab(w, '🏠 Dashboard')
        h = QHBoxLayout(w); h.setContentsMargins(10,10,10,10); h.setSpacing(10)

        # ── Left column ───────────────────────────────────────────────────────
        left = QVBoxLayout(); left.setSpacing(8)

        sensors_g = group('Live Sensors')
        g2 = QGridLayout(sensors_g); g2.setSpacing(8); g2.setColumnStretch(5, 1)

        def _vl(color=C['accent']):
            w2 = QLabel('—')
            w2.setStyleSheet(f"color:{color};font-family:Consolas;font-size:12px;font-weight:600;")
            return w2

        self._dv_temp   = _vl(C['blue']);    self._dv_vdda  = _vl(C['text_dim'])
        self._dv_batmv  = _vl(C['success']); self._dv_batpct= _vl(C['success'])
        self._dv_light  = _vl(C['yellow']);  self._dv_lux   = _vl(C['text_dim'])
        self._dv_mode   = _vl(C['accent'])
        self._dv_rtc    = _vl(C['cyan']);    self._dv_sched = _vl(C['accent'])

        rows = [
            ('🌡️', 'Temperature', self._dv_temp,  '°C',     self._dv_vdda,  'VDDA mV'),
            ('🔋',  'Battery',     self._dv_batmv, 'mV',     self._dv_batpct, '%'),
            ('💡',  'Light',       self._dv_light, 'raw',    self._dv_lux,   'lux~'),
            ('📡',  'TX Mode',     self._dv_mode,  '',       None,            ''),
            ('🕐',  'RTC',         self._dv_rtc,   '',       self._dv_sched,  'sched'),
        ]
        for r, (icon, title, v1, u1, v2, u2) in enumerate(rows):
            g2.addWidget(lbl(icon, size=13),                          r, 0)
            g2.addWidget(lbl(title, color=C['text_dim'], size=9),     r, 1)
            g2.addWidget(v1,                                           r, 2)
            if u1: g2.addWidget(lbl(u1, color=C['text_muted'], size=9), r, 3)
            if v2: g2.addWidget(v2,                                    r, 4)
            if u2: g2.addWidget(lbl(u2, color=C['text_muted'], size=9), r, 5)

        left.addWidget(sensors_g)

        qa_g = group('Quick Actions')
        qa = QHBoxLayout(qa_g); qa.setSpacing(6)
        b_txon = btn('TX ON',        BTN_SUCCESS); b_txon.clicked.connect(lambda: self._cmd('tx on'))
        b_txoff= btn('TX OFF',       BTN_DANGER);  b_txoff.clicked.connect(lambda: self._cmd('tx off'))
        b_rtc  = btn('⟳ Sync RTC',  BTN_NEUTRAL); b_rtc.clicked.connect(self._sync_rtc)
        b_save = btn('💾 Save Flash',BTN_PRIMARY);  b_save.clicked.connect(self._do_save)
        b_rst  = btn('↺ Reset',      BTN_WARN);    b_rst.clicked.connect(self._do_reset)
        b_slp  = btn('😴 Sleep',     BTN_DANGER);  b_slp.clicked.connect(self._do_sleep)
        for b in [b_txon, b_txoff, b_rtc, b_save, b_rst, b_slp]: qa.addWidget(b)
        qa.addStretch()
        left.addWidget(qa_g)
        left.addStretch()

        lw = QWidget(); lw.setLayout(left); lw.setMaximumWidth(420)
        h.addWidget(lw)

        # ── Right column: charts ──────────────────────────────────────────────
        right = QVBoxLayout(); right.setSpacing(8)

        if _HAS_PG:
            pg.setConfigOption('background', C['bg'])
            pg.setConfigOption('foreground', C['text'])

            temp_g = group('Temperature'); tgl = QVBoxLayout(temp_g)
            self._temp_plot = pg.PlotWidget()
            self._temp_plot.setLabel('left', 'Temperature', units='°C')
            self._temp_plot.showGrid(x=True, y=True, alpha=0.15)
            self._temp_plot.setMinimumHeight(180)
            nr = pg.LinearRegionItem([36, 38], brush=pg.mkBrush(34, 197, 94, 25), movable=False)
            self._temp_plot.addItem(nr)
            self._temp_curve = self._temp_plot.plot(pen=pg.mkPen(C['accent'], width=2))
            tgl.addWidget(self._temp_plot); right.addWidget(temp_g)

            bat_g = group('Battery'); bgl = QVBoxLayout(bat_g)
            self._bat_plot = pg.PlotWidget()
            self._bat_plot.setLabel('left', 'Battery', units='mV')
            self._bat_plot.showGrid(x=True, y=True, alpha=0.15)
            self._bat_plot.setMinimumHeight(160)
            self._bat_curve = self._bat_plot.plot(pen=pg.mkPen(C['success'], width=2))
            bgl.addWidget(self._bat_plot); right.addWidget(bat_g)
        else:
            right.addWidget(lbl('Install pyqtgraph for charts: pip install pyqtgraph',
                                 color=C['text_muted']))
            right.addStretch()

        rw = QWidget(); rw.setLayout(right)
        h.addWidget(rw, 1)

    # ── Tab 1: Transmitter ────────────────────────────────────────────────────
    def _build_tab_transmitter(self):
        w = QWidget(); self._tabs.addTab(w, '📻 Transmitter')
        v = QVBoxLayout(w); v.setContentsMargins(10,10,10,10); v.setSpacing(8)

        # ── TX Control ────────────────────────────────────────────────────────
        tx_g = group('Transmitter Control')
        tgl = QVBoxLayout(tx_g); tgl.setSpacing(8)

        # Profiles
        prow = QHBoxLayout()
        prow.addWidget(lbl('Profile:', color=C['text_dim']))
        self._prof_cb = QComboBox(); self._prof_cb.setFixedWidth(180)
        self._refresh_profile_combo()
        prow.addWidget(self._prof_cb)
        b_load = btn('▶ Apply', BTN_PRIMARY); b_load.clicked.connect(self._load_selected_profile)
        b_save_as = btn('💾 Save As', BTN_NEUTRAL); b_save_as.clicked.connect(self._save_as_profile)
        b_del = btn('✕', BTN_DANGER); b_del.setFixedWidth(32); b_del.clicked.connect(self._delete_profile)
        prow.addWidget(b_load); prow.addWidget(b_save_as); prow.addWidget(b_del); prow.addStretch()
        tgl.addLayout(prow)
        tgl.addWidget(sep_h())

        # Mode
        mrow = QHBoxLayout(); mrow.setSpacing(4)
        mrow.addWidget(lbl('Mode:', color=C['text_dim'], bold=True))
        self._mode_btns = {}
        self._mode_grp = QButtonGroup(self)
        for val, label in [('off','OFF'),('pulse','Pulse'),('cont','Cont'),('eco','Eco')]:
            rb = QPushButton(label); rb.setCheckable(True)
            rb.setStyleSheet(f"QPushButton{{background:{C['surface2']};color:{C['text']};border:1px solid {C['border']};"
                             f"border-radius:5px;padding:5px 14px;}}QPushButton:checked{{background:{C['accent']};color:white;"
                             f"border-color:{C['accent']};}}QPushButton:hover{{background:{C['border']};}}")
            self._mode_grp.addButton(rb)
            self._mode_btns[val] = rb
            rb.clicked.connect(lambda checked,v=val: self._cmd(f'mode {v}') if checked else None)
            mrow.addWidget(rb)
        self._mode_btns['pulse'].setChecked(True)
        mrow.addStretch()
        tgl.addLayout(mrow)

        # Channel
        crow = QHBoxLayout(); crow.setSpacing(4)
        crow.addWidget(lbl('Channel:', color=C['text_dim'], bold=True))
        self._ch_btns = {}; self._ch_grp = QButtonGroup(self)
        for i in range(4):
            rb = QPushButton(f'CH{i}'); rb.setCheckable(True)
            rb.setStyleSheet(f"QPushButton{{background:{C['surface2']};color:{C['text']};border:1px solid {C['border']};"
                             f"border-radius:5px;padding:5px 12px;}}QPushButton:checked{{background:{C['accent2']};color:white;"
                             f"border-color:{C['accent2']};}}QPushButton:hover{{background:{C['border']};}}")
            self._ch_grp.addButton(rb); self._ch_btns[i] = rb
            rb.clicked.connect(lambda checked,v=i: self._cmd(f'ch {v}') if checked else None)
            crow.addWidget(rb)
        self._ch_btns[0].setChecked(True)
        crow.addStretch(); tgl.addLayout(crow)

        # Power
        prow2 = QHBoxLayout(); prow2.setSpacing(4)
        prow2.addWidget(lbl('Power:', color=C['text_dim'], bold=True))
        self._pwr_btns = {}; self._pwr_grp = QButtonGroup(self)
        for i in range(1,5):
            rb = QPushButton(f'PWR{i}'); rb.setCheckable(True)
            rb.setStyleSheet(f"QPushButton{{background:{C['surface2']};color:{C['text']};border:1px solid {C['border']};"
                             f"border-radius:5px;padding:5px 12px;}}QPushButton:checked{{background:{C['warning']};color:white;"
                             f"border-color:{C['warning']};}}QPushButton:hover{{background:{C['border']};}}")
            self._pwr_grp.addButton(rb); self._pwr_btns[i] = rb
            rb.clicked.connect(lambda checked,v=i: self._cmd(f'pwr {v}') if checked else None)
            prow2.addWidget(rb)
        self._pwr_btns[3].setChecked(True)
        prow2.addStretch(); tgl.addLayout(prow2)

        # Pulse / Period
        timing = QHBoxLayout(); timing.setSpacing(12)
        timing.addWidget(lbl('Pulse:', color=C['text_dim']))
        self._pulse_sp = spin(1, 60000, 23, ' ms')
        timing.addWidget(self._pulse_sp)
        bp = btn('Set', BTN_NEUTRAL); bp.setFixedWidth(40)
        bp.clicked.connect(lambda: self._cmd(f'pulse {self._pulse_sp.value()}'))
        timing.addWidget(bp)

        timing.addWidget(lbl('   Period:', color=C['text_dim']))
        self._period_sp = spin(100, 3600000, 2000, ' ms')
        self._period_sp.setFixedWidth(100)
        timing.addWidget(self._period_sp)
        bpr = btn('Set', BTN_NEUTRAL); bpr.setFixedWidth(40)
        bpr.clicked.connect(lambda: self._cmd(f'period {self._period_sp.value()}'))
        timing.addWidget(bpr)
        timing.addStretch(); tgl.addLayout(timing)

        tgl.addWidget(sep_h())

        # TX on/off
        trow = QHBoxLayout(); trow.setSpacing(10)
        b_on  = btn('▶ TX ON',  BTN_SUCCESS); b_on.setFixedHeight(36)
        b_off = btn('■ TX OFF', BTN_DANGER);  b_off.setFixedHeight(36)
        b_on.clicked.connect(lambda: self._cmd('tx on'))
        b_off.clicked.connect(lambda: self._cmd('tx off'))
        trow.addWidget(b_on); trow.addWidget(b_off); trow.addStretch()

        self._badge_tx_big = QLabel('TX —')
        self._badge_tx_big.setStyleSheet(f"color:{C['text_dim']};background:{C['surface2']};"
                                          f"border:1px solid {C['border']};border-radius:6px;"
                                          f"padding:4px 14px;font-size:12px;font-weight:600;")
        trow.addWidget(self._badge_tx_big)
        tgl.addLayout(trow)

        v.addWidget(tx_g)

        # ── Schedule ──────────────────────────────────────────────────────────
        sc_g = group('Schedule')
        scl = QVBoxLayout(sc_g); scl.setSpacing(6)

        self._sched_cb = QCheckBox('Enable schedule  (unchecked = TX always active)')
        self._sched_cb.setStyleSheet(f"color:{C['text']};font-weight:600;")
        self._sched_cb.toggled.connect(self._on_sched_toggle)
        scl.addWidget(self._sched_cb)

        # Hours grid
        scl.addWidget(lbl('Active hours:', color=C['text_dim']))
        hgrid = QGridLayout(); hgrid.setSpacing(2)
        self._hour_cbs = []
        for h in range(24):
            r, c = divmod(h, 8)
            cb = QCheckBox(f'{h:02d}')
            cb.setEnabled(False)
            cb.setStyleSheet("font-family:Consolas;font-size:9px;")
            cb.toggled.connect(self._on_hour_change)
            hgrid.addWidget(cb, r, c)
            self._hour_cbs.append(cb)
        scl.addLayout(hgrid)

        # Hour preset buttons
        hprow = QHBoxLayout(); hprow.setSpacing(4)
        for ltext, hrs in [('All',list(range(24))),('None',[]),('Day 8–20',list(range(8,21))),('Night 22–6',[22,23,0,1,2,3,4,5,6])]:
            b = btn(ltext, BTN_NEUTRAL); b.setFixedHeight(24)
            b.clicked.connect(lambda checked=False, hh=hrs: self._set_hours(hh))
            hprow.addWidget(b)
        hprow.addStretch(); scl.addLayout(hprow)

        # Days
        scl.addWidget(lbl('Active days:', color=C['text_dim']))
        drow = QHBoxLayout(); drow.setSpacing(4)
        self._day_cbs = []
        for d, dn in enumerate(_DAY_NAMES):
            cb = QCheckBox(dn); cb.setEnabled(False)
            cb.setStyleSheet("font-family:Consolas;font-size:9px;")
            cb.toggled.connect(self._on_day_change); drow.addWidget(cb)
            self._day_cbs.append(cb)
        drow.addStretch()
        dprow = QHBoxLayout(); dprow.setSpacing(4)
        for ltext, days in [('All',list(range(7))),('Weekdays',list(range(5))),('Weekend',[5,6])]:
            b = btn(ltext, BTN_NEUTRAL); b.setFixedHeight(24)
            b.clicked.connect(lambda checked=False, dd=days: self._set_days(dd))
            dprow.addWidget(b)
        dprow.addStretch()
        scl.addLayout(drow); scl.addLayout(dprow)

        # Months
        scl.addWidget(lbl('Active months:', color=C['text_dim']))
        mrow_l = QHBoxLayout(); mrow_l.setSpacing(4)
        self._month_cbs = []
        for m, mn in enumerate(_MONTH_NAMES):
            cb = QCheckBox(mn); cb.setEnabled(False)
            cb.setStyleSheet("font-family:Consolas;font-size:9px;")
            cb.toggled.connect(self._on_month_change); mrow_l.addWidget(cb)
            self._month_cbs.append(cb)
        mrow_l.addStretch()
        mprow = QHBoxLayout(); mprow.setSpacing(4)
        for ltext, months in [('All',list(range(12))),('Field (Apr–Sep)',list(range(3,9)))]:
            b = btn(ltext, BTN_NEUTRAL); b.setFixedHeight(24)
            b.clicked.connect(lambda checked=False, mm=months: self._set_months(mm))
            mprow.addWidget(b)
        mprow.addStretch()
        scl.addLayout(mrow_l); scl.addLayout(mprow)

        sched_row = QHBoxLayout()
        self._sched_status_lbl = QLabel('●  —')
        self._sched_status_lbl.setStyleSheet(f"color:{C['text_dim']};font-weight:600;font-size:11px;")
        sched_row.addWidget(self._sched_status_lbl)
        sched_row.addStretch()
        b_sched_read = btn('↺ Read from MCU', BTN_NEUTRAL)
        b_sched_read.clicked.connect(lambda: self._cmd('sched show'))
        sched_row.addWidget(b_sched_read)
        scl.addLayout(sched_row)

        v.addWidget(sc_g)
        v.addStretch()

    def _set_hours(self, hrs):
        for i,cb in enumerate(self._hour_cbs): cb.setChecked(i in hrs)
    def _set_days(self, days):
        for i,cb in enumerate(self._day_cbs): cb.setChecked(i in days)
    def _set_months(self, months):
        for i,cb in enumerate(self._month_cbs): cb.setChecked(i in months)

    def _on_sched_toggle(self, en):
        self._sched_enabled = en
        for cb in self._hour_cbs:   cb.setEnabled(en)
        for cb in self._day_cbs:    cb.setEnabled(en)
        for cb in self._month_cbs:  cb.setEnabled(en)
        if not en:
            self._cmd('sched off')
        else:
            self._on_hour_change(); self._on_day_change(); self._on_month_change()

    def _on_hour_change(self):
        if not self._sched_enabled: return
        hrs = [h for h,cb in enumerate(self._hour_cbs) if cb.isChecked()]
        self._cmd('sched hours ' + ' '.join(str(h) for h in hrs) if hrs else 'sched hours')

    def _on_day_change(self):
        if not self._sched_enabled: return
        days = [d+1 for d,cb in enumerate(self._day_cbs) if cb.isChecked()]
        self._cmd('sched days ' + ' '.join(str(d) for d in days) if days else 'sched days')

    def _on_month_change(self):
        if not self._sched_enabled: return
        months = [m+1 for m,cb in enumerate(self._month_cbs) if cb.isChecked()]
        self._cmd('sched months ' + ' '.join(str(m) for m in months) if months else 'sched months')

    # ── Tab 2: Flash Log ──────────────────────────────────────────────────────
    def _build_tab_flash_log(self):
        w = QWidget(); self._tabs.addTab(w, '💾 Flash Log')
        v = QVBoxLayout(w); v.setContentsMargins(10,10,10,10); v.setSpacing(8)

        # ── Status bar ────────────────────────────────────────────────────────
        st_g = group('Log Status')
        stl = QVBoxLayout(st_g); stl.setSpacing(4)

        info_row = QHBoxLayout()
        self._log_used_lbl = QLabel('Used: —/768 entries')
        self._log_used_lbl.setStyleSheet(f"color:{C['text']};font-weight:600;")
        info_row.addWidget(self._log_used_lbl)
        self._log_mode_lbl = QLabel('Mode: —  |  Write: —  |  Mask: —')
        self._log_mode_lbl.setStyleSheet(f"color:{C['text_dim']};")
        info_row.addWidget(self._log_mode_lbl, 1)
        stl.addLayout(info_row)

        self._log_pbar = QProgressBar(); self._log_pbar.setRange(0,100); self._log_pbar.setValue(0)
        stl.addWidget(self._log_pbar)

        btn_row = QHBoxLayout(); btn_row.setSpacing(6)
        for txt, cmd in [('↻ Refresh Info','log info'),('Get Config','log get'),
                         ('Calc','log calc'),('Write Now','log write'),('Pages','log pages')]:
            b = btn(txt, BTN_NEUTRAL); b.setFixedHeight(28)
            b.clicked.connect(lambda c=False, cm=cmd: self._cmd(cm)); btn_row.addWidget(b)
        b_clr = btn('🗑 Clear All', BTN_DANGER); b_clr.setFixedHeight(28)
        b_clr.clicked.connect(self._log_clear_confirm); btn_row.addWidget(b_clr)
        btn_row.addStretch(); stl.addLayout(btn_row)
        v.addWidget(st_g)

        # ── Config + Calculator side by side ─────────────────────────────────
        mid = QHBoxLayout(); mid.setSpacing(8)

        # Log Config
        cfg_g = group('Log Configuration')
        cfgl = QVBoxLayout(cfg_g); cfgl.setSpacing(6)

        mask_row = QHBoxLayout()
        mask_row.addWidget(lbl('Fields:', color=C['text_dim']))
        self._lm_temp  = QCheckBox('Temp');  self._lm_light = QCheckBox('Light')
        self._lm_batp  = QCheckBox('Bat%');  self._lm_batmv = QCheckBox('BatmV')
        self._lm_temp.setChecked(True); self._lm_batp.setChecked(True)
        for cb in [self._lm_temp, self._lm_light, self._lm_batp, self._lm_batmv]:
            mask_row.addWidget(cb)
        b_smask = btn('Set Mask', BTN_NEUTRAL); b_smask.clicked.connect(self._log_set_mask)
        mask_row.addWidget(b_smask); mask_row.addStretch()
        cfgl.addLayout(mask_row)

        # Intervals
        iv_grid = QGridLayout(); iv_grid.setSpacing(4)
        iv_grid.addWidget(lbl('Sensor', color=C['text_dim'], size=9), 0, 0)
        iv_grid.addWidget(lbl('Read (driver) s', color='#7ec8e3', size=9), 0, 1)
        iv_grid.addWidget(lbl('→', color=C['text_muted'], size=9), 0, 2)
        iv_grid.addWidget(lbl('Write (flash) s', color=C['purple'], size=9), 0, 3)

        self._temp_period_sp  = spin(1,65535,1);   self._log_dt_sp = spin(1,65535,60)
        self._batt_period_sp  = spin(1,65535,5);   self._log_db_sp = spin(1,65535,3600)
        self._light_period_sp = spin(1,65535,5);   self._log_dl_sp = spin(1,65535,300)

        for r, (name, rsp, rc, wsp, wc) in enumerate([
            ('Temp',  self._temp_period_sp,  'temp period',  self._log_dt_sp,  'log temp'),
            ('Bat',   self._batt_period_sp,  'batt period',  self._log_db_sp,  'log bat'),
            ('Light', self._light_period_sp, 'light period', self._log_dl_sp,  'log light'),
        ], start=1):
            iv_grid.addWidget(lbl(name+':', color=C['text_dim'], size=9), r, 0)
            iv_grid.addWidget(rsp, r, 1)
            br = btn('Set', BTN_NEUTRAL); br.setFixedSize(36,24)
            br.clicked.connect(lambda c=False, s=rsp, k=rc: self._cmd(f'{k} {s.value()}'))
            iv_grid.addWidget(br, r, 1, 1, 1)
            iv_grid.addWidget(lbl('→', color=C['text_muted']), r, 2)
            iv_grid.addWidget(wsp, r, 3)
            bw = btn('Set', BTN_NEUTRAL); bw.setFixedSize(36,24)
            bw.clicked.connect(lambda c=False, s=wsp, k=wc: self._cmd(f'{k} {s.value()}'))
            iv_grid.addWidget(bw, r, 4)
        cfgl.addLayout(iv_grid)

        # Write mode / overflow / CKP
        mode_row = QHBoxLayout(); mode_row.setSpacing(6)
        mode_row.addWidget(lbl('Write:', color=C['text_dim']))
        self._log_mode_cb = QComboBox(); self._log_mode_cb.addItems(['always','onchange','adaptive'])
        self._log_mode_cb.setFixedWidth(100); mode_row.addWidget(self._log_mode_cb)
        bwm = btn('Set', BTN_NEUTRAL); bwm.setFixedSize(36,24)
        bwm.clicked.connect(lambda: self._cmd(f'log mode {self._log_mode_cb.currentIndex()}'))
        mode_row.addWidget(bwm)
        mode_row.addWidget(lbl('  Oflow:', color=C['text_dim']))
        self._log_oflow_cb = QComboBox(); self._log_oflow_cb.addItems(['circular','stop'])
        self._log_oflow_cb.setFixedWidth(90); mode_row.addWidget(self._log_oflow_cb)
        bof = btn('Set', BTN_NEUTRAL); bof.setFixedSize(36,24)
        bof.clicked.connect(lambda: self._cmd(f'log overflow {1 if self._log_oflow_cb.currentIndex()==0 else 0}'))
        mode_row.addWidget(bof)
        mode_row.addWidget(lbl('  CKP/N:', color=C['text_dim']))
        self._log_ckp_sp = spin(2,255,16); mode_row.addWidget(self._log_ckp_sp)
        bck = btn('Set', BTN_NEUTRAL); bck.setFixedSize(36,24)
        bck.clicked.connect(lambda: self._cmd(f'log ckp {self._log_ckp_sp.value()}'))
        mode_row.addWidget(bck); mode_row.addStretch()
        cfgl.addLayout(mode_row)

        # Timestamp
        ts_row = QHBoxLayout()
        ts_row.addWidget(lbl('Timestamp:', color=C['text_dim']))
        self._ts_boot_rb = QRadioButton('Boot'); self._ts_rtc_rb = QRadioButton('RTC')
        self._ts_boot_rb.setChecked(True)
        ts_row.addWidget(self._ts_boot_rb); ts_row.addWidget(self._ts_rtc_rb)
        bts = btn('Set', BTN_NEUTRAL); bts.setFixedSize(36,24)
        bts.clicked.connect(lambda: self._cmd(f'log ts {"rtc" if self._ts_rtc_rb.isChecked() else "boot"}'))
        ts_row.addWidget(bts); ts_row.addStretch()
        cfgl.addLayout(ts_row)

        bvfy = btn('✔ Write All & Verify', BTN_SUCCESS); bvfy.setFixedHeight(30)
        bvfy.clicked.connect(self._log_write_verify)
        cfgl.addWidget(bvfy)
        cfgl.addStretch()

        mid.addWidget(cfg_g, 1)

        # ── Calculator ────────────────────────────────────────────────────────
        calc_g = group('Flash Memory Calculator')
        cll = QVBoxLayout(calc_g); cll.setSpacing(6)

        cr0 = QHBoxLayout()
        b_imp = btn('◀ Import config', BTN_PRIMARY); b_imp.clicked.connect(self._calc_import)
        cr0.addWidget(b_imp)
        cr0.addWidget(lbl('Total:', color=C['text_dim']))
        self._calc_total_sp = spin(1,768,768,' rec')
        cr0.addWidget(self._calc_total_sp)
        cr0.addWidget(lbl('Free:', color=C['text_dim']))
        self._calc_free_sp = spin(0,768,768,' rec')
        cr0.addWidget(self._calc_free_sp); cr0.addStretch()
        cll.addLayout(cr0)

        # Sensor enable + interval
        self._calc_temp_cb  = QCheckBox('Temp');  self._calc_temp_cb.setChecked(True)
        self._calc_bat_cb   = QCheckBox('Bat');   self._calc_bat_cb.setChecked(True)
        self._calc_light_cb = QCheckBox('Light')
        self._calc_temp_sp  = spin(1,86400,60,' s');  self._calc_temp_sp.setFixedWidth(80)
        self._calc_bat_sp   = spin(1,86400,3600,' s'); self._calc_bat_sp.setFixedWidth(80)
        self._calc_light_sp = spin(1,86400,300,' s');  self._calc_light_sp.setFixedWidth(80)
        for cb, sp in [(self._calc_temp_cb, self._calc_temp_sp),
                       (self._calc_bat_cb,  self._calc_bat_sp),
                       (self._calc_light_cb,self._calc_light_sp)]:
            r = QHBoxLayout()
            r.addWidget(cb); r.addWidget(lbl('every', color=C['text_dim']))
            r.addWidget(sp); r.addStretch(); cll.addLayout(r)

        wr_row = QHBoxLayout()
        wr_row.addWidget(lbl('Write:', color=C['text_dim']))
        self._calc_mode_cb = QComboBox(); self._calc_mode_cb.addItems(['always','onchange','adaptive'])
        self._calc_mode_cb.setFixedWidth(100); wr_row.addWidget(self._calc_mode_cb)
        wr_row.addWidget(lbl('  Compr:', color=C['text_dim']))
        self._calc_eff_sp = spin(1,100,100,' %'); wr_row.addWidget(self._calc_eff_sp)
        wr_row.addStretch(); cll.addLayout(wr_row)

        cll.addWidget(sep_h())

        # Results
        res_grid = QGridLayout(); res_grid.setSpacing(4)
        self._calc_rph_lbl    = QLabel('—'); self._calc_hrs_lbl = QLabel('—')
        self._calc_days_lbl   = QLabel('—')
        self._calc_budget_lbl = QLabel('—'); self._calc_sugg_lbl = QLabel('—')
        for vl, col in [(self._calc_rph_lbl, C['accent']), (self._calc_hrs_lbl, C['success']),
                        (self._calc_days_lbl, C['success']), (self._calc_budget_lbl, C['purple']),
                        (self._calc_sugg_lbl, C['warning'])]:
            vl.setStyleSheet(f"color:{col};font-family:Consolas;font-size:11px;font-weight:600;"
                             f"background:{C['surface2']};border:1px solid {C['border']};"
                             f"border-radius:4px;padding:2px 6px;")
        res_grid.addWidget(lbl('Records/hr:', color=C['text_dim']), 0, 0)
        res_grid.addWidget(self._calc_rph_lbl, 0, 1)
        res_grid.addWidget(lbl('Full in:', color=C['text_dim']), 1, 0)
        res_grid.addWidget(self._calc_hrs_lbl, 1, 1)
        res_grid.addWidget(self._calc_days_lbl, 1, 2)
        cll.addLayout(res_grid)

        tgt_row = QHBoxLayout(); tgt_row.setSpacing(4)
        tgt_row.addWidget(lbl('Fill', color=C['text_dim']))
        self._calc_pct_sp = spin(1,100,90,' %')
        tgt_row.addWidget(self._calc_pct_sp)
        tgt_row.addWidget(lbl('in', color=C['text_dim']))
        self._calc_days_sp = spin(1,9999,7,' d')
        tgt_row.addWidget(self._calc_days_sp)
        tgt_row.addWidget(self._calc_budget_lbl)
        tgt_row.addWidget(lbl('→ interval ≥', color=C['text_dim']))
        tgt_row.addWidget(self._calc_sugg_lbl); tgt_row.addStretch()
        cll.addLayout(tgt_row)

        b_calc = btn('Calculate', BTN_PRIMARY); b_calc.clicked.connect(self._calc_run)
        cll.addWidget(b_calc)
        cll.addWidget(lbl('16 bytes/record, 768 max records (6×2KB pages)', color=C['text_muted'], size=8))
        cll.addStretch()

        mid.addWidget(calc_g, 1)
        v.addLayout(mid)

        # ── Data viewer ───────────────────────────────────────────────────────
        data_g = group('Log Data')
        dgl = QVBoxLayout(data_g); dgl.setSpacing(6)

        dc = QHBoxLayout(); dc.setSpacing(6)
        b_dump = btn('⬇ Dump All', BTN_PRIMARY); b_dump.clicked.connect(self._log_dump_all)
        dc.addWidget(b_dump)
        dc.addWidget(lbl('From:', color=C['text_dim']))
        self._log_from_sp  = spin(0,767,0); dc.addWidget(self._log_from_sp)
        dc.addWidget(lbl('Count(0=all):', color=C['text_dim']))
        self._log_count_sp = spin(0,768,0); dc.addWidget(self._log_count_sp)
        b_rng = btn('Dump', BTN_NEUTRAL); b_rng.clicked.connect(self._log_dump_range)
        dc.addWidget(b_rng)
        dc.addWidget(lbl('  Rec#:', color=C['text_dim']))
        self._log_get_sp = spin(0,767,0); dc.addWidget(self._log_get_sp)
        b_read = btn('Read', BTN_NEUTRAL)
        b_read.clicked.connect(lambda: self._cmd(f'log read {self._log_get_sp.value()}'))
        dc.addWidget(b_read)
        dc.addStretch()
        self._log_dl_pbar = QProgressBar(); self._log_dl_pbar.setRange(0,100)
        self._log_dl_pbar.setFixedWidth(120); self._log_dl_pbar.setVisible(False)
        dc.addWidget(self._log_dl_pbar)
        dgl.addLayout(dc)

        self._log_table = QTableWidget(0, 7)
        self._log_table.setHorizontalHeaderLabels(['idx','ts','flags','temp°C','light','bat%','batmV'])
        self._log_table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        self._log_table.setAlternatingRowColors(True)
        self._log_table.setStyleSheet(f"QTableWidget::item:alternate{{background:{C['surface2']};}}")
        self._log_table.setMinimumHeight(150)
        dgl.addWidget(self._log_table, 1)

        btn_row2 = QHBoxLayout(); btn_row2.setSpacing(6)
        b_csv = btn('📤 Export CSV', BTN_NEUTRAL); b_csv.clicked.connect(self._log_save_csv)
        b_clrt = btn('Clear Table', BTN_NEUTRAL); b_clrt.clicked.connect(self._log_clear_table)
        self._log_ts_date_cb = QCheckBox('ts → datetime (RTC mode)')
        self._log_ts_date_cb.toggled.connect(self._log_refresh_ts)
        btn_row2.addWidget(b_csv); btn_row2.addWidget(b_clrt)
        btn_row2.addWidget(self._log_ts_date_cb); btn_row2.addStretch()
        dgl.addLayout(btn_row2)

        if _HAS_PG:
            chart_row = QHBoxLayout(); chart_row.setSpacing(6)
            chart_row.addWidget(lbl('Charts:', color=C['text_dim']))
            for label, which in [('Temp','temp'),('Battery','bat'),('Light','light'),('All','all')]:
                b = btn(label, BTN_NEUTRAL)
                b.clicked.connect(lambda c=False, wh=which: self._log_plot_window(wh))
                chart_row.addWidget(b)
            chart_row.addStretch(); dgl.addLayout(chart_row)

        v.addWidget(data_g, 1)

    def _log_clear_confirm(self):
        r = QMessageBox.question(self, 'Clear Log',
            'Erase ALL log data from flash?\nThis cannot be undone.',
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if r == QMessageBox.StandardButton.Yes: self._cmd('log clear yes')

    def _log_set_mask(self):
        mask = ((0x01 if self._lm_temp.isChecked()  else 0) |
                (0x02 if self._lm_light.isChecked() else 0) |
                (0x04 if self._lm_batp.isChecked()  else 0) |
                (0x08 if self._lm_batmv.isChecked() else 0))
        self._cmd(f'log mask {mask:02X}')

    def _log_dump_all(self):
        self._log_in_dump = True; self._log_csv_buf = []
        self._log_table.setRowCount(0)
        self._log_dl_pbar.setVisible(True); self._log_dl_pbar.setValue(0)
        self._cmd('log dump')

    def _log_dump_range(self):
        fr = self._log_from_sp.value(); ct = self._log_count_sp.value()
        self._log_in_dump = True; self._log_csv_buf = []
        self._log_table.setRowCount(0)
        self._cmd(f'log read {fr} {ct}')

    def _log_clear_table(self):
        self._log_table.setRowCount(0); self._log_csv_buf = []

    @staticmethod
    def _epoch2000_to_str(ts_s):
        try:
            epoch = ts_s + 946684800
            return datetime.datetime.utcfromtimestamp(epoch).strftime('%Y-%m-%d %H:%M:%S')
        except: return str(ts_s)

    def _ts_display(self, raw):
        if self._log_ts_date_cb.isChecked():
            try: return self._epoch2000_to_str(int(raw.strip()))
            except: pass
        return raw.strip()

    def _log_refresh_ts(self):
        for row_i, parts in enumerate(self._log_csv_buf):
            if len(parts) > 1:
                item = self._log_table.item(row_i, 1)
                if item: item.setText(self._ts_display(parts[1]))

    def _log_append_csv_row(self, s):
        parts = s.split(',')
        if len(parts) < 7: return
        self._log_csv_buf.append(parts)
        row_i = self._log_table.rowCount()
        self._log_table.insertRow(row_i)
        flag = parts[2].strip()
        for ci, val in enumerate([parts[0].strip(),
                                   self._ts_display(parts[1]),
                                   flag,
                                   parts[3].strip(), parts[4].strip(),
                                   parts[5].strip(), parts[6].strip()]):
            item = QTableWidgetItem(val)
            item.setFlags(Qt.ItemFlag.ItemIsSelectable | Qt.ItemFlag.ItemIsEnabled)
            if 'CKP' in flag or 'CHECKPOINT' in flag:
                item.setForeground(QColor(C['success'])); item.setFont(QFont('Consolas',9,QFont.Weight.Bold))
            elif 'DELTA' in flag:
                item.setForeground(QColor(C['warning']))
                f = QFont('Consolas',9); f.setItalic(True); item.setFont(f)
            else:
                item.setForeground(QColor(C['text']))
            self._log_table.setItem(row_i, ci, item)
        self._log_table.scrollToBottom()
        if self._log_total > 0:
            pct = int(len(self._log_csv_buf) * 100 / max(self._log_total, 1))
            self._log_dl_pbar.setValue(min(pct, 100))

    def _log_save_csv(self):
        if not self._log_csv_buf:
            QMessageBox.information(self, 'Save CSV', 'No data. Run a dump first.'); return
        fname, _ = QFileDialog.getSaveFileName(self, 'Save flash log as CSV', '',
                                               'CSV files (*.csv);;All files (*.*)')
        if not fname: return
        try:
            with open(fname, 'w', newline='') as f:
                f.write('idx,ts_s,flags,temp_c,light_raw,bat_pct,bat_mv\n')
                for row in self._log_csv_buf:
                    f.write(','.join(p.strip() for p in row) + '\n')
            QMessageBox.information(self, 'Save CSV',
                                    f'Saved {len(self._log_csv_buf)} records to:\n{fname}')
        except Exception as e:
            QMessageBox.critical(self, 'Save CSV', str(e))

    def _log_plot_window(self, which):
        if not self._log_csv_buf:
            QMessageBox.information(self, 'Plot', 'No data. Run a dump first.'); return
        use_rtc = self._ts_rtc_rb.isChecked()
        ts_l, temp_l, light_l, bat_mv_l, bat_pct_l = [], [], [], [], []
        for row in self._log_csv_buf:
            if len(row) < 7: continue
            try:
                raw_ts = int(row[1].strip())
                ts_l.append(datetime.datetime.utcfromtimestamp(raw_ts + 946684800) if use_rtc else raw_ts)
                temp_l.append(float(row[3].strip()) if row[3].strip() else None)
                light_l.append(float(row[4].strip()) if row[4].strip() else None)
                bat_pct_l.append(float(row[5].strip()) if row[5].strip() else None)
                bat_mv_l.append(float(row[6].strip()) if row[6].strip() else None)
            except: continue
        if not ts_l: return
        dlg = QDialog(self); dlg.setWindowTitle(f'Flash Log — {which.capitalize()}')
        dlg.resize(900, 500)
        dlg.setStyleSheet(APP_SS)
        v2 = QVBoxLayout(dlg)
        ts_idx = list(range(len(ts_l)))
        def make_plot(title, ys, color, yunit):
            pw = pg.PlotWidget(title=title)
            pw.setLabel('left', title, units=yunit)
            pw.showGrid(x=True, y=True, alpha=0.15)
            pts = [(i,y) for i,y in zip(ts_idx, ys) if y is not None]
            if pts:
                xi,yi = zip(*pts)
                pw.plot(list(xi), list(yi), pen=pg.mkPen(color, width=1.5))
            return pw
        if which == 'all':
            sp = QSplitter(Qt.Orientation.Vertical)
            sp.addWidget(make_plot('Temperature',temp_l,C['blue'],'°C'))
            sp.addWidget(make_plot('Light',light_l,C['yellow'],'raw'))
            sp.addWidget(make_plot('Battery',bat_mv_l,C['success'],'mV'))
            v2.addWidget(sp)
        elif which == 'temp': v2.addWidget(make_plot('Temperature', temp_l, C['blue'], '°C'))
        elif which == 'bat':  v2.addWidget(make_plot('Battery mV', bat_mv_l, C['success'], 'mV'))
        else:                 v2.addWidget(make_plot('Light', light_l, C['yellow'], 'raw'))
        dlg.exec()

    # ── Calculator ────────────────────────────────────────────────────────────
    def _calc_import(self):
        self._calc_total_sp.setValue(768)
        self._calc_free_sp.setValue(min(self._log_free, 768))
        self._calc_temp_cb.setChecked(self._lm_temp.isChecked())
        self._calc_bat_cb.setChecked(self._lm_batp.isChecked())
        self._calc_light_cb.setChecked(self._lm_light.isChecked())
        self._calc_temp_sp.setValue(self._log_dt_sp.value())
        self._calc_bat_sp.setValue(self._log_db_sp.value())
        self._calc_light_sp.setValue(self._log_dl_sp.value())
        self._calc_mode_cb.setCurrentIndex(self._log_mode_cb.currentIndex())
        self._calc_run()

    def _calc_run(self):
        try:
            total = max(1, self._calc_total_sp.value())
            free  = max(0, min(self._calc_free_sp.value(), total))
            mode  = self._calc_mode_cb.currentText()
            eff   = max(1.0, min(100.0, float(self._calc_eff_sp.value()))) / 100.0
            sensors = []
            for cb, sp in [(self._calc_temp_cb, self._calc_temp_sp),
                           (self._calc_bat_cb,  self._calc_bat_sp),
                           (self._calc_light_cb, self._calc_light_sp)]:
                if cb.isChecked() and sp.value() > 0:
                    sensors.append(sp.value())
            if not sensors:
                for w in [self._calc_rph_lbl, self._calc_hrs_lbl, self._calc_days_lbl,
                           self._calc_budget_lbl, self._calc_sugg_lbl]:
                    w.setText('—')
                return
            min_iv  = min(sensors)
            rph_raw = 3600.0 / min_iv
            rph     = rph_raw if mode == 'always' else rph_raw * eff
            self._calc_rph_lbl.setText(f'{rph:.1f}')

            def _fmt(hours):
                if hours >= 24:
                    d, h = divmod(hours, 24)
                    return f'{hours:.1f}h ({d:.0f}d {h:.0f}h)'
                return f'{hours:.1f}h'

            if rph > 0:
                hrs = free / rph
                self._calc_hrs_lbl.setText(_fmt(hrs))
                self._calc_days_lbl.setText(f'{hrs/24:.2f} days')
            else:
                self._calc_hrs_lbl.setText('∞'); self._calc_days_lbl.setText('∞')

            tgt_pct  = max(1.0, min(100.0, float(self._calc_pct_sp.value()))) / 100.0
            tgt_days = max(0.001, float(self._calc_days_sp.value()))
            budget   = int(total * tgt_pct)
            budget_rph = budget / (tgt_days * 24.0)
            self._calc_budget_lbl.setText(f'{budget} rec → {budget_rph:.1f}/hr')
            if budget_rph > 0:
                sugg_s = (3600.0 * eff) / budget_rph
                if sugg_s >= 3600: sugg = f'{sugg_s:.0f}s (~{sugg_s/3600:.1f}h)'
                elif sugg_s >= 60: sugg = f'{sugg_s:.0f}s (~{sugg_s/60:.0f}min)'
                else:              sugg = f'{sugg_s:.0f}s'
                self._calc_sugg_lbl.setText(sugg)
            else: self._calc_sugg_lbl.setText('∞')
        except (ValueError, ZeroDivisionError):
            for w in [self._calc_rph_lbl, self._calc_hrs_lbl, self._calc_days_lbl,
                       self._calc_budget_lbl, self._calc_sugg_lbl]:
                w.setText('ERR')

    def _log_write_verify(self):
        if not self._worker.is_open():
            QMessageBox.warning(self, 'Not connected', 'Connect to a COM port first'); return
        mask = ((0x01 if self._lm_temp.isChecked()  else 0) |
                (0x02 if self._lm_light.isChecked() else 0) |
                (0x04 if self._lm_batp.isChecked()  else 0) |
                (0x08 if self._lm_batmv.isChecked() else 0))
        mode_n  = self._log_mode_cb.currentIndex()
        oflow_n = 0 if self._log_oflow_cb.currentIndex() == 1 else 1
        ts_n    = 1 if self._ts_rtc_rb.isChecked() else 0
        self._verify_snapshot = {
            'mask': mask, 'mode': mode_n, 'oflow': oflow_n,
            'ckp': self._log_ckp_sp.value(), 'ts': ts_n,
            'temp': self._log_dt_sp.value(),
            'light': self._log_dl_sp.value(),
            'bat': self._log_db_sp.value(),
        }
        self._verify_received = {}
        self._cmd(f'log mask {mask:02X}')
        self._cmd(f'log mode {mode_n}')
        self._cmd(f'log overflow {oflow_n}')
        self._cmd(f'log ckp {self._log_ckp_sp.value()}')
        self._cmd(f'log ts {"rtc" if ts_n else "boot"}')
        self._cmd(f'log temp {self._log_dt_sp.value()}')
        self._cmd(f'log light {self._log_dl_sp.value()}')
        self._cmd(f'log bat {self._log_db_sp.value()}')
        self._verify_active = True
        QTimer.singleShot(900, lambda: self._cmd('log get'))

    def _log_flash_verify_result(self, results):
        all_ok = all(results.values())
        if all_ok:
            QMessageBox.information(self, 'Write & Verify', 'All settings written and confirmed ✓')
        else:
            failed = [k for k,v in results.items() if not v]
            QMessageBox.warning(self, 'Write & Verify',
                f'Written, but MCU reported different values for:\n{", ".join(failed)}')

    # ── Tab 4: Terminal ───────────────────────────────────────────────────────
    def _build_tab_terminal(self):
        w = QWidget(); self._tabs.addTab(w, '🖥️ Terminal')
        v = QVBoxLayout(w); v.setContentsMargins(8,8,8,8); v.setSpacing(6)

        self._term = QTextEdit()
        self._term.setReadOnly(True)
        self._term.setFont(QFont('Consolas', 9))
        self._term.setStyleSheet(f"background:#0d1117;color:{C['text']};border:1px solid {C['border']};border-radius:6px;")
        self._term.document().setMaximumBlockCount(2000)
        v.addWidget(self._term, 1)

        # Quick commands
        qc = QHBoxLayout(); qc.setSpacing(4)
        qc.addWidget(lbl('Quick:', color=C['text_dim']))
        for cmd in ['status','log info','log get','log calc','log dump','save','regs','reset']:
            b = btn(cmd, BTN_NEUTRAL)
            b.setFixedHeight(26)
            b.clicked.connect(lambda checked=False, c=cmd: self._cmd(c))
            qc.addWidget(b)
        qc.addStretch()
        b_clr = btn('Clear', BTN_NEUTRAL); b_clr.setFixedHeight(26)
        b_clr.clicked.connect(self._term.clear); qc.addWidget(b_clr)
        v.addLayout(qc)

        # Input line
        inp = QHBoxLayout(); inp.setSpacing(6)
        self._term_input = QLineEdit()
        self._term_input.setPlaceholderText('type command here…')
        self._term_input.setStyleSheet(f"background:{C['surface2']};border:1px solid {C['border']};"
                                        f"border-radius:6px;padding:4px 8px;font-family:Consolas;")
        self._term_input.returnPressed.connect(self._term_send)
        inp.addWidget(self._term_input, 1)
        b_snd = btn('Send', BTN_PRIMARY); b_snd.clicked.connect(self._term_send)
        inp.addWidget(b_snd)
        v.addLayout(inp)

    # ── Tab 3: Settings ───────────────────────────────────────────────────────
    def _build_tab_settings(self):
        w = QScrollArea(); w.setWidgetResizable(True)
        self._tabs.addTab(w, '⚙️ Settings')
        inner = QWidget(); w.setWidget(inner)
        v = QVBoxLayout(inner); v.setContentsMargins(10,10,10,10); v.setSpacing(8)

        # Temperature
        tg = group('Temperature Sensor'); tgl = QVBoxLayout(tg)
        tr0 = QHBoxLayout(); tr0.addWidget(lbl('Mode:', color=C['text_dim']))
        self._temp_mode_rb = {m: QRadioButton(m) for m in ['off','periodic','tx']}
        self._temp_mode_rb['off'].setChecked(True)
        for m,rb in self._temp_mode_rb.items():
            rb.toggled.connect(lambda checked,mv=m: self._cmd(f'temp mode {mv}') if checked else None)
            tr0.addWidget(rb)
        tr0.addStretch(); tgl.addLayout(tr0)
        tr1 = QHBoxLayout()
        tr1.addWidget(lbl('Period:', color=C['text_dim']))
        self._temp_period_sp2 = spin(1,255,1,' s')
        tr1.addWidget(self._temp_period_sp2)
        bt = btn('Set', BTN_NEUTRAL); bt.setFixedSize(40,24)
        bt.clicked.connect(lambda: self._cmd(f'temp period {self._temp_period_sp2.value()}'))
        tr1.addWidget(bt)
        tr1.addWidget(lbl('  Offset:', color=C['text_dim']))
        self._temp_off_sp = dspin(-9.9,9.9,0.0,1,' °C')
        tr1.addWidget(self._temp_off_sp)
        bo = btn('Set', BTN_NEUTRAL); bo.setFixedSize(40,24)
        bo.clicked.connect(lambda: self._cmd(f'temp offset {self._temp_off_sp.value():.1f}'))
        tr1.addWidget(bo)
        tr1.addStretch(); tgl.addLayout(tr1)
        tr2 = QHBoxLayout()
        self._temp_live = val_lbl(); tr2.addWidget(lbl('Last:', color=C['text_dim']))
        tr2.addWidget(self._temp_live); tr2.addWidget(lbl('VDDA:', color=C['text_dim']))
        self._vdda_live = val_lbl(color=C['text_dim']); tr2.addWidget(self._vdda_live)
        br = btn('Read Now', BTN_NEUTRAL); br.clicked.connect(lambda: self._cmd('temp read'))
        tr2.addWidget(br); tr2.addStretch(); tgl.addLayout(tr2); v.addWidget(tg)

        # Battery
        bg = group('Battery ADC'); bgl = QVBoxLayout(bg)
        br0 = QHBoxLayout(); br0.addWidget(lbl('Mode:', color=C['text_dim']))
        self._batt_mode_rb = {m: QRadioButton(m) for m in ['off','periodic']}
        self._batt_mode_rb['off'].setChecked(True)
        for m,rb in self._batt_mode_rb.items():
            rb.toggled.connect(lambda checked,mv=m: self._cmd(f'batt mode {mv}') if checked else None)
            br0.addWidget(rb)
        br0.addStretch(); bgl.addLayout(br0)
        br1 = QHBoxLayout()
        br1.addWidget(lbl('Period:', color=C['text_dim']))
        self._batt_period_sp2 = spin(1,255,5,' s'); br1.addWidget(self._batt_period_sp2)
        bbt = btn('Set', BTN_NEUTRAL); bbt.setFixedSize(40,24)
        bbt.clicked.connect(lambda: self._cmd(f'batt period {self._batt_period_sp2.value()}'))
        br1.addWidget(bbt)
        br1.addWidget(lbl('  Scale×:', color=C['text_dim']))
        self._batt_scale_sp = dspin(0.1,25.0,2.0,1)
        br1.addWidget(self._batt_scale_sp)
        bsc = btn('Set', BTN_NEUTRAL); bsc.setFixedSize(40,24)
        bsc.clicked.connect(lambda: self._cmd(f'batt scale {self._batt_scale_sp.value():.1f}'))
        br1.addWidget(bsc); br1.addStretch(); bgl.addLayout(br1)
        br2 = QHBoxLayout()
        self._batt_mv_live = val_lbl(color=C['success'])
        self._batt_pct_live= val_lbl(color=C['success'])
        self._batt_raw_live= val_lbl(color=C['text_dim'])
        self._batt_vref_live=val_lbl(color=C['text_dim'])
        br2.addWidget(lbl('mV:', color=C['text_dim'])); br2.addWidget(self._batt_mv_live)
        br2.addWidget(lbl('%:', color=C['text_dim']));  br2.addWidget(self._batt_pct_live)
        br2.addWidget(lbl('raw:', color=C['text_dim']));br2.addWidget(self._batt_raw_live)
        br2.addWidget(lbl('vref:', color=C['text_dim']));br2.addWidget(self._batt_vref_live)
        brd = btn('Read Now', BTN_NEUTRAL); brd.clicked.connect(lambda: self._cmd('batt read'))
        br2.addWidget(brd); br2.addStretch(); bgl.addLayout(br2); v.addWidget(bg)

        # Light
        lg = group('Light Sensor ADC'); lgl = QVBoxLayout(lg)
        lr0 = QHBoxLayout(); lr0.addWidget(lbl('Mode:', color=C['text_dim']))
        self._light_mode_rb = {m: QRadioButton(m) for m in ['off','periodic']}
        self._light_mode_rb['off'].setChecked(True)
        for m,rb in self._light_mode_rb.items():
            rb.toggled.connect(lambda checked,mv=m: self._cmd(f'light mode {mv}') if checked else None)
            lr0.addWidget(rb)
        lr0.addStretch(); lgl.addLayout(lr0)
        lr1 = QHBoxLayout()
        lr1.addWidget(lbl('Period:', color=C['text_dim']))
        self._light_period_sp2 = spin(1,255,5,' s'); lr1.addWidget(self._light_period_sp2)
        lbt = btn('Set', BTN_NEUTRAL); lbt.setFixedSize(40,24)
        lbt.clicked.connect(lambda: self._cmd(f'light period {self._light_period_sp2.value()}'))
        lr1.addWidget(lbt); lr1.addStretch(); lgl.addLayout(lr1)
        lr2 = QHBoxLayout()
        self._light_raw_live = val_lbl(color=C['yellow'])
        self._light_lux_live = val_lbl(color=C['text_dim'])
        lr2.addWidget(lbl('raw:', color=C['text_dim'])); lr2.addWidget(self._light_raw_live)
        lr2.addWidget(lbl('lux~:', color=C['text_dim'])); lr2.addWidget(self._light_lux_live)
        lrd = btn('Read Now', BTN_NEUTRAL); lrd.clicked.connect(lambda: self._cmd('light read'))
        lr2.addWidget(lrd); lr2.addStretch(); lgl.addLayout(lr2); v.addWidget(lg)

        # LED
        ledg = group('LED Indicator'); ledl = QHBoxLayout(ledg)
        self._led_mode_rb = {m: QRadioButton(m) for m in ['off','on','heartbeat','tx']}
        self._led_mode_rb['heartbeat'].setChecked(True)
        for m,rb in self._led_mode_rb.items():
            rb.toggled.connect(lambda checked,mv=m: self._cmd(f'led {mv}') if checked else None)
            ledl.addWidget(rb)
        ledl.addStretch(); v.addWidget(ledg)

        # RTC
        rtcg = group('Real-Time Clock'); rtcl = QVBoxLayout(rtcg)
        rrow0 = QHBoxLayout()
        self._rtc_date_lbl = val_lbl(color=C['cyan'])
        self._rtc_time_lbl = val_lbl(color=C['cyan'])
        self._rtc_time_lbl.setStyleSheet(self._rtc_time_lbl.styleSheet() + 'font-size:16px;')
        rrow0.addWidget(lbl('Date:', color=C['text_dim'])); rrow0.addWidget(self._rtc_date_lbl)
        rrow0.addWidget(lbl('  Time:', color=C['text_dim'])); rrow0.addWidget(self._rtc_time_lbl)
        rrow0.addStretch(); rtcl.addLayout(rrow0)
        rrow1 = QHBoxLayout()
        self._rtc_live_cb = QCheckBox('Live RTC streaming')
        self._rtc_live_cb.toggled.connect(lambda en: self._cmd(f'rtc live {"on" if en else "off"}'))
        rrow1.addWidget(self._rtc_live_cb)
        brtcg = btn('↺ Get RTC', BTN_NEUTRAL); brtcg.clicked.connect(lambda: self._cmd('rtc get'))
        bsync = btn('⟳ Sync PC Time', BTN_PRIMARY); bsync.clicked.connect(self._sync_rtc)
        rrow1.addWidget(brtcg); rrow1.addWidget(bsync); rrow1.addStretch()
        rtcl.addLayout(rrow1); v.addWidget(rtcg)

        # HW Descriptor
        hwg = group('Hardware Descriptor'); hwl = QGridLayout(hwg); hwl.setSpacing(6)
        def hw_field(var, r, c, width=160):
            e = QLineEdit(); e.setFixedWidth(width)
            setattr(self, var, e); hwl.addWidget(e, r, c)

        self._hw_vars = {}
        fields = [
            ('Board:',    '_hw_board',   0, 0),
            ('Firmware:', '_hw_fw',      1, 0),
            ('TX freq Hz:','_hw_freq',   2, 0),
            ('Channels:', '_hw_channels',3, 0),
            ('Pwr levels:','_hw_pwr_lvl',4, 0),
            ('TX type:',  '_hw_txtype',  5, 0),
            ('Comment:',  '_hw_comment', 6, 0),
        ]
        for label, attr, r, c in fields:
            hwl.addWidget(lbl(label, color=C['text_dim']), r, 0)
            e = QLineEdit(); e.setMinimumWidth(200)
            setattr(self, attr, e); hwl.addWidget(e, r, 1)
        hw_btn_row = QHBoxLayout()
        b_hw_read = btn('↺ Read from Beacon', BTN_NEUTRAL)
        b_hw_read.clicked.connect(lambda: self._cmd('hwdesc show'))
        b_hw_send = btn('▶ Send to Beacon', BTN_PRIMARY)
        b_hw_send.clicked.connect(self._hwdesc_send)
        b_hw_save = btn('💾 Save to Flash', BTN_SUCCESS)
        b_hw_save.clicked.connect(lambda: self._cmd('hwdesc save'))
        for b in [b_hw_read, b_hw_send, b_hw_save]: hw_btn_row.addWidget(b)
        hw_btn_row.addStretch()
        hwl_outer = QVBoxLayout()
        hwl_outer.addLayout(hwl); hwl_outer.addLayout(hw_btn_row)
        hwg.setLayout(hwl_outer); v.addWidget(hwg)

        # Actions
        acg = group('Actions'); acl = QHBoxLayout(acg)
        b_status = btn('Status', BTN_NEUTRAL); b_status.clicked.connect(lambda: self._cmd('status'))
        b_regs   = btn('Regs', BTN_NEUTRAL);   b_regs.clicked.connect(lambda: self._cmd('regs'))
        b_save2  = btn('💾 Save Flash', BTN_SUCCESS); b_save2.clicked.connect(self._do_save)
        b_reset  = btn('↺ Reset MCU', BTN_WARN);     b_reset.clicked.connect(self._do_reset)
        b_sleep  = btn('😴 Sleep/Shutdown', BTN_DANGER); b_sleep.clicked.connect(self._do_sleep)
        b_help   = btn('? Help', BTN_NEUTRAL); b_help.clicked.connect(self._show_help)
        for b in [b_status, b_regs, b_save2, b_reset, b_sleep, b_help]: acl.addWidget(b)
        acl.addStretch(); v.addWidget(acg)
        v.addStretch()

    def _hwdesc_send(self):
        freq = self._hw_freq.text().strip() or '30000000'
        ch   = self._hw_channels.text().strip() or '4'
        pwr  = self._hw_pwr_lvl.text().strip() or '4'
        tt   = self._hw_txtype.text().strip() or 'colpitts'
        self._cmd(f'hwdesc tx {freq} {ch} {pwr} {tt}')
        cmt = self._hw_comment.text().strip()
        if cmt: self._cmd(f'hwdesc comment {cmt[:183]}')

    def _term_send(self):
        t = self._term_input.text().strip()
        if t: self._cmd(t); self._term_input.clear()

    def _term_append(self, text, color=None):
        c = color or C['text']
        ts = datetime.datetime.now().strftime('%H:%M:%S')
        self._term.append(f'<span style="color:{C["text_muted"]}">{ts}</span>'
                          f'  <span style="color:{c}">{text.replace("<","&lt;").replace(">","&gt;")}</span>')

    # ── Live tick ─────────────────────────────────────────────────────────────
    def _on_live_tick(self):
        if self._worker.is_open():
            self._cmd('status')

    # ── Serial helpers ────────────────────────────────────────────────────────
    def _cmd(self, text):
        if not self._worker.is_open():
            self.statusBar().showMessage('Not connected'); return
        self._worker.send(text)
        self._term_append(f'>>> {text}', C['text'])

    def _refresh_ports(self):
        self._port_cb.clear()
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self._port_cb.addItems(ports)
        last = self._settings.get('port', '')
        if last in ports: self._port_cb.setCurrentText(last)
        elif ports: self._port_cb.setCurrentIndex(0)

    def _toggle_connect(self):
        if self._worker.is_open(): self._disconnect()
        else: self._connect()

    def _connect(self):
        port = self._port_cb.currentText()
        if not port: QMessageBox.critical(self, 'Error', 'Select a COM port'); return
        result = self._worker.connect_port(port)
        if result is not True:
            QMessageBox.critical(self, 'Connect error', str(result)); return
        self._settings['port'] = port
        self._save_settings()

    def _disconnect(self):
        self._worker.disconnect_port()

    def _on_connected(self):
        self._dot.setStyleSheet(f"color:{C['success']};font-size:16px;")
        self._btn_conn.setText('Disconnect')
        self._btn_conn.setStyleSheet(BTN_DANGER)
        self.statusBar().showMessage(f'Connected: {self._port_cb.currentText()}')
        self._term_append(f'[SYS] connected to {self._port_cb.currentText()}', C['cyan'])
        self._live_timer.start()
        QTimer.singleShot(400, lambda: self._cmd('status'))
        QTimer.singleShot(800, lambda: self._cmd('log get'))

    def _on_disconnected(self):
        self._dot.setStyleSheet(f"color:{C['danger']};font-size:16px;")
        self._btn_conn.setText('Connect')
        self._btn_conn.setStyleSheet(BTN_PRIMARY)
        self.statusBar().showMessage('Disconnected')
        self._term_append('[SYS] disconnected', C['text_muted'])
        self._live_timer.stop()

    # ── Dispatch ──────────────────────────────────────────────────────────────
    def _dispatch(self, line):
        s = line.strip()

        if '[RTC]' in s:
            color = C['cyan']
            m = re.search(r'\[RTC\]\s+(\d{4}-\d{2}-\d{2})\s+(\d{2}:\d{2}:\d{2})\s+(\w+)', s)
            if m:
                self._rtc_date = f'{m.group(1)}  {m.group(3)}'
                self._rtc_time = m.group(2)
                self._rtc_date_lbl.setText(self._rtc_date)
                self._rtc_time_lbl.setText(self._rtc_time)
        elif '[SCHED]' in s:
            color = C['orange']
            m = re.search(r'\[SCHED\]\s+(.+)', s)
            if m:
                self._sched_status = m.group(1).strip()[:30]
                self._sched_status_lbl.setText(f'● {self._sched_status}')
                col = C['success'] if 'ACTIVE' in s or 'YES' in s else C['danger']
                self._sched_status_lbl.setStyleSheet(f'color:{col};font-weight:600;font-size:11px;')
        elif '[TEMP]' in s:
            color = C['blue']
            m = re.search(r'\[TEMP\].*chip=(-?\d+\.\d+)C\s+VDDA=(\d+)mV', s)
            if m:
                self._temp_c  = m.group(1) + ' °C'
                self._vdda_mv = m.group(2) + ' mV'
                self._temp_live.setText(self._temp_c)
                self._vdda_live.setText(self._vdda_mv)
                self._update_dashboard()
        elif '[BATT]' in s:
            color = C['blue']
            m = re.search(r'Battery:\s*(\d+)mV\s+(\d+)%(?:\s+raw=(\d+)\s+vref=(\d+))?', s)
            if m:
                self._batt_mv  = m.group(1) + ' mV'
                self._batt_pct = m.group(2) + ' %'
                self._batt_mv_live.setText(self._batt_mv)
                self._batt_pct_live.setText(self._batt_pct)
                if m.group(3):
                    self._batt_raw = m.group(3); self._batt_vref = m.group(4)
                    self._batt_raw_live.setText(self._batt_raw)
                    self._batt_vref_live.setText(self._batt_vref)
                self._update_dashboard()
        elif '[LIGHT]' in s:
            color = C['blue']
            m = re.search(r'Light:\s*(\d+)\s+\(raw\)\s+~(\d+)\s+lux', s)
            if m:
                self._light_raw = m.group(1); self._light_lux = '~' + m.group(2) + ' lux'
                self._light_raw_live.setText(self._light_raw)
                self._light_lux_live.setText(self._light_lux)
                self._update_dashboard()
        elif '[TX ON' in s or ('[TX]' in s and ' ON' in s):
            color = C['success']
            self._badge_tx.setText('TX ON')
            self._badge_tx.setStyleSheet(f"color:{C['success']};background:{C['surface2']};"
                                          f"border:1px solid {C['border']};border-radius:4px;padding:2px 8px;")
            self._badge_tx_big.setText('▶ TX ON')
            self._badge_tx_big.setStyleSheet(f"color:{C['success']};background:{C['surface2']};"
                                              f"border:1px solid {C['success']};border-radius:6px;"
                                              f"padding:4px 14px;font-size:12px;font-weight:600;")
        elif '[TX OFF]' in s or ('[TX]' in s and ' OFF' in s) or '[ECO' in s:
            color = C['orange']
            self._badge_tx.setText('TX OFF')
            self._badge_tx.setStyleSheet(f"color:{C['text_dim']};background:{C['surface2']};"
                                          f"border:1px solid {C['border']};border-radius:4px;padding:2px 8px;")
            self._badge_tx_big.setText('■ TX OFF')
            self._badge_tx_big.setStyleSheet(f"color:{C['text_muted']};background:{C['surface2']};"
                                              f"border:1px solid {C['border']};border-radius:6px;"
                                              f"padding:4px 14px;font-size:12px;font-weight:600;")
        elif '[GEKON]' in s:  color = C['yellow']
        elif '[SHUTDOWN]' in s or 'Shutdown' in s or 'SLEEP' in s: color = C['danger']
        elif '[PWR DIAG]' in s or '[PRE-WFI]' in s: color = C['text_muted']
        elif '[LOG]' in s:
            color = C['purple']
            self._log_in_dump = False
            self._parse_log_line(s)
        elif s.startswith('idx,ts,flags'):
            color = C['purple']
            self._log_in_dump = True; self._log_csv_buf = []
            self._log_table.setRowCount(0)
        elif self._log_in_dump and re.match(r'^\d+,', s):
            color = C['purple']
            self._log_append_csv_row(s)
        elif '[HWDESC]' in s:
            color = C['text_dim']
            self._parse_hwdesc_line(s)
        elif s == 'OK':   color = C['text_muted']
        elif s == 'ERR':  color = C['danger']
        else:             color = C['text']

        if s and not (self._log_in_dump and re.match(r'^\d+,', s)):
            self._term_append(f'<<< {line}', color)

        self._parse_status(s)
        self._parse_sched_line(s)

    def _parse_log_line(self, s):
        m = re.search(r'\[LOG\]\s+Used:\s+(\d+)\s+entries.*?(\d+)%', s)
        if m:
            used, pct = int(m.group(1)), int(m.group(2))
            self._log_used = used
            self._log_used_lbl.setText(f'Used: {used}/768 entries ({pct}%)')
            self._log_pbar.setValue(pct)
            col = C['success'] if pct < 70 else (C['warning'] if pct < 90 else C['danger'])
            self._log_pbar.setStyleSheet(f"QProgressBar::chunk{{background:{col};border-radius:4px;}}")
            return
        m = re.search(r'\[LOG\]\s+Free:\s+(\d+)\s+entries', s)
        if m: self._log_free = int(m.group(1)); return
        m = re.search(r'\[LOG\]\s+hdr=pg\d+.*?(\d+)/(\d+)\s+entries', s)
        if m:
            used, total = int(m.group(1)), int(m.group(2))
            self._log_used = used; self._log_total = total; self._log_free = total - used
            pct = int(used*100/total) if total else 0
            self._log_used_lbl.setText(f'Used: {used}/{total} entries ({pct}%)')
            self._log_pbar.setValue(pct); return
        m = re.search(r'\[LOG\]\s+Write:\s+(\S+)', s)
        if m:
            v = m.group(1).strip()
            idx = {'ALWAYS':0,'ON_CHANGE':1,'ADAPTIVE':2}.get(v, 0)
            self._log_mode_cb.setCurrentIndex(idx)
            self._log_mode_lbl.setText(f'Write: {v}'); return
        m = re.search(r'\[LOG\]\s+Oflow:\s+(\S+)', s)
        if m:
            v = m.group(1).strip()
            self._log_oflow_cb.setCurrentIndex(0 if v=='CIRCULAR' else 1); return
        m = re.search(r'\[LOG\]\s+mask=0x([0-9a-fA-F]+)\s+oflow=(\d+)\s+mode=(\d+)\s+ckp=(\d+)(?:\s+ts=(\d+))?', s)
        if m:
            mv = int(m.group(1), 16)
            self._lm_temp.setChecked(bool(mv & 0x01))
            self._lm_light.setChecked(bool(mv & 0x02))
            self._lm_batp.setChecked(bool(mv & 0x04))
            self._lm_batmv.setChecked(bool(mv & 0x08))
            ov, mo, ck = int(m.group(2)), int(m.group(3)), int(m.group(4))
            self._log_oflow_cb.setCurrentIndex(0 if ov else 1)
            self._log_mode_cb.setCurrentIndex(mo if mo < 3 else 0)
            self._log_ckp_sp.setValue(ck)
            if m.group(5) is not None:
                if int(m.group(5)): self._ts_rtc_rb.setChecked(True)
                else: self._ts_boot_rb.setChecked(True)
            if self._verify_active:
                self._verify_received.update({
                    'mask': mv, 'oflow': ov, 'mode': mo, 'ckp': ck,
                    'ts': int(m.group(5)) if m.group(5) is not None else 0
                })
            return
        m = re.search(r'\[LOG\]\s+ts=(rtc|boot)', s)
        if m:
            if m.group(1)=='rtc': self._ts_rtc_rb.setChecked(True)
            else: self._ts_boot_rb.setChecked(True); return
        m = re.search(r'\[LOG\]\s+temp=(\d+)s\s+light=(\d+)s\s+bat=(\d+)s', s)
        if m:
            self._log_dt_sp.setValue(int(m.group(1)))
            self._log_dl_sp.setValue(int(m.group(2)))
            self._log_db_sp.setValue(int(m.group(3)))
            if self._verify_active:
                vr = self._verify_received
                vr['temp'] = int(m.group(1)); vr['light'] = int(m.group(2)); vr['bat'] = int(m.group(3))
                self._verify_active = False
                sn = self._verify_snapshot
                results = {k: (vr.get(k) == sn.get(k)) for k in sn}
                QTimer.singleShot(50, lambda r=dict(results): self._log_flash_verify_result(r))
            return
        if 'cleared OK' in s:
            self._log_used = 0; self._log_free = 768; self._log_pbar.setValue(0)
            self._log_table.setRowCount(0); self._log_csv_buf = []
            self._log_dl_pbar.setVisible(False)
        if 'OK' in s and self._log_in_dump:
            self._log_in_dump = False; self._log_dl_pbar.setVisible(False)

    def _parse_status(self, s):
        m = re.match(r'^\s*(\w+)\s*=\s*(.+)$', s)
        if m:
            key = m.group(1).strip()
            val = m.group(2).strip().split('[')[0].strip()
            if key == 'mode':
                for v,b in self._mode_btns.items(): b.setChecked(v == val)
                self._dv_mode.setText(val)
            elif key == 'ch':
                n = re.search(r'(\d+)', val)
                if n:
                    ci = int(n.group(1))
                    for v,b in self._ch_btns.items(): b.setChecked(v == ci)
            elif key == 'pwr':
                n = re.search(r'(\d+)', val)
                if n:
                    pi = int(n.group(1))
                    for v,b in self._pwr_btns.items(): b.setChecked(v == pi)
            elif key == 'pulse_ms':
                try: self._pulse_sp.setValue(int(val))
                except: pass
            elif key == 'period_ms':
                try: self._period_sp.setValue(int(val))
                except: pass
            elif key == 'led_mode':
                for mv,rb in self._led_mode_rb.items(): rb.setChecked(mv == val)
            elif key == 'temp_mode':
                for mv,rb in self._temp_mode_rb.items(): rb.setChecked(mv == val)
            elif key == 'temp_period':
                try: self._temp_period_sp2.setValue(int(val.split()[0]))
                except: pass
            elif key == 'temp_offset':
                try: self._temp_off_sp.setValue(float(val.split()[0]))
                except: pass
            elif key == 'batt_mode':
                for mv,rb in self._batt_mode_rb.items(): rb.setChecked(mv == val)
            elif key == 'batt_period':
                try: self._batt_period_sp2.setValue(int(val.split()[0]))
                except: pass
            elif key == 'batt_scale':
                try: self._batt_scale_sp.setValue(float(val.split()[0]))
                except: pass
            elif key == 'batt_mv':
                self._batt_mv = val.strip() + ' mV'
                self._batt_mv_live.setText(self._batt_mv)
            elif key == 'batt_pct':
                self._batt_pct = val.strip() + ' %'
                self._batt_pct_live.setText(self._batt_pct)
            elif key == 'light_mode':
                for mv,rb in self._light_mode_rb.items(): rb.setChecked(mv == val)
            elif key == 'light_period':
                try: self._light_period_sp2.setValue(int(val.split()[0]))
                except: pass
            elif key == 'light_raw':
                self._light_raw = val.strip()
                self._light_raw_live.setText(self._light_raw)
            elif key == 'light_lux':
                self._light_lux = '~' + val.strip() + ' lux'
                self._light_lux_live.setText(self._light_lux)
            elif key == 'rtc_live':
                self._rtc_live_cb.setChecked(val.strip() == 'on')
            return

        m = re.match(r'^\s*active now:\s*(.+)$', s)
        if m:
            val = m.group(1).strip()
            self._sched_status = val
            col = C['success'] if val == 'YES' else C['danger']
            self._sched_status_lbl.setText(f'● {val}')
            self._sched_status_lbl.setStyleSheet(f'color:{col};font-weight:600;font-size:11px;')

    def _parse_sched_line(self, s):
        m = re.match(r'^\s*hours\s*:\s*(.+)$', s)
        if m:
            val = m.group(1).strip()
            has = False
            for cb in self._hour_cbs: cb.setChecked(False)
            if val != 'all':
                for tok in val.split():
                    if tok.isdigit():
                        h = int(tok)
                        if 0 <= h <= 23: self._hour_cbs[h].setChecked(True); has = True
            if has and not self._sched_cb.isChecked():
                self._sched_cb.setChecked(True)
            return
        m = re.match(r'^\s*days\s*:\s*(.+)$', s)
        if m:
            val = m.group(1).strip()
            for cb in self._day_cbs: cb.setChecked(False)
            if val != 'all':
                day_map = {name: i for i,name in enumerate(_DAY_NAMES)}
                for tok in val.split():
                    if tok in day_map: self._day_cbs[day_map[tok]].setChecked(True)
            return
        m = re.match(r'^\s*months\s*:\s*(.+)$', s)
        if m:
            val = m.group(1).strip()
            for cb in self._month_cbs: cb.setChecked(False)
            if val != 'all':
                mon_map = {name: i for i,name in enumerate(_MONTH_NAMES)}
                for tok in val.split():
                    if tok in mon_map: self._month_cbs[mon_map[tok]].setChecked(True)
            return

    def _parse_hwdesc_line(self, s):
        m = re.match(r'^\[HWDESC\]\s+(\w[\w_]*)\s*=\s*(.+)$', s)
        if not m: return
        key, val = m.group(1).strip(), m.group(2).strip()
        if key == 'tx_freq':
            m2 = re.search(r'(\d+)', val)
            if m2: self._hw_freq.setText(m2.group(1))
        elif key == 'tx_channels': self._hw_channels.setText(val)
        elif key == 'tx_pwr_lvls': self._hw_pwr_lvl.setText(val)
        elif key == 'tx_type':     self._hw_txtype.setText(val)
        elif key == 'comment':     self._hw_comment.setText(val)

    # ── Profiles ──────────────────────────────────────────────────────────────
    def _get_all_profiles(self):
        p = dict(PRESET_PROFILES); p.update(self._custom_profiles); return p

    def _refresh_profile_combo(self):
        self._prof_cb.blockSignals(True)
        cur = self._prof_cb.currentText()
        self._prof_cb.clear()
        self._prof_cb.addItems(list(self._get_all_profiles().keys()))
        if cur in [self._prof_cb.itemText(i) for i in range(self._prof_cb.count())]:
            self._prof_cb.setCurrentText(cur)
        self._prof_cb.blockSignals(False)

    def _load_selected_profile(self):
        name = self._prof_cb.currentText()
        profiles = self._get_all_profiles()
        if name not in profiles: return
        self._apply_profile(profiles[name])
        self._term_append(f'[PROFILE] loaded "{name}"', C['cyan'])

    def _save_as_profile(self):
        name, ok = QInputDialog.getText(self, 'Save Profile', 'Profile name:')
        if not ok or not name.strip(): return
        name = name.strip()
        if name in PRESET_PROFILES:
            QMessageBox.critical(self, 'Profile', f'Cannot overwrite preset "{name}"'); return
        self._custom_profiles[name] = self._capture_state()
        self._save_profiles(); self._refresh_profile_combo()
        self._prof_cb.setCurrentText(name)

    def _delete_profile(self):
        name = self._prof_cb.currentText()
        if name in PRESET_PROFILES:
            QMessageBox.critical(self, 'Profile', 'Cannot delete a preset'); return
        if name not in self._custom_profiles: return
        r = QMessageBox.question(self, 'Delete', f'Delete profile "{name}"?',
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if r == QMessageBox.StandardButton.Yes:
            del self._custom_profiles[name]
            self._save_profiles(); self._refresh_profile_combo()

    def _capture_state(self):
        return {
            'mode': next((v for v,b in self._mode_btns.items() if b.isChecked()), 'pulse'),
            'ch':   next((v for v,b in self._ch_btns.items()   if b.isChecked()), 0),
            'pwr':  next((v for v,b in self._pwr_btns.items()  if b.isChecked()), 3),
            'pulse_ms':  self._pulse_sp.value(),
            'period_ms': self._period_sp.value(),
            'led_mode': next((m for m,rb in self._led_mode_rb.items() if rb.isChecked()), 'heartbeat'),
            'sched_enabled': self._sched_cb.isChecked(),
            'hours':  [h for h,cb in enumerate(self._hour_cbs)  if cb.isChecked()],
            'days':   [d for d,cb in enumerate(self._day_cbs)   if cb.isChecked()],
            'months': [m for m,cb in enumerate(self._month_cbs) if cb.isChecked()],
        }

    def _apply_profile(self, p):
        for v,b in self._mode_btns.items(): b.setChecked(v == p.get('mode','pulse'))
        for v,b in self._ch_btns.items():   b.setChecked(v == p.get('ch',0))
        for v,b in self._pwr_btns.items():  b.setChecked(v == p.get('pwr',3))
        self._pulse_sp.setValue(p.get('pulse_ms', 23))
        self._period_sp.setValue(p.get('period_ms', 2000))
        lm = p.get('led_mode','heartbeat')
        for m,rb in self._led_mode_rb.items(): rb.setChecked(m == lm)
        self._cmd(f'mode {p.get("mode","pulse")}')
        self._cmd(f'ch {p.get("ch",0)}')
        self._cmd(f'pwr {p.get("pwr",3)}')
        self._cmd(f'pulse {p.get("pulse_ms",23)}')
        self._cmd(f'period {p.get("period_ms",2000)}')
        self._cmd(f'led {lm}')
        for cb in self._hour_cbs:   cb.setChecked(False)
        for cb in self._day_cbs:    cb.setChecked(False)
        for cb in self._month_cbs:  cb.setChecked(False)
        for h in p.get('hours',  []):
            if 0 <= h <= 23: self._hour_cbs[h].setChecked(True)
        for d in p.get('days',   []):
            if 0 <= d <= 6:  self._day_cbs[d].setChecked(True)
        for m in p.get('months', []):
            if 0 <= m <= 11: self._month_cbs[m].setChecked(True)
        en = p.get('sched_enabled', False)
        self._sched_cb.setChecked(en)

    # ── Actions ───────────────────────────────────────────────────────────────
    def _sync_rtc(self):
        now = datetime.datetime.now()
        self._cmd(now.strftime('rtc set %Y-%m-%d %H:%M:%S'))

    def _do_reset(self):
        r = QMessageBox.question(self, 'Reset MCU', 'Soft-reset the MCU?\nConfig reloads from flash.',
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if r == QMessageBox.StandardButton.Yes: self._cmd('reset')

    def _do_sleep(self):
        r = QMessageBox.question(self, 'Shutdown',
            'Enter Shutdown mode?\nMCU stops until GEKON button or power cycle.',
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if r == QMessageBox.StandardButton.Yes: self._cmd('sleep')

    def _do_save(self):
        r = QMessageBox.question(self, 'Save & Restart',
            'Apply all settings, save to flash and restart MCU?',
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if r != QMessageBox.StandardButton.Yes: return
        for cb,h in [(cb, h) for h,cb in enumerate(self._hour_cbs) if cb.isChecked()]:
            pass
        if self._sched_cb.isChecked():
            self._on_hour_change(); self._on_day_change(); self._on_month_change()
        else:
            self._cmd('sched off')
        self._cmd(f'mode {next((v for v,b in self._mode_btns.items() if b.isChecked()),"pulse")}')
        self._cmd(f'ch {next((v for v,b in self._ch_btns.items() if b.isChecked()),0)}')
        self._cmd(f'pwr {next((v for v,b in self._pwr_btns.items() if b.isChecked()),3)}')
        self._cmd(f'pulse {self._pulse_sp.value()}')
        self._cmd(f'period {self._period_sp.value()}')
        self._cmd(f'led {next((m for m,rb in self._led_mode_rb.items() if rb.isChecked()),"heartbeat")}')
        self._cmd(f'temp mode {next((m for m,rb in self._temp_mode_rb.items() if rb.isChecked()),"off")}')
        self._cmd(f'temp period {self._temp_period_sp2.value()}')
        self._cmd(f'temp offset {self._temp_off_sp.value():.1f}')
        self._cmd(f'batt mode {next((m for m,rb in self._batt_mode_rb.items() if rb.isChecked()),"off")}')
        self._cmd(f'batt period {self._batt_period_sp2.value()}')
        self._cmd(f'batt scale {self._batt_scale_sp.value():.1f}')
        self._cmd(f'light mode {next((m for m,rb in self._light_mode_rb.items() if rb.isChecked()),"off")}')
        self._cmd(f'light period {self._light_period_sp2.value()}')
        self._cmd(f'rtc live {"on" if self._rtc_live_cb.isChecked() else "off"}')
        self._cmd('save')
        self._cmd('reset')

    def _show_help(self):
        dlg = QDialog(self); dlg.setWindowTitle('TX Beacon — Help'); dlg.resize(700, 500)
        dlg.setStyleSheet(APP_SS)
        v2 = QVBoxLayout(dlg)
        te = QTextEdit(); te.setReadOnly(True)
        te.setFont(QFont('Consolas', 9))
        te.setStyleSheet(f'background:#0d1117;color:{C["text"]};border:none;')
        te.setPlainText(
            'TX Beacon 30 MHz — v2\n\nUART Commands:\n'
            '  status, regs, reset, sleep, save\n'
            '  tx on/off  mode off|pulse|cont|eco  ch 0..3  pwr 1..4\n'
            '  pulse <ms>  period <ms>\n'
            '  temp [mode/period/offset/read]\n'
            '  batt [mode/period/scale/read]\n'
            '  light [mode/period/read]\n'
            '  led off|on|heartbeat|tx\n'
            '  rtc get|set|live\n'
            '  sched show|off|hours|days|months\n'
            '  log info|get|dump|clear|mask|temp|light|bat|mode|overflow\n'
            '  hwdesc show|save|tx|comment\n'
        )
        v2.addWidget(te)
        b = btn('Close', BTN_NEUTRAL); b.clicked.connect(dlg.close)
        v2.addWidget(b); dlg.exec()

    # ── Persistence ───────────────────────────────────────────────────────────
    def _load_settings(self):
        try:
            if os.path.exists(SETTINGS_FILE):
                with open(SETTINGS_FILE, 'r', encoding='utf-8') as f: return json.load(f)
        except: pass
        return {}

    def _save_settings(self):
        try:
            with open(SETTINGS_FILE, 'w', encoding='utf-8') as f:
                json.dump(self._settings, f, indent=2)
        except: pass

    def _load_profiles(self):
        try:
            if os.path.exists(PROFILES_FILE):
                with open(PROFILES_FILE, 'r', encoding='utf-8') as f: return json.load(f)
        except: pass
        return {}

    def _save_profiles(self):
        try:
            with open(PROFILES_FILE, 'w', encoding='utf-8') as f:
                json.dump(self._custom_profiles, f, indent=2)
        except: pass

    def closeEvent(self, event):
        self._save_settings()
        self._worker.disconnect_port()
        super().closeEvent(event)

    def _update_dashboard(self):
        self._dv_temp.setText(self._temp_c)
        self._dv_vdda.setText(self._vdda_mv)
        self._dv_batmv.setText(self._batt_mv)
        self._dv_batpct.setText(self._batt_pct)
        self._dv_light.setText(self._light_raw)
        self._dv_lux.setText(self._light_lux)
        self._dv_rtc.setText(f"{self._rtc_date}  {self._rtc_time}")
        self._dv_sched.setText(self._sched_status)

        # Badges
        tc = self._temp_c.replace('°C','').strip()
        try:
            tv = float(tc)
            if 36 <= tv <= 38:   tc_col = C['success']
            elif 35 <= tv < 36 or 38 < tv <= 39: tc_col = C['warning']
            else:                tc_col = C['danger']
        except: tc_col = C['text_dim']
        self._badge_temp.setText(f'🌡 {self._temp_c}')
        self._badge_temp.setStyleSheet(f"color:{tc_col};background:{C['surface2']};"
                                        f"border:1px solid {C['border']};border-radius:4px;padding:2px 8px;")
        self._badge_bat.setText(f'🔋 {self._batt_pct}')

        # Charts
        if _HAS_PG:
            try:
                tv_f = float(tc); self._temp_buf.append(tv_f)
            except: pass
            try:
                bv_f = float(self._batt_mv.replace('mV','').strip()); self._bat_buf.append(bv_f)
            except: pass
            t_idx = list(range(len(self._temp_buf)))
            self._temp_curve.setData(t_idx, list(self._temp_buf))
            b_idx = list(range(len(self._bat_buf)))
            self._bat_curve.setData(b_idx, list(self._bat_buf))


# ── Entry point ───────────────────────────────────────────────────────────────
if __name__ == '__main__':
    app = QApplication(sys.argv)
    app.setApplicationName('TX Beacon 30MHz v2')
    app.setStyle('Fusion')
    w = MainWindow()
    w.show()
    sys.exit(app.exec())

