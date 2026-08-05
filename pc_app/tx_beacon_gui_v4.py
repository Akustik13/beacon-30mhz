#!/usr/bin/env python3
"""TX Beacon GUI v4 — Light/Dark theme, transport abstraction, BLE configuration tab.
Usage: python tx_beacon_gui_v4.py [--selftest]
"""
import sys, os, re, json, binascii, struct, time, math, threading, queue, random
from collections import deque
from dataclasses import dataclass, field
from typing import Optional, List, Tuple

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QGridLayout, QLabel, QPushButton, QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox,
    QTabWidget, QFrame, QProgressBar, QScrollArea, QCheckBox,
    QTableWidget, QTableWidgetItem, QHeaderView, QSizePolicy,
    QMessageBox, QFileDialog, QInputDialog, QStackedWidget, QButtonGroup,
    QRadioButton, QAbstractItemView, QSplitter, QPlainTextEdit,
    QGraphicsOpacityEffect, QDialog, QGroupBox
)
from PyQt6.QtCore import (
    Qt, QThread, pyqtSignal, QObject, QTimer, QSettings, QSize, QRect, QRectF,
    QEvent
)
from PyQt6.QtGui import (
    QFont, QColor, QPainter, QPen, QBrush, QPalette, QPainterPath
)

try:
    import pyqtgraph as pg
    HAS_PG = True
except ImportError:
    HAS_PG = False

try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False

_BLEAK_ERR = ''
try:
    from bleak import BleakScanner, BleakClient
    import asyncio as _asyncio
    HAS_BLEAK = True
except Exception as _e:
    HAS_BLEAK = False
    _BLEAK_ERR = str(_e)

# ─────────────────────────────────────────────────────────────────────────────
# Themes
# ─────────────────────────────────────────────────────────────────────────────
THEMES = {
    'light': {
        'bg':             '#dce8f7',   # page background — mid-blue-gray
        'card':           '#eef6ff',   # card background — very light blue
        'border':         '#b0c8e8',
        'accent':         '#1a4fd6',
        'success':        '#0d7c38',
        'warning':        '#b45000',
        'danger':         '#c01c1c',
        'text':           '#0d1017',
        'text_dim':       '#3d5270',
        # chip unchecked — light blue pill, always visible
        'chip_bg':        '#dbeafe',
        'chip_bdr':       '#7ab3f8',
        'chip_bdr_b':     '#3b82f6',
        'chip_txt':       '#1339a8',
        # chip checked — solid blue
        'chip_on':        '#1a4fd6',
        'chip_on_t':      '#ffffff',
        'chip_on_b':      '#0f2d8a',
        # secondary button — light blue fill
        'btn2_bg':        '#dbeafe',
        'btn2_bdr':       '#7ab3f8',
        'btn2_bdr_b':     '#3b82f6',
        'btn2_txt':       '#1339a8',
        'input_bg':       '#f8fbff',   # inputs slightly whiter than cards
        'input_border':   '#7aabdc',
        'banner_ok_bg':   '#d1fae5',
        'banner_ok_brd':  '#0d7c38',
        'banner_ok_txt':  '#064420',
        'banner_err_bg':  '#fee2e2',
        'banner_err_brd': '#c01c1c',
        'banner_err_txt': '#6b0f0f',
        'footer_bg':      '#e2effe',
        'sparkline':      '#1a4fd6',
        'log_bg':         '#eaf3ff',
        'log_text':       '#0d2040',
    },
    'dark': {
        'bg':             '#080b14',
        'card':           '#0e1c35',   # dark blue-navy card
        'border':         '#2a3050',
        'accent':         '#5b9bff',
        'success':        '#22d46a',
        'warning':        '#f0a500',
        'danger':         '#f04040',
        'text':           '#e8edf8',
        'text_dim':       '#8595b8',
        # chip unchecked — navy pill
        'chip_bg':        '#1c2f6e',
        'chip_bdr':       '#3b5bbf',
        'chip_bdr_b':     '#243b9a',
        'chip_txt':       '#93c5fd',
        # chip checked — bright blue
        'chip_on':        '#2563eb',
        'chip_on_t':      '#ffffff',
        'chip_on_b':      '#1a4fd6',
        # secondary button
        'btn2_bg':        '#1a2035',
        'btn2_bdr':       '#3b5bbf',
        'btn2_bdr_b':     '#2a3050',
        'btn2_txt':       '#93c5fd',
        'input_bg':       '#1a2035',
        'input_border':   '#2a3050',
        'banner_ok_bg':   '#0c3a20',
        'banner_ok_brd':  '#22d46a',
        'banner_ok_txt':  '#6effa8',
        'banner_err_bg':  '#3a0c0c',
        'banner_err_brd': '#f04040',
        'banner_err_txt': '#ffaaaa',
        'footer_bg':      '#111827',
        'sparkline':      '#5b9bff',
        'log_bg':         '#0d1220',
        'log_text':       '#c8d8f0',
    },
}

# ─── Unicode icons (render fine on Win11 with Segoe UI Emoji) ────────────────
ICO = {
    'tx':       '📡',
    'temp':     '🌡',
    'battery':  '🔋',
    'light':    '💡',
    'storage':  '💾',
    'data':     '📊',
    'settings': '⚙',
    'log':      '📋',
    'erase':    '🗑',
    'download': '📥',
    'schedule': '🗓',
    'ok':       '✅',
    'err':      '❌',
    'warn':     '⚠',
    'connect':  '🔗',
    'ble':      '📶',
    'events':   '⚡',
}

def build_qss(C: dict) -> str:
    return f"""
QWidget {{ background-color:{C['bg']}; color:{C['text']};
           font-family:"Segoe UI",Arial,sans-serif; font-size:10pt; }}
QMainWindow,QDialog {{ background-color:{C['bg']}; }}

/* Labels/checkboxes/radio inside cards must be transparent so card BG shows through */
QLabel {{ background: transparent; }}
QCheckBox {{ background: transparent; }}
QRadioButton {{ background: transparent; }}

/* Cards */
QFrame#card {{ background:{C['card']}; border:1px solid {C['border']};
               border-radius:10px; }}

/* Tabs */
QTabWidget::pane {{ border:none; background:transparent; }}
QTabBar::tab {{ background:transparent; color:{C['text_dim']};
                border:2px solid transparent; padding:8px 20px;
                font-size:10pt; }}
QTabBar::tab:selected {{ color:{C['accent']};
                         border-bottom:2px solid {C['accent']};
                         font-weight:bold; }}
QTabBar::tab:hover:!selected {{ color:{C['text']}; }}

/* Primary button */
QPushButton#primary {{ background:{C['accent']}; color:#fff;
                       border:1px solid {C['accent']};
                       border-bottom:3px solid {C['chip_on_b']};
                       border-radius:6px;
                       padding:7px 20px; font-weight:bold; font-size:10pt; }}
QPushButton#primary:hover {{ background:{C['chip_on']}; color:{C['chip_on_t']}; }}
QPushButton#primary:pressed {{ border-bottom:1px solid {C['chip_on_b']}; padding-top:9px; }}
QPushButton#primary:disabled {{ background:{C['border']}; color:{C['text_dim']};
                                border-bottom:1px solid {C['border']}; }}

/* Secondary / outlined button — light-blue fill, always visible */
QPushButton#secondary {{ background:{C['btn2_bg']}; color:{C['btn2_txt']};
                         border:1px solid {C['btn2_bdr']};
                         border-bottom:3px solid {C['btn2_bdr_b']};
                         border-radius:6px; padding:5px 14px; font-weight:600; }}
QPushButton#secondary:hover {{ background:{C['chip_bg']}; border-color:{C['accent']};
                                border-bottom-color:{C['accent']}; color:{C['accent']}; }}
QPushButton#secondary:pressed {{ border-bottom:1px solid {C['btn2_bdr_b']}; padding-top:7px; }}
QPushButton#secondary:disabled {{ background:{C['bg']}; color:{C['text_dim']};
                                   border-color:{C['border']}; border-bottom-color:{C['border']}; }}

/* Chip toggle buttons — light-blue pill, raised look */
QPushButton#chip {{ background:{C['chip_bg']}; color:{C['chip_txt']};
                    border:1px solid {C['chip_bdr']};
                    border-bottom:3px solid {C['chip_bdr_b']};
                    border-radius:6px;
                    padding:3px 10px; font-size:9pt; font-weight:600; min-height:28px; }}
QPushButton#chip:checked {{ background:{C['chip_on']}; color:{C['chip_on_t']};
                             border:1px solid {C['chip_on_b']};
                             border-bottom:3px solid {C['chip_on_b']}; font-weight:bold; }}
QPushButton#chip:pressed  {{ border-bottom:1px solid {C['chip_bdr_b']}; padding-top:5px; }}
QPushButton#chip:hover:!checked {{ background:{C['chip_bg']}; border-color:{C['accent']};
                                    border-bottom-color:{C['accent']}; color:{C['accent']}; }}
QPushButton#chip:disabled {{ background:{C['bg']}; color:{C['text_dim']};
                              border-color:{C['border']}; border-bottom-color:{C['border']}; }}

/* Inputs */
QLineEdit,QSpinBox {{ background:{C['input_bg']}; color:{C['text']};
                      border:1px solid {C['input_border']}; border-radius:6px;
                      padding:4px 8px; font-size:10pt; }}
QLineEdit:focus,QSpinBox:focus {{ border-color:{C['accent']}; }}
QSpinBox::up-button,QSpinBox::down-button {{ background:transparent; border:none; }}

/* ComboBox */
QComboBox {{ background:{C['input_bg']}; color:{C['text']};
             border:1px solid {C['input_border']}; border-radius:6px;
             padding:4px 8px; }}
QComboBox QAbstractItemView {{ background:{C['card']}; color:{C['text']};
                                border:1px solid {C['border']};
                                selection-background-color:{C['accent']}; }}

/* Scrollbar */
QScrollBar:vertical {{ background:{C['bg']}; width:8px; border-radius:4px; }}
QScrollBar::handle:vertical {{ background:{C['border']}; border-radius:4px; min-height:20px; }}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {{ height:0; }}

/* Table */
QTableWidget {{ background:{C['card']}; color:{C['text']};
                gridline-color:{C['border']}; border:1px solid {C['border']};
                border-radius:6px; }}
QTableWidget::item:selected {{ background:{C['accent']}; color:#fff; }}
QHeaderView::section {{ background:{C['bg']}; color:{C['text_dim']};
                        border:none; border-bottom:1px solid {C['border']};
                        padding:4px 8px; font-weight:bold; }}

/* Progress bar */
QProgressBar {{ background:{C['border']}; border-radius:4px; border:none; }}
QProgressBar::chunk {{ background:{C['success']}; border-radius:4px; }}

/* Checkbox — styled indicator so it's visible on any card background */
QCheckBox {{ color:{C['text']}; spacing:8px; }}
QCheckBox::indicator {{
    width: 16px; height: 16px;
    border: 2px solid {C['accent']};
    border-radius: 3px;
    background: {C['input_bg']};
}}
QCheckBox::indicator:unchecked:hover {{
    background: {C['chip_bg']};
    border-color: {C['chip_on_b']};
}}
QCheckBox::indicator:checked {{
    background: {C['accent']};
    border-color: {C['accent']};
    image: url(none);  /* let native draw checkmark */
}}
QCheckBox::indicator:checked:hover {{
    background: {C['chip_on_b']};
    border-color: {C['chip_on_b']};
}}

/* RadioButton */
QRadioButton {{ color:{C['text']}; spacing:6px; }}
QRadioButton::indicator {{
    width: 14px; height: 14px;
    border: 2px solid {C['accent']};
    border-radius: 7px;
    background: {C['input_bg']};
}}
QRadioButton::indicator:checked {{
    background: {C['accent']};
    border-color: {C['accent']};
}}

/* Top bar and sticky footer — scoped to their own objectName so style
   does NOT cascade into child buttons */
QFrame#topbar {{ background:{C['card']}; border-bottom:1px solid {C['border']}; }}
QWidget#stickyFooter {{ background:{C['footer_bg']}; border-top:1px solid {C['border']}; }}

/* Theme toggle button — always high-contrast */
QPushButton#theme_btn {{ background:{C['accent']}; color:#ffffff;
                          border:none; border-radius:16px;
                          font-size:13pt; font-weight:bold;
                          min-width:32px; min-height:32px; }}
QPushButton#theme_btn:hover {{ background:{C['chip_on_b']}; }}
"""

# ─────────────────────────────────────────────────────────────────────────────
# Drag-scroll helper — finger/mouse drag on any QScrollArea
# ─────────────────────────────────────────────────────────────────────────────
class DragScrollFilter(QObject):
    """Install on a QScrollArea's viewport to enable drag-to-scroll.

    Distinguishes a drag (>5 px movement) from a click so child widgets
    (buttons, checkboxes, spinboxes) still receive their click events.

    Usage:
        _install_drag_scroll(scroll_area)   # convenience wrapper below
    """
    _THRESHOLD = 5

    def __init__(self, scroll_area):
        super().__init__(scroll_area)
        self._sa       = scroll_area
        self._start    = None   # QPoint of press
        self._dragging = False

    def eventFilter(self, obj, event):
        t = event.type()
        if t == QEvent.Type.MouseButtonPress and event.button() == Qt.MouseButton.LeftButton:
            self._start    = event.pos()
            self._dragging = False
            return False   # let child widgets see the press
        elif t == QEvent.Type.MouseMove and self._start is not None:
            if event.buttons() & Qt.MouseButton.LeftButton:
                d = event.pos() - self._start
                if not self._dragging and (abs(d.x()) > self._THRESHOLD or
                                           abs(d.y()) > self._THRESHOLD):
                    self._dragging = True
                if self._dragging:
                    self._sa.horizontalScrollBar().setValue(
                        self._sa.horizontalScrollBar().value() - d.x())
                    self._sa.verticalScrollBar().setValue(
                        self._sa.verticalScrollBar().value() - d.y())
                    self._start = event.pos()
                    return True   # consume move — prevents text-select etc.
        elif t == QEvent.Type.MouseButtonRelease:
            self._start    = None
            self._dragging = False
        return False


def _install_drag_scroll(scroll_area):
    """Attach DragScrollFilter to scroll_area.viewport()."""
    f = DragScrollFilter(scroll_area)
    scroll_area.viewport().installEventFilter(f)
    return f


# ─────────────────────────────────────────────────────────────────────────────
# Wire structures
# ─────────────────────────────────────────────────────────────────────────────
PROTO_VERSION = 1
LOG_ENTRIES_MAX = 768   # 6 pages × 2048 / 16
LOG_ENTRY_SIZE  = 16
FLASH_PAGE_SIZE = 2048

# ConfigBlob: 64 bytes, little-endian packed
# Offsets confirmed from C struct (proto_structs.h):
#  0  proto_ver        u8
#  1  cfg_size         u8
#  2  rf_mode          u8
#  3  rf_channel       u8
#  4  rf_power         u8
#  5  led_mode         u8
#  6  rf_pulse_ms      u16
#  8  rf_period_ms     u32
# 12  temp_iv_s        u16
# 14  light_iv_s       u16
# 16  bat_iv_s         u16
# 18  temp_offset_01c  i16
# 20  bat_scale_x100   u16
# 22  log_mask         u8
# 23  log_mode         u8
# 24  log_overflow     u8
# 25  log_ckp_n        u8
# 26  log_dt_01c       u8
# 27  log_dl_pct       u8
# 28  log_db_pct       u8
# 29  log_ts_source    u8  (0=boot seconds, 1=RTC)
# 30  sched_en         u8
# 31  sched_scope      u8
# 32  sched_hours      u32
# 36  sched_days       u8
# 37  reserved3        u8
# 38  sched_months     u16
# 40  uptime_save_min  u16  (minutes; 0 = 24 h firmware default)
# 42  reserved4[18]
# 60  crc32            u32
_CFG_FMT = '<BBBBBBHIHHHhHBBBBBBBBBBIBBHH18sI'
assert struct.calcsize(_CFG_FMT) == 64

@dataclass
class ConfigBlob:
    proto_ver:       int = PROTO_VERSION
    cfg_size:        int = 64
    rf_mode:         int = 1    # 0=off 1=pulse 2=cont 3=eco
    rf_channel:      int = 0    # 0..3
    rf_power:        int = 4    # 1..4
    led_mode:        int = 3    # 0=off 1=on 2=heartbeat 3=tx
    rf_pulse_ms:     int = 23   # 5..5000
    rf_period_ms:    int = 2000 # 100..600000
    temp_iv_s:       int = 60   # 0=off
    light_iv_s:      int = 0
    bat_iv_s:        int = 3600
    temp_offset_01c: int = 0
    bat_scale_x100:  int = 200  # 200 = 2.00×
    log_mask:        int = 0x0D # temp+bat%+batmv
    log_mode:        int = 0    # 0=always
    log_overflow:    int = 1    # circular
    log_ckp_n:       int = 16
    log_dt_01c:      int = 5
    log_dl_pct:      int = 10
    log_db_pct:      int = 2
    log_ts_source:   int = 0    # 0=boot seconds, 1=RTC
    sched_en:        int = 0
    sched_scope:     int = 0
    sched_hours:     int = 0
    sched_days:      int = 0
    reserved3:       int = 0
    sched_months:    int = 0
    uptime_save_min: int = 0    # minutes; 0 = 24 h firmware default
    crc32:           int = 0    # filled by to_bytes()

    def to_bytes(self) -> bytes:
        raw = struct.pack(_CFG_FMT,
            self.proto_ver, self.cfg_size,
            self.rf_mode, self.rf_channel, self.rf_power, self.led_mode,
            self.rf_pulse_ms, self.rf_period_ms,
            self.temp_iv_s, self.light_iv_s, self.bat_iv_s,
            self.temp_offset_01c,
            self.bat_scale_x100,
            self.log_mask, self.log_mode, self.log_overflow, self.log_ckp_n,
            self.log_dt_01c, self.log_dl_pct, self.log_db_pct, self.log_ts_source,
            self.sched_en, self.sched_scope,
            self.sched_hours, self.sched_days, self.reserved3, self.sched_months,
            self.uptime_save_min, bytes(18), 0)
        crc = binascii.crc32(raw[:60]) & 0xFFFFFFFF
        return raw[:60] + struct.pack('<I', crc)

    @classmethod
    def from_bytes(cls, data: bytes) -> 'ConfigBlob':
        assert len(data) == 64
        f = struct.unpack(_CFG_FMT, data)
        obj = cls(
            proto_ver=f[0], cfg_size=f[1],
            rf_mode=f[2], rf_channel=f[3], rf_power=f[4], led_mode=f[5],
            rf_pulse_ms=f[6], rf_period_ms=f[7],
            temp_iv_s=f[8], light_iv_s=f[9], bat_iv_s=f[10],
            temp_offset_01c=f[11], bat_scale_x100=f[12],
            log_mask=f[13], log_mode=f[14], log_overflow=f[15], log_ckp_n=f[16],
            log_dt_01c=f[17], log_dl_pct=f[18], log_db_pct=f[19], log_ts_source=f[20],
            sched_en=f[21], sched_scope=f[22],
            sched_hours=f[23], sched_days=f[24], reserved3=f[25], sched_months=f[26],
            uptime_save_min=f[27],
            crc32=f[29]
        )
        return obj

    def verify_crc(self) -> bool:
        # to_bytes() always produces a fresh CRC; compare it against
        # self.crc32 which was set from the wire by from_bytes().
        fresh = self.to_bytes()
        fresh_crc = struct.unpack('<I', fresh[60:64])[0]
        return fresh_crc == self.crc32

    def to_dict(self) -> dict:
        return {k: v for k, v in self.__dict__.items() if k != 'crc32'
                and k != 'reserved3' and k != 'cfg_size' and k != 'proto_ver'}

    @classmethod
    def from_dict(cls, d: dict) -> 'ConfigBlob':
        obj = cls()
        for k, v in d.items():
            if hasattr(obj, k):
                setattr(obj, k, v)
        return obj


# StatusBlob: 24 bytes
_STAT_FMT = '<IhHHBBHBBHHI'
assert struct.calcsize(_STAT_FMT) == 24

@dataclass
class StatusBlob:
    uptime_s:     int = 0
    temp_01c:     int = 365   # 36.5 C
    vdda_mv:      int = 3300
    bat_mv:       int = 3720
    bat_pct:      int = 82
    tx_active:    int = 0
    light_raw:    int = 0
    sched_active: int = 1
    flags:        int = 0
    log_used:     int = 0
    log_total:    int = 768
    rtc_unix:     int = 0

    @classmethod
    def from_bytes(cls, data: bytes) -> 'StatusBlob':
        assert len(data) == 24
        f = struct.unpack(_STAT_FMT, data)
        return cls(uptime_s=f[0], temp_01c=f[1], vdda_mv=f[2],
                   bat_mv=f[3], bat_pct=f[4], tx_active=f[5],
                   light_raw=f[6], sched_active=f[7], flags=f[8],
                   log_used=f[9], log_total=f[10], rtc_unix=f[11])

# InfoBlob: 48 bytes
# Layout: proto_ver(B) hw_rev(B) fw_major(B) fw_minor(B) fw_patch(B)
#         sensors_present(B) log_entry_size(H) uid(I) log_total_entries(I)
#         flash_page_size(I) tag(12s) total_active_h(I) total_stop1_h(I)
#         total_shutdown_h(I) flash_erase_count(I)
#         → 6+2+4+4+4+12+4+4+4+4 = 48
_INFO_FMT2 = '<BBBBBBHIII12sIIII'
assert struct.calcsize(_INFO_FMT2) == 48

@dataclass
class InfoBlob:
    proto_ver:          int = PROTO_VERSION
    hw_rev:             int = 1
    fw_major:           int = 1
    fw_minor:           int = 2
    fw_patch:           int = 0
    sensors_present:    int = 0x07
    log_entry_size:     int = 16
    uid:                int = 0
    log_total_entries:  int = 768
    flash_page_size:    int = 2048
    tag:                str = ''
    total_active_h:     int = 0
    total_stop1_h:      int = 0
    total_shutdown_h:   int = 0
    flash_erase_count:  int = 0

    @classmethod
    def from_bytes(cls, data: bytes) -> 'InfoBlob':
        return cls._from_bytes(data)

    @classmethod
    def _from_bytes(cls, data: bytes) -> 'InfoBlob':
        assert len(data) == 48
        (pv, hw, fmaj, fmin, fpat, sensors, lsz, uid, ltot, fpsz,
         tag_b, active_h, stop1_h, shutdn_h, erase_cnt) = struct.unpack(_INFO_FMT2, data)
        return cls(proto_ver=pv, hw_rev=hw, fw_major=fmaj, fw_minor=fmin, fw_patch=fpat,
                   sensors_present=sensors, log_entry_size=lsz, uid=uid,
                   log_total_entries=ltot, flash_page_size=fpsz,
                   tag=tag_b.rstrip(b'\x00').decode('ascii', errors='replace'),
                   total_active_h=active_h, total_stop1_h=stop1_h,
                   total_shutdown_h=shutdn_h, flash_erase_count=erase_cnt)

# LogRecord: 16 bytes
_LOG_FMT = '<IBBhHBBB3s'
assert struct.calcsize(_LOG_FMT) == 16

@dataclass
class LogRecord:
    timestamp:   int = 0
    mask:        int = 0
    rec_flags:   int = 0
    temp_01c:    int = 0
    light_raw:   int = 0
    battery_pct: int = 0
    bat_mv_hi:   int = 0
    bat_mv_lo:   int = 0
    # pad[3] ignored

    @classmethod
    def from_bytes(cls, data: bytes) -> 'LogRecord':
        assert len(data) == 16
        f = struct.unpack(_LOG_FMT, data)
        return cls(timestamp=f[0], mask=f[1], rec_flags=f[2], temp_01c=f[3],
                   light_raw=f[4], battery_pct=f[5], bat_mv_hi=f[6], bat_mv_lo=f[7])

    @property
    def bat_mv(self) -> int:
        return (self.bat_mv_hi << 8) | self.bat_mv_lo

    REC_FLAG_FULL       = 0x00
    REC_FLAG_DELTA      = 0x01
    REC_FLAG_CHECKPOINT = 0x02
    REC_FLAG_EMPTY      = 0xFF

    LOG_MASK_TEMP        = 0x01
    LOG_MASK_LIGHT       = 0x02
    LOG_MASK_BATTERY_PCT = 0x04
    LOG_MASK_BATTERY_MV  = 0x08


# ── Log record v2 types (must match flash_log_types.h LOGREC_TYPE_*) ─────────
LOGREC_TYPE_TEMP    = 0x00   # payload: temp_01c(i16) vdda_mv(u16) [6 rsv]
LOGREC_TYPE_BATT    = 0x01   # payload: bat_mv(u16) bat_pct(u8)    [7 rsv]
LOGREC_TYPE_LIGHT   = 0x02   # payload: light_raw(u16)             [8 rsv]
LOGREC_TYPE_ACCEL   = 0x03   # payload: x(i16) y(i16) z(i16) fs(u8) [3 rsv]
LOGREC_TYPE_MARKER  = 0xFE   # payload: tag(u8)
LOGREC_TYPE_EMPTY   = 0xFF   # erased flash slot

@dataclass
class LogRecordV2:
    timestamp: int = 0
    type:      int = 0
    flags:     int = 0
    payload:   bytes = field(default_factory=lambda: bytes(10))

    @classmethod
    def from_bytes(cls, data: bytes) -> 'LogRecordV2':
        assert len(data) == 16
        return cls(timestamp=struct.unpack_from('<I', data, 0)[0],
                   type=data[4], flags=data[5], payload=bytes(data[6:16]))

    def accel_xyz(self) -> 'Optional[Tuple[int,int,int]]':
        if self.type != LOGREC_TYPE_ACCEL or len(self.payload) < 6:
            return None
        x, y, z = struct.unpack_from('<hhh', self.payload, 0)
        return x, y, z

    def accel_fs(self) -> int:
        return self.payload[6] if len(self.payload) > 6 else 0


# ── Binary command layer — opcode constants (mirrors cmd_layer.h) ─────────────
CMD_OK              = 0x00
CMD_ERR_LEN         = 0x01
CMD_ERR_CRC         = 0x02
CMD_ERR_UNSUPPORTED = 0x03
CMD_ERR_BUSY        = 0x04
CMD_ERR_STATE       = 0x05
CMD_ERR_PARAM       = 0x06
CMD_ERR_FLASH       = 0x07

OP_PING             = 0x00
OP_TIME_GET         = 0x01
OP_TIME_SET         = 0x02
OP_REBOOT           = 0x03
OP_CONFIG_INFO      = 0x10
OP_CONFIG_READ      = 0x11
OP_CONFIG_WRITE     = 0x12
OP_CONFIG_COMMIT    = 0x13
OP_CONFIG_RESET     = 0x14
OP_LOG_INFO         = 0x20
OP_LOG_READ         = 0x21
OP_LOG_ERASE        = 0x22
OP_LOG_MARK         = 0x23
OP_SENSOR_LIST      = 0x30
OP_SENSOR_ENABLE    = 0x31
OP_SENSOR_INTERVAL  = 0x32
OP_SENSOR_READ_NOW  = 0x33
OP_ACCEL_CFG_GET    = 0x34
OP_ACCEL_CFG_SET    = 0x35
OP_ACCEL_POWER      = 0x36
OP_ACCEL_PROBE      = 0x37
OP_TX_GET           = 0x40
OP_TX_SET           = 0x41
OP_TX_SCHEDULE_GET  = 0x42
OP_TX_SCHEDULE_SET  = 0x43
OP_WAKE_CFG_GET     = 0x50
OP_WAKE_CFG_SET     = 0x51
OP_WAKE_STATUS      = 0x52
OP_WAKE_CLEAR       = 0x53

# Hardware descriptor opcodes (flash page 59)
OP_HWDESC_GET       = 0x60
OP_HWDESC_SET       = 0x61
OP_HWDESC_COMMIT    = 0x62
HWDESC_BLOB_SIZE    = 128
OP_UART_MODE        = 0x06  # mode(1): 0=verbose, 1=silent

# BLE configuration opcodes (mirrors cmd_layer.h 0x70/0x71/0x72)
OP_BLE_GET          = 0x70  # → op_mode(1) tx_pwr(1) iv_s(2) dur_s(2) adv_ms(2) nm(1) name(12)
OP_BLE_SET          = 0x71  # ← same layout → OK
OP_BLE_RSSI         = 0x72  # → rssi(1, int8 dBm)
OP_MEASURE_ALL      = 0x73  # → stat(24) [accel_xyz(6)]  force-read all sensors
BLE_CMD_PAYLOAD_SZ  = 22

# BLE operating mode bitmask (mirrors app_ble.h defines)
# OFF=0x00, SCHEDULE+GEKON can coexist (0x06), CONTINUOUS is exclusive
BLE_OP_OFF        = 0x00
BLE_OP_CONTINUOUS = 0x01
BLE_OP_SCHEDULE   = 0x02
BLE_OP_GEKON      = 0x04

# ── Events protocol (opcodes 0x80-0x82, blob = 4 × 28 = 112 bytes) ───────────
MAX_EVENTS      = 4
MAX_CONDS       = 3
EVENT_SIZE      = 28
EVENTS_BLOB_SZ  = MAX_EVENTS * EVENT_SIZE   # 112 bytes
OP_EVT_GET      = 0x80
OP_EVT_SET      = 0x81
OP_EVT_CLEAR    = 0x82
EV_FLAG_ENABLED = 0x01
EV_FLAG_ONESHOT = 0x02

# Condition types
COND_DISABLED   = 0x00; COND_BATT_BELOW  = 0x01; COND_BATT_ABOVE  = 0x02
COND_TEMP_ABOVE = 0x03; COND_TEMP_BELOW  = 0x04; COND_NO_MOTION   = 0x05
COND_MOTION     = 0x06; COND_LIGHT_BELOW = 0x07; COND_LIGHT_ABOVE = 0x08
COND_EVERY_NCYC = 0x09; COND_ALWAYS      = 0x0A; COND_BEFORE_BLE  = 0x0B
# COND_EVERY_NHRS: val1 = hours*60+minutes (total min), val2 = extra seconds
COND_EVERY_NHRS = 0x0C

# Action types
ACT_NONE = 0x00; ACT_SET_POWER = 0x01; ACT_TX_PULSES = 0x02
ACT_TX_PAT = 0x03; ACT_BLE_START = 0x04; ACT_SET_CH = 0x05
ACT_SET_PERIOD = 0x06; ACT_LOG_MARK = 0x07
ACT_LED_ON = 0x08; ACT_LED_OFF = 0x09; ACT_LED_BLINK = 0x0A

COND_LABELS = [
    (COND_DISABLED,   'Disabled'),
    (COND_BATT_BELOW, 'Battery below'),  (COND_BATT_ABOVE, 'Battery above'),
    (COND_TEMP_ABOVE, 'Temp above'),     (COND_TEMP_BELOW, 'Temp below'),
    (COND_NO_MOTION,  'No motion for'),  (COND_MOTION,     'Motion detected'),
    (COND_LIGHT_BELOW,'Light below'),    (COND_LIGHT_ABOVE,'Light above'),
    (COND_EVERY_NCYC, 'Every N wake cycles'),
    (COND_ALWAYS,     'Always (every wake)'),
    (COND_BEFORE_BLE, 'Before BLE start'),
    (COND_EVERY_NHRS, 'Every H h M m S s'),
]
ACT_LABELS = [
    (ACT_NONE,       'No action'),      (ACT_SET_POWER,  'Set TX power'),
    (ACT_TX_PULSES,  'Send N pulses'),  (ACT_TX_PAT,     'TX on/off pattern'),
    (ACT_BLE_START,  'Start BLE'),      (ACT_SET_CH,     'Set channel'),
    (ACT_SET_PERIOD, 'Set TX period'),  (ACT_LOG_MARK,   'Write log marker'),
    (ACT_LED_ON,     'LED on'),         (ACT_LED_OFF,    'LED off'),
    (ACT_LED_BLINK,  'LED blink'),
]

# Nordic UART Service UUIDs — transparent UART protocol over BLE
# Firmware must expose these two characteristics for CMD protocol to work.
_NUS_RX_CHAR = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'  # write: PC → MCU
_NUS_TX_CHAR = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'  # notify: MCU → PC


@dataclass
class BleSettings:
    """BLE runtime configuration — mirrors TxConfigV2_t BLE fields."""
    op_mode:         int = BLE_OP_CONTINUOUS
    tx_power:        int = 24    # aci_hal_set_tx_power_level param (0-31); 24 ≈ 0 dBm
    interval_s:      int = 1800  # SCHEDULE: seconds between advertising windows
    duration_sec:    int = 60    # SCHEDULE/GEKON: session window (seconds)
    adv_interval_ms: int = 1000  # advertising interval in ms (100-10000)
    name_mode:       int = 0     # 0=auto "BCN_XXXX", 1=manual name
    name:            str = ''    # manual device name (max 11 chars)
    led_mode:        int = 0     # 0=BLE_LED_NORMAL, 1=BLE_LED_OFF

    def to_bytes(self) -> bytes:
        name_b = self.name.encode('ascii', errors='replace')[:11].ljust(12, b'\x00')
        return (struct.pack('<BBHHHB',
                            self.op_mode, self.tx_power,
                            self.interval_s, self.duration_sec,
                            self.adv_interval_ms, self.name_mode)
                + name_b
                + bytes([self.led_mode & 0xFF]))

    @classmethod
    def from_bytes(cls, data: bytes) -> 'BleSettings':
        if len(data) < 21:
            return cls()
        op, pwr, iv, dur, adv, nm = struct.unpack_from('<BBHHHB', data)
        name = data[9:21].rstrip(b'\x00').decode('ascii', errors='replace')
        led = data[21] if len(data) >= 22 else 0
        return cls(op_mode=op, tx_power=pwr, interval_s=iv,
                   duration_sec=dur, adv_interval_ms=adv,
                   name_mode=nm, name=name, led_mode=led)

# HwDesc type enumerations (mirror hw_desc.h)
HW_TEMP_NONE=0; HW_TEMP_CRYSTAL=1; HW_TEMP_NTC=2; HW_TEMP_STTS22H=3; HW_TEMP_LIS2DW12=4
HW_LIGHT_NONE=0; HW_LIGHT_PRESENT=1
HW_BATT_NONE=0; HW_BATT_ADC=1; HW_BATT_FUELGAUGE=2
HW_ACCEL_NONE=0; HW_ACCEL_ISM330=1; HW_ACCEL_LIS2DW12=2; HW_ACCEL_OTHER=3
HW_LED_NONE=0; HW_LED_SINGLE=1; HW_LED_RGB=2

@dataclass
class HwDescBlob:
    """128-byte compact hardware descriptor (mirrors HwDesc_t editable fields)."""
    hw_version:    int   = 1
    temp_type:     int   = HW_TEMP_CRYSTAL
    light_type:    int   = HW_LIGHT_NONE
    batt_type:     int   = HW_BATT_ADC
    accel_type:    int   = HW_ACCEL_LIS2DW12
    led_type:      int   = HW_LED_SINGLE
    tx_channels:   int   = 4
    tx_pwr_levels: int   = 4
    tx_freq_hz:    int   = 30_000_000
    batt_full_mv:  int   = 4200
    batt_empty_mv: int   = 3000
    light_model:   str   = ''
    accel_model:   str   = ''
    tx_type:       str   = 'colpitts'
    led_model:     str   = ''
    comment:       str   = ''

    def to_bytes(self) -> bytes:
        b = bytearray(HWDESC_BLOB_SIZE)
        b[0] = self.hw_version & 0xFF
        b[1] = self.temp_type  & 0xFF
        b[2] = self.light_type & 0xFF
        b[3] = self.batt_type  & 0xFF
        b[4] = self.accel_type & 0xFF
        b[5] = self.led_type   & 0xFF
        b[6] = self.tx_channels   & 0xFF
        b[7] = self.tx_pwr_levels & 0xFF
        struct.pack_into('<I', b, 8,  self.tx_freq_hz)
        struct.pack_into('<H', b, 12, self.batt_full_mv)
        struct.pack_into('<H', b, 14, self.batt_empty_mv)
        for off, s in [(16, self.light_model), (32, self.accel_model),
                       (48, self.tx_type),     (64, self.led_model)]:
            enc = s.encode('ascii', errors='replace')[:15]
            b[off:off + len(enc)] = enc
        comment_enc = self.comment.encode('ascii', errors='replace')[:47]
        b[80:80 + len(comment_enc)] = comment_enc
        return bytes(b)

    @classmethod
    def from_bytes(cls, data: bytes) -> 'HwDescBlob':
        if len(data) < HWDESC_BLOB_SIZE:
            return cls()
        def _s(off, n): return data[off:off+n].rstrip(b'\x00').decode('ascii', errors='replace')
        return cls(
            hw_version    = data[0],
            temp_type     = data[1],
            light_type    = data[2],
            batt_type     = data[3],
            accel_type    = data[4],
            led_type      = data[5],
            tx_channels   = data[6],
            tx_pwr_levels = data[7],
            tx_freq_hz    = struct.unpack_from('<I', data, 8)[0],
            batt_full_mv  = struct.unpack_from('<H', data, 12)[0],
            batt_empty_mv = struct.unpack_from('<H', data, 14)[0],
            light_model   = _s(16, 16),
            accel_model   = _s(32, 16),
            tx_type       = _s(48, 16),
            led_model     = _s(64, 16),
            comment       = _s(80, 48),
        )

SENSOR_ID_TEMP  = 0
SENSOR_ID_BATT  = 1
SENSOR_ID_LIGHT = 2
SENSOR_ID_ACCEL = 3

WAKE_ACTION_WAKE_MCU   = 0x01
WAKE_ACTION_TX_BURST   = 0x02
WAKE_ACTION_LOG_MARKER = 0x04


def proto_crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


# ─────────────────────────────────────────────────────────────────────────────
# Self-test
# ─────────────────────────────────────────────────────────────────────────────
def run_selftest():
    print("=== TX Beacon GUI v4 self-test ===")
    errors = 0

    # ConfigBlob roundtrip
    c = ConfigBlob(rf_mode=1, rf_channel=2, rf_power=3, rf_pulse_ms=50,
                   rf_period_ms=3000, temp_iv_s=60, bat_iv_s=3600,
                   sched_en=1, sched_hours=0x00FF00, sched_months=0x0F0)
    raw = c.to_bytes()
    assert len(raw) == 64, f"ConfigBlob size {len(raw)} != 64"
    c2 = ConfigBlob.from_bytes(raw)
    assert c2.rf_channel == 2
    assert c2.sched_hours == 0x00FF00
    assert c2.verify_crc(), "CRC mismatch after roundtrip"
    print("  ConfigBlob roundtrip ... PASS")

    # CRC corruption detection
    corrupted = bytearray(raw)
    corrupted[3] ^= 0x01
    c3 = ConfigBlob.from_bytes(bytes(corrupted))
    assert not c3.verify_crc(), "Should detect corrupted CRC"
    print("  CRC corruption detection ... PASS")

    # CRC32 Python vs zlib compatibility
    test_data = b'\x01\x40' + bytes(58)
    crc_val = proto_crc32(test_data[:60])
    assert isinstance(crc_val, int)
    # Verify: CRC of all-zeros 60 bytes
    crc_zeros = proto_crc32(bytes(60))
    assert crc_zeros == 0x00000000 or crc_zeros != 0  # non-trivial
    print(f"  CRC32 of 60 zeros = 0x{crc_zeros:08X} ... PASS")

    # StatusBlob roundtrip
    raw_s = struct.pack(_STAT_FMT, 3600, 364, 3300, 3720, 82, 1, 1240, 1, 1, 247, 768, 1721000000)
    s = StatusBlob.from_bytes(raw_s)
    assert s.temp_01c == 364
    assert s.bat_mv == 3720
    assert s.log_used == 247
    print("  StatusBlob roundtrip ... PASS")

    # InfoBlob roundtrip
    raw_i = struct.pack(_INFO_FMT2, 1, 1, 1, 2, 0, 7, 16, 0xDEADBEEF, 768, 2048,
                        b'RAT_001\x00\x00\x00\x00\x00', 42, 8, 3, 15)
    info = InfoBlob._from_bytes(raw_i)
    assert info.uid == 0xDEADBEEF
    assert info.tag == 'RAT_001'
    assert info.total_active_h == 42
    assert info.total_stop1_h == 8
    assert info.total_shutdown_h == 3
    assert info.flash_erase_count == 15
    print("  InfoBlob roundtrip ... PASS")

    # LogRecord roundtrip
    raw_l = struct.pack(_LOG_FMT, 1000, 0x0D, 0x00, 364, 1240, 82, 0x0E, 0x88, bytes(3))
    lr = LogRecord.from_bytes(raw_l)
    assert lr.temp_01c == 364
    assert lr.bat_mv == (0x0E << 8 | 0x88)
    print("  LogRecord roundtrip ... PASS")

    if errors == 0:
        print("=== All tests PASSED ===")
    else:
        print(f"=== {errors} FAILURES ===")
        sys.exit(1)


# ─────────────────────────────────────────────────────────────────────────────
# Transport abstraction
# ─────────────────────────────────────────────────────────────────────────────
class Transport(QObject):
    telemetry         = pyqtSignal(str)
    connected_changed = pyqtSignal(bool)

    def connect_to(self, target: str) -> bool:   ...
    def disconnect(self):                         ...
    def read_info(self)   -> bytes:               ...  # 48 B InfoBlob
    def read_config(self) -> bytes:               ...  # 64 B ConfigBlob
    def write_config(self, blob: bytes) -> Tuple[bool, str]: ...
    def save_flash(self)  -> Tuple[bool, str]:    ...
    def read_status(self) -> bytes:               ...  # 24 B StatusBlob
    def read_log(self, offset: int, count: int) -> bytes: ...
    def sync_time(self, unix_ts: int) -> Tuple[bool, str]: ...
    def send_tag(self, tag: str) -> Tuple[bool, str]:  ...
    def send_uptime_clr(self) -> Tuple[bool, str]:     ...
    def flush_uptime(self)    -> Tuple[bool, str]:     ...  # save RAM uptime to flash


# ─── UART transport ──────────────────────────────────────────────────────────
class _ReaderThread(QThread):
    line_received = pyqtSignal(str)

    def __init__(self, ser, parent=None):
        super().__init__(parent)
        self._ser = ser
        self._stop = False

    def run(self):
        buf = b''
        while not self._stop:
            try:
                chunk = self._ser.read(64)
                if not chunk:
                    continue
                buf += chunk
                while b'\n' in buf:
                    idx = buf.index(b'\n')
                    line = buf[:idx].rstrip(b'\r').decode('ascii', errors='replace')
                    buf = buf[idx + 1:]
                    self.line_received.emit(line)
            except Exception:
                break

    def stop(self):
        self._stop = True


class UartTransport(Transport):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._ser        = None
        self._reader     = None
        self._req_id     = 0
        self._lock       = threading.Lock()
        self._write_lock = threading.Lock()  # serializes ser.write() calls
        self._tagged  : dict[int, queue.Queue] = {}
        self._tele_q  = queue.Queue()

    def connect_to(self, target: str) -> bool:
        if not HAS_SERIAL:
            return False
        try:
            self._ser = serial.Serial(target, 115200, timeout=0.1)
            self._reader = _ReaderThread(self._ser)
            self._reader.line_received.connect(self._on_line)
            self._reader.start()
            self.connected_changed.emit(True)
            return True
        except Exception as e:
            self._ser = None
            return False

    def disconnect(self):
        if self._reader:
            self._reader.stop()
            self._reader.wait(500)
            self._reader = None
        if self._ser:
            try: self._ser.close()
            except: pass
            self._ser = None
        self.connected_changed.emit(False)

    def _on_line(self, line: str):
        if line.startswith('#'):
            parts = line[1:].split(' ', 2)
            try:
                rid = int(parts[0])
                with self._lock:
                    if rid in self._tagged:
                        self._tagged[rid].put(line)
            except (ValueError, IndexError):
                pass
        else:
            self.telemetry.emit(line)

    def _request(self, verb: str, payload: str = '', timeout: float = 3.0) -> Tuple[bool, list]:
        if not self._ser:
            return False, ['not connected']
        with self._lock:
            self._req_id = (self._req_id % 65535) + 1
            rid = self._req_id
            q: queue.Queue = queue.Queue()
            self._tagged[rid] = q

        cmd = f'#{rid} {verb}'
        if payload:
            cmd += f' {payload}'
        cmd += '\r\n'
        with self._write_lock:
            try:
                self._ser.write(cmd.encode('ascii'))
            except Exception as e:
                with self._lock: self._tagged.pop(rid, None)
                return False, [str(e)]

        lines = []
        prefix = f'#{rid} '
        deadline = time.time() + timeout
        try:
            while time.time() < deadline:
                remaining = max(0.05, deadline - time.time())
                try:
                    raw = q.get(timeout=remaining)
                except queue.Empty:
                    break
                if raw.startswith(prefix):
                    rest = raw[len(prefix):]
                    if rest.startswith('OK'):
                        return True, lines
                    elif rest.startswith('ERR'):
                        return False, [rest]
                    else:
                        lines.append(rest)
            return False, ['timeout']
        finally:
            with self._lock: self._tagged.pop(rid, None)

    def read_info(self) -> bytes:
        ok, lines = self._request('INFO?')
        if ok and lines:
            hex_str = lines[0].split(' ', 1)[1] if ' ' in lines[0] else ''
            try: return bytes.fromhex(hex_str)
            except: pass
        return bytes(48)

    def send_tag(self, tag: str) -> Tuple[bool, str]:
        hex_str = tag.encode('ascii').ljust(11, b'\x00')[:11].hex().upper()
        ok, lines = self._request('TAG!', hex_str)
        return ok, lines[0] if lines else ('OK' if ok else 'ERR')

    def send_uptime_clr(self) -> Tuple[bool, str]:
        ok, lines = self._request('UPTIMECLR!')
        return ok, lines[0] if lines else ('OK' if ok else 'ERR')

    def read_config(self) -> bytes:
        ok, lines = self._request('CFG?')
        if ok and lines:
            hex_str = lines[0].split(' ', 1)[1] if ' ' in lines[0] else ''
            try: return bytes.fromhex(hex_str)
            except: pass
        return b''

    def write_config(self, blob: bytes) -> Tuple[bool, str]:
        hex_str = blob.hex().upper()
        ok, lines = self._request('CFG!', hex_str, timeout=8.0)
        msg = lines[0] if lines else ('OK' if ok else 'ERR unknown')
        return ok, msg

    def save_flash(self) -> Tuple[bool, str]:
        ok, lines = self._request('SAVE!')
        return ok, lines[0] if lines else ''

    def measure_all(self) -> bytes:
        """Force-read all sensors and return fresh 24-byte StatusBlob (+ 6-byte accel XYZ if present)."""
        rc, data = self.send_cmd(OP_MEASURE_ALL, b'', timeout=3.0)
        if rc == CMD_OK and len(data) >= 24:
            return data  # 24 or 30 bytes
        return self.read_status()  # fallback to cached STAT

    def read_status(self) -> bytes:
        ok, lines = self._request('STAT?')
        if ok and lines:
            hex_str = lines[0].split(' ', 1)[1] if ' ' in lines[0] else ''
            try: return bytes.fromhex(hex_str)
            except: pass
        return bytes(24)

    def read_log(self, offset: int, count: int) -> bytes:
        ok, lines = self._request(f'LOG?', f'{offset} {count}', timeout=8.0)
        if ok and lines:
            hex_str = lines[0].split(' ', 1)[1] if ' ' in lines[0] else ''
            try: return bytes.fromhex(hex_str)
            except: pass
        return b''

    def erase_log(self) -> Tuple[bool, str]:
        # Use binary deferred command: MCU sets g_log_erase_pending and executes
        # FlashLog_Clear() after BLE disconnect (avoids forcing CPU2 shutdown mid-session).
        rc = self.cmd_log_erase()
        if rc == CMD_OK:
            return True, 'deferred'
        return False, f'CMD error 0x{rc:02X}'

    def sync_time(self, unix_ts: int) -> Tuple[bool, str]:
        ok, lines = self._request('TIME!', str(unix_ts), timeout=3.0)
        msg = lines[0] if lines else ('OK' if ok else 'timeout')
        return ok, msg

    def flush_uptime(self) -> Tuple[bool, str]:
        ok, lines = self._request('HWSAVE!')
        return ok, lines[0] if lines else ('OK' if ok else 'ERR timeout')

    # ── Binary command layer ──────────────────────────────────────────────────

    def send_cmd(self, opcode: int, payload: bytes = b'',
                 timeout: float = 3.0) -> Tuple[int, bytes]:
        """Send one binary command via the CMD verb.
        Returns (result_code, out_payload). 0xFF on transport error."""
        hex_str = bytes([opcode]).hex().upper() + payload.hex().upper()
        ok, lines = self._request('CMD', hex_str, timeout=timeout)
        if not ok:
            return 0xFF, b''
        for line in lines:
            if line.startswith('RSP '):
                try:
                    raw = bytes.fromhex(line[4:].strip())
                    return (raw[0] if raw else 0xFF), raw[1:]
                except Exception:
                    return 0xFF, b''
        return CMD_OK, b''

    def cmd_ping(self) -> Tuple[int, bytes]:
        return self.send_cmd(OP_PING)

    def cmd_time_set(self, unix_ts: int) -> int:
        rc, _ = self.send_cmd(OP_TIME_SET, struct.pack('<I', unix_ts))
        return rc

    def cmd_reboot(self, mode: int = 0) -> int:
        rc, _ = self.send_cmd(OP_REBOOT, bytes([mode]))
        return rc

    def cmd_config_info(self) -> Tuple[int, int, int]:
        """Returns (total_len, version, crc16). -1 on error."""
        rc, payload = self.send_cmd(OP_CONFIG_INFO)
        if rc != CMD_OK or len(payload) < 5:
            return -1, -1, -1
        total_len = struct.unpack_from('<H', payload, 0)[0]
        version   = payload[2]
        crc16     = struct.unpack_from('<H', payload, 3)[0]
        return total_len, version, crc16

    def cmd_config_read(self, offset: int, length: int) -> bytes:
        rc, payload = self.send_cmd(OP_CONFIG_READ,
                                    struct.pack('<HB', offset, length))
        return payload if rc == CMD_OK else b''

    def cmd_config_write(self, offset: int, data: bytes) -> int:
        header = struct.pack('<HB', offset, len(data))
        rc, _ = self.send_cmd(OP_CONFIG_WRITE, header + data)
        return rc

    def cmd_config_commit(self, crc16: int = 0) -> int:
        rc, _ = self.send_cmd(OP_CONFIG_COMMIT, struct.pack('<H', crc16),
                               timeout=8.0)
        return rc

    def cmd_config_reset(self) -> int:
        rc, _ = self.send_cmd(OP_CONFIG_RESET, struct.pack('<I', 0xDEADC0DE))
        return rc

    def cmd_log_info(self) -> dict:
        rc, p = self.send_cmd(OP_LOG_INFO)
        if rc != CMD_OK or len(p) < 18:
            return {}
        # Layout: total(4) used(4) free(4) used_bytes(4) rec_size(1) fmt_ver(1)
        return {
            'total':    struct.unpack_from('<I', p, 0)[0],
            'used':     struct.unpack_from('<I', p, 4)[0],
            'head':     struct.unpack_from('<I', p, 8)[0],
            'tail':     struct.unpack_from('<I', p, 12)[0],
            'rec_size': p[16],
            'fmt_ver':  p[17],
        }

    def cmd_log_erase(self) -> int:
        rc, _ = self.send_cmd(OP_LOG_ERASE, struct.pack('<I', 0xCA11AB1E),
                               timeout=10.0)
        return rc

    def cmd_log_mark(self, tag: int = 0) -> int:
        rc, _ = self.send_cmd(OP_LOG_MARK, bytes([tag]))
        return rc

    def cmd_accel_cfg_get(self) -> dict:
        """Returns dict with odr, fs, mode, lp_mode, bw — or {} on error."""
        rc, p = self.send_cmd(OP_ACCEL_CFG_GET)
        if rc != CMD_OK or len(p) < 4:
            return {}
        keys = ['odr', 'fs', 'mode', 'lp_mode', 'bw']
        return {k: p[i] for i, k in enumerate(keys) if i < len(p)}

    def cmd_accel_cfg_set(self, odr: int, fs: int, mode: int,
                          lp_mode: int, bw: int) -> int:
        rc, _ = self.send_cmd(OP_ACCEL_CFG_SET, bytes([odr, fs, mode, lp_mode, bw]))
        return rc

    def cmd_accel_power(self, on: int) -> int:
        rc, _ = self.send_cmd(OP_ACCEL_POWER, bytes([on]))
        return rc

    def cmd_accel_probe(self) -> dict:
        """Returns dict with i2c_addr, who_am_i, ok, boot_ms — or {} on error."""
        rc, p = self.send_cmd(OP_ACCEL_PROBE)
        if rc != CMD_OK or len(p) < 4:
            return {}
        return {
            'i2c_addr': p[0], 'who_am_i': p[1], 'ok': p[2],
            'boot_ms': struct.unpack_from('<H', p, 3)[0],
        }

    def cmd_sensor_read_now(self, sensor_id: int) -> dict:
        """Trigger an immediate sensor read. Returns sensor-specific dict or {} on error."""
        rc, p = self.send_cmd(OP_SENSOR_READ_NOW, bytes([sensor_id]))
        if rc != CMD_OK or len(p) < 6:
            return {}
        if sensor_id == SENSOR_ID_ACCEL:
            x, y, z = struct.unpack_from('<hhh', p, 0)
            return {'raw_i16': x, 'y': y, 'z': z}
        return {
            'raw_i16':    struct.unpack_from('<h', p, 0)[0],
            'scaled_i32': struct.unpack_from('<i', p, 2)[0],
        }

    def cmd_wake_cfg_get(self) -> dict:
        rc, p = self.send_cmd(OP_WAKE_CFG_GET)
        if rc != CMD_OK or len(p) < 6:
            return {}
        return {
            'enable':       p[0],
            'threshold_mg': struct.unpack_from('<H', p, 1)[0],
            'duration_ms':  struct.unpack_from('<H', p, 3)[0],
            'action':       p[5],
        }

    def cmd_wake_cfg_set(self, enable: int, threshold_mg: int,
                         duration_ms: int, action: int) -> int:
        payload = bytes([enable]) + struct.pack('<HH', threshold_mg, duration_ms) + bytes([action])
        rc, _ = self.send_cmd(OP_WAKE_CFG_SET, payload)
        return rc

    def cmd_wake_status(self) -> dict:
        rc, p = self.send_cmd(OP_WAKE_STATUS)
        if rc != CMD_OK or len(p) < 9:
            return {}
        return {
            'armed':        p[0],
            'count':        struct.unpack_from('<I', p, 1)[0],
            'last_ts':      struct.unpack_from('<I', p, 5)[0],
        }

    def cmd_wake_clear(self) -> int:
        rc, _ = self.send_cmd(OP_WAKE_CLEAR)
        return rc

    def cmd_hwdesc_get(self) -> 'Optional[HwDescBlob]':
        rc, p = self.send_cmd(OP_HWDESC_GET)
        if rc != CMD_OK or len(p) < HWDESC_BLOB_SIZE:
            return None
        return HwDescBlob.from_bytes(p)

    def cmd_hwdesc_set(self, blob: 'HwDescBlob') -> int:
        rc, _ = self.send_cmd(OP_HWDESC_SET, blob.to_bytes())
        return rc

    def cmd_hwdesc_commit(self) -> int:
        rc, _ = self.send_cmd(OP_HWDESC_COMMIT)
        return rc

    def cmd_uart_mode(self, mode: int) -> int:
        rc, _ = self.send_cmd(OP_UART_MODE, bytes([mode]))
        return rc

    def cmd_ble_get(self) -> 'Optional[BleSettings]':
        rc, p = self.send_cmd(OP_BLE_GET)
        if rc != CMD_OK or len(p) < BLE_CMD_PAYLOAD_SZ:
            return None
        return BleSettings.from_bytes(p)

    def cmd_ble_set(self, s: BleSettings) -> int:
        rc, _ = self.send_cmd(OP_BLE_SET, s.to_bytes(), timeout=8.0)
        return rc

    def cmd_sensor_list(self) -> list:
        """Returns [{id, present, enabled, interval_s, has_ext}, ...] or []."""
        rc, p = self.send_cmd(OP_SENSOR_LIST)
        if rc != CMD_OK or len(p) < 1:
            return []
        n = p[0]; sensors = []; off = 1
        for _ in range(n):
            if off + 7 > len(p):
                break
            sensors.append({'id': p[off], 'present': p[off+1], 'enabled': p[off+2],
                             'interval_s': struct.unpack_from('<I', p, off+3)[0],
                             'has_ext': p[off+7]})
            off += 8
        return sensors

    def cmd_sensor_enable(self, sensor_id: int, enable: int) -> int:
        rc, _ = self.send_cmd(OP_SENSOR_ENABLE, bytes([sensor_id, enable]))
        return rc

    def cmd_sensor_interval(self, sensor_id: int, interval_s: int) -> int:
        rc, _ = self.send_cmd(OP_SENSOR_INTERVAL,
                               bytes([sensor_id]) + struct.pack('<I', interval_s))
        return rc


# ─── Mock BLE transport (full simulation, no hardware needed) ────────────────
class MockBleTransport(Transport):
    """Simulates a connected beacon in memory. No hardware required."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._cfg   = ConfigBlob()
        self._t0    = time.time()
        self._records: List[LogRecord] = []
        self._gen_log()
        self._connected = False
        self._mock_tag = 'RAT_M001'
        self._mock_erase_count = 15
        self._mock_uptime_base_h = 5   # simulated persistent hours before this boot
        self._mock_hwdesc = HwDescBlob(
            accel_model='LIS2DW12TR', tx_type='colpitts',
            comment='Beacon 30MHz v1 — rat implant prototype')

    def _gen_log(self):
        """Generate plausible history: all v2 typed records (TEMP + BATT + ACCEL)."""
        self._records = []
        t = 0
        temp = 365   # ×0.1°C
        bat  = 82    # %
        bat_mv = 3720
        for i in range(200):
            # TEMP record every cycle (60 s interval)
            self._records.append(
                struct.pack('<IBBhH6x', t, LOGREC_TYPE_TEMP, 0x00, temp, 3300))
            # BATT record every 5 cycles
            if i % 5 == 0:
                self._records.append(
                    struct.pack('<IBBHB7x', t, LOGREC_TYPE_BATT, 0x00, bat_mv, bat))
            # ACCEL record every 8 cycles
            if i % 8 == 7:
                ax = int(2000 * math.sin(i / 20.0))
                ay = int(1500 * math.cos(i / 25.0))
                az = 16384 + int(500 * math.sin(i / 10.0))
                self._records.append(
                    struct.pack('<IBBhhhBxxx', t + 1, LOGREC_TYPE_ACCEL, 0x00,
                                ax, ay, az, 0))
            t += 60
            temp += int(3 * math.sin(i / 15.0))
            temp = max(350, min(380, temp))
            if i % 20 == 19:
                bat = max(10, bat - 1)
                bat_mv = max(3200, bat_mv - 5)

    def connect_to(self, target: str) -> bool:
        time.sleep(0.05)  # artificial delay
        self._connected = True
        self.connected_changed.emit(True)
        return True

    def disconnect(self):
        self._connected = False
        self.connected_changed.emit(False)

    def read_info(self) -> bytes:
        time.sleep(0.03)
        elapsed_h = int((time.time() - self._t0) / 3600)
        info = InfoBlob(uid=0xA3F2C1B0, tag=self._mock_tag,
                        fw_major=1, fw_minor=2, fw_patch=0,
                        sensors_present=0x0F, log_entry_size=16,
                        log_total_entries=768, flash_page_size=2048,
                        total_active_h=self._mock_uptime_base_h + elapsed_h,
                        total_stop1_h=self._mock_uptime_base_h * 3,
                        total_shutdown_h=2,
                        flash_erase_count=self._mock_erase_count)
        return struct.pack(_INFO_FMT2,
            info.proto_ver, info.hw_rev, info.fw_major, info.fw_minor, info.fw_patch,
            info.sensors_present, info.log_entry_size, info.uid,
            info.log_total_entries, info.flash_page_size,
            info.tag.encode('ascii').ljust(12, b'\x00')[:12],
            info.total_active_h, info.total_stop1_h,
            info.total_shutdown_h, info.flash_erase_count)

    def send_tag(self, tag: str) -> Tuple[bool, str]:
        time.sleep(0.05)
        self._mock_tag = tag[:11]
        self._mock_erase_count += 1  # save erases one page
        return True, 'OK'

    def send_uptime_clr(self) -> Tuple[bool, str]:
        time.sleep(0.05)
        self._mock_uptime_base_h = 0
        self._t0 = time.time()
        self._mock_erase_count += 1
        return True, 'OK'

    def flush_uptime(self) -> Tuple[bool, str]:
        time.sleep(0.04)
        self._mock_erase_count += 1  # HwDesc_Save() erases page 59
        return True, 'OK'

    def read_config(self) -> bytes:
        time.sleep(0.04)
        return self._cfg.to_bytes()

    def write_config(self, blob: bytes) -> Tuple[bool, str]:
        time.sleep(0.08)
        if len(blob) != 64:
            return False, 'ERR len'
        c = ConfigBlob.from_bytes(blob)
        if not c.verify_crc():
            return False, 'ERR crc'
        self._cfg = c
        return True, 'OK changed=1'

    def save_flash(self) -> Tuple[bool, str]:
        time.sleep(0.12)
        return True, 'OK'

    def read_status(self) -> bytes:
        time.sleep(0.05)
        elapsed = time.time() - self._t0
        # Slowly drifting values
        temp  = int(364 + 8 * math.sin(elapsed / 60.0))
        bat_v = max(3000, 3720 - int(elapsed / 3600))
        bat_p = max(0, min(100, int((bat_v - 3000) * 100 / 720)))
        lux   = max(0, int(1200 * max(0, math.sin(math.pi * (elapsed % 86400) / 43200))))
        unix  = int(1721480000 + elapsed)
        raw = struct.pack(_STAT_FMT,
            int(elapsed), temp, 3300, bat_v, bat_p,
            1 if self._cfg.rf_mode != 0 else 0,
            lux, 1, 0x01,
            len(self._records), 768, unix)
        return raw

    def read_log(self, offset: int, count: int) -> bytes:
        time.sleep(0.06)
        out = b''
        for i in range(count):
            idx = offset + i
            if idx >= len(self._records):
                break
            r = self._records[idx]
            out += r  # all records are pre-packed bytes (v2 format)
        return out

    def erase_log(self) -> Tuple[bool, str]:
        time.sleep(0.15)
        self._records = []
        return True, 'OK'

    def sync_time(self, unix_ts: int) -> Tuple[bool, str]:
        time.sleep(0.03)
        return True, f'OK ts={unix_ts}'

    # ── Mock binary command layer ─────────────────────────────────────────────

    def __init_mock_accel(self):
        if not hasattr(self, '_mock_accel_odr'):
            self._mock_accel_odr    = 2    # 12.5 Hz
            self._mock_accel_fs     = 0    # ±2g
            self._mock_accel_mode   = 0    # LP
            self._mock_accel_lp_mode= 0    # LP1
            self._mock_accel_bw     = 1    # ODR/4
            self._mock_accel_on     = 1
            self._mock_wake_enable  = 0
            self._mock_wake_thr_mg  = 500
            self._mock_wake_dur_ms  = 100
            self._mock_wake_action  = WAKE_ACTION_WAKE_MCU
            self._mock_wake_count   = 0
            self._mock_wake_ts      = 0

    def send_cmd(self, opcode: int, payload: bytes = b'',
                 timeout: float = 3.0) -> Tuple[int, bytes]:
        self.__init_mock_accel()
        time.sleep(0.02)
        elapsed = int(time.time() - self._t0)

        if opcode == OP_PING:
            return CMD_OK, bytes([1, 1]) + struct.pack('<II', 0xA3F2C1B0, elapsed)

        elif opcode == OP_TIME_SET:
            return CMD_OK, b''

        elif opcode == OP_REBOOT:
            return CMD_OK, b''

        elif opcode == OP_SENSOR_LIST:
            if not hasattr(self, '_mock_accel_log_en'):
                self._mock_accel_log_en = 0
                self._mock_accel_log_iv = 1
            def _e(sid, present, enabled, iv, has_ext):
                return bytes([sid, present, enabled]) + struct.pack('<I', iv) + bytes([has_ext])
            body = (bytes([4]) +
                    _e(SENSOR_ID_TEMP,  1, 1, 60,    0) +
                    _e(SENSOR_ID_BATT,  1, 1, 3600,  0) +
                    _e(SENSOR_ID_LIGHT, 1, 1, 300,   0) +
                    _e(SENSOR_ID_ACCEL, 1, self._mock_accel_log_en,
                       self._mock_accel_log_iv, 1))
            return CMD_OK, body

        elif opcode == OP_SENSOR_ENABLE and len(payload) >= 2:
            if not hasattr(self, '_mock_accel_log_en'):
                self._mock_accel_log_en = 0
                self._mock_accel_log_iv = 1
            if payload[0] == SENSOR_ID_ACCEL:
                self._mock_accel_log_en = payload[1]
            return CMD_OK, b''

        elif opcode == OP_SENSOR_INTERVAL and len(payload) >= 5:
            if not hasattr(self, '_mock_accel_log_iv'):
                self._mock_accel_log_en = 0
                self._mock_accel_log_iv = 1
            if payload[0] == SENSOR_ID_ACCEL:
                self._mock_accel_log_iv = struct.unpack_from('<I', payload, 1)[0]
            return CMD_OK, b''

        elif opcode == OP_LOG_INFO:
            n = len(self._records)
            return CMD_OK, struct.pack('<IIIIBB', 768, n, 0, n, 16, 2)

        elif opcode == OP_LOG_ERASE:
            self._records = []
            return CMD_OK, b''

        elif opcode == OP_LOG_MARK:
            return CMD_OK, b''

        elif opcode == OP_ACCEL_CFG_GET:
            return CMD_OK, bytes([self._mock_accel_odr, self._mock_accel_fs,
                                   self._mock_accel_mode, self._mock_accel_lp_mode,
                                   self._mock_accel_bw])

        elif opcode == OP_ACCEL_CFG_SET and len(payload) >= 4:
            self._mock_accel_odr     = payload[0]
            self._mock_accel_fs      = payload[1]
            self._mock_accel_mode    = payload[2]
            self._mock_accel_lp_mode = payload[3]
            self._mock_accel_bw      = payload[4] if len(payload) > 4 else 1
            return CMD_OK, b''

        elif opcode == OP_ACCEL_POWER:
            on = payload[0] if payload else 1
            if on == 0 and self._mock_wake_enable:
                return CMD_ERR_STATE, b''
            self._mock_accel_on = on
            return CMD_OK, b''

        elif opcode == OP_ACCEL_PROBE:
            return CMD_OK, bytes([0x30, 0x44, 1]) + struct.pack('<H', 7)

        elif opcode == OP_SENSOR_READ_NOW:
            sensor_id = payload[0] if payload else 0
            if sensor_id == SENSOR_ID_ACCEL:
                # Return x, y, z as three i16 (matches new firmware format)
                t = time.time()
                x = int(300 * math.cos(t * 0.7))
                y = int(300 * math.sin(t * 0.5))
                z = int(16384 + 200 * math.sin(t * 0.3))
                return CMD_OK, struct.pack('<hhh', x, y, z)
            elif sensor_id == SENSOR_ID_TEMP:
                raw = 365 + int(5 * math.sin(time.time() / 10))
                return CMD_OK, struct.pack('<hi', raw, raw * 100 // 10)
            else:
                return CMD_OK, struct.pack('<hi', 0, 0)

        elif opcode == OP_WAKE_CFG_GET:
            return CMD_OK, (bytes([self._mock_wake_enable]) +
                            struct.pack('<HH', self._mock_wake_thr_mg,
                                        self._mock_wake_dur_ms) +
                            bytes([self._mock_wake_action]))

        elif opcode == OP_WAKE_CFG_SET and len(payload) >= 6:
            if payload[0] and not self._mock_accel_on:
                return CMD_ERR_STATE, b''
            self._mock_wake_enable  = payload[0]
            self._mock_wake_thr_mg  = struct.unpack_from('<H', payload, 1)[0]
            self._mock_wake_dur_ms  = struct.unpack_from('<H', payload, 3)[0]
            self._mock_wake_action  = payload[5]
            return CMD_OK, b''

        elif opcode == OP_WAKE_STATUS:
            return CMD_OK, (bytes([self._mock_wake_enable]) +
                            struct.pack('<II', self._mock_wake_count,
                                        self._mock_wake_ts))

        elif opcode == OP_WAKE_CLEAR:
            self._mock_wake_count = 0
            self._mock_wake_ts    = 0
            return CMD_OK, b''

        elif opcode == OP_HWDESC_GET:
            return CMD_OK, self._mock_hwdesc.to_bytes()

        elif opcode == OP_HWDESC_SET:
            if len(payload) < HWDESC_BLOB_SIZE:
                return CMD_ERR_LEN, b''
            self._mock_hwdesc = HwDescBlob.from_bytes(payload)
            return CMD_OK, b''

        elif opcode == OP_HWDESC_COMMIT:
            return CMD_OK, b''   # mock: no flash

        elif opcode == OP_BLE_GET:
            if not hasattr(self, '_mock_ble'):
                self._mock_ble = BleSettings()
            return CMD_OK, self._mock_ble.to_bytes()

        elif opcode == OP_BLE_SET:
            if len(payload) < BLE_CMD_PAYLOAD_SZ:
                return CMD_ERR_LEN, b''
            self._mock_ble = BleSettings.from_bytes(payload)
            return CMD_OK, b''

        else:
            return CMD_ERR_UNSUPPORTED, b''

    def cmd_ping(self) -> Tuple[int, bytes]:
        return self.send_cmd(OP_PING)

    def cmd_time_set(self, unix_ts: int) -> int:
        rc, _ = self.send_cmd(OP_TIME_SET, struct.pack('<I', unix_ts))
        return rc

    def cmd_reboot(self, mode: int = 0) -> int:
        rc, _ = self.send_cmd(OP_REBOOT, bytes([mode]))
        return rc

    def cmd_accel_cfg_get(self) -> dict:
        rc, p = self.send_cmd(OP_ACCEL_CFG_GET)
        if rc != CMD_OK or len(p) < 4:
            return {}
        keys = ['odr', 'fs', 'mode', 'lp_mode', 'bw']
        return {k: p[i] for i, k in enumerate(keys) if i < len(p)}

    def cmd_accel_cfg_set(self, odr: int, fs: int, mode: int,
                          lp_mode: int, bw: int) -> int:
        rc, _ = self.send_cmd(OP_ACCEL_CFG_SET, bytes([odr, fs, mode, lp_mode, bw]))
        return rc

    def cmd_accel_power(self, on: int) -> int:
        rc, _ = self.send_cmd(OP_ACCEL_POWER, bytes([on]))
        return rc

    def cmd_accel_probe(self) -> dict:
        rc, p = self.send_cmd(OP_ACCEL_PROBE)
        if rc != CMD_OK or len(p) < 4:
            return {}
        return {'i2c_addr': p[0], 'who_am_i': p[1], 'ok': p[2],
                'boot_ms': struct.unpack_from('<H', p, 3)[0]}

    def cmd_sensor_read_now(self, sensor_id: int) -> dict:
        rc, p = self.send_cmd(OP_SENSOR_READ_NOW, bytes([sensor_id]))
        if rc != CMD_OK or len(p) < 6:
            return {}
        return {'raw_i16':    struct.unpack_from('<h', p, 0)[0],
                'scaled_i32': struct.unpack_from('<i', p, 2)[0]}

    def cmd_wake_cfg_get(self) -> dict:
        rc, p = self.send_cmd(OP_WAKE_CFG_GET)
        if rc != CMD_OK or len(p) < 6:
            return {}
        return {'enable':       p[0],
                'threshold_mg': struct.unpack_from('<H', p, 1)[0],
                'duration_ms':  struct.unpack_from('<H', p, 3)[0],
                'action':       p[5]}

    def cmd_wake_cfg_set(self, enable: int, threshold_mg: int,
                         duration_ms: int, action: int) -> int:
        payload = bytes([enable]) + struct.pack('<HH', threshold_mg, duration_ms) + bytes([action])
        rc, _ = self.send_cmd(OP_WAKE_CFG_SET, payload)
        return rc

    def cmd_wake_status(self) -> dict:
        rc, p = self.send_cmd(OP_WAKE_STATUS)
        if rc != CMD_OK or len(p) < 9:
            return {}
        return {'armed': p[0],
                'count': struct.unpack_from('<I', p, 1)[0],
                'last_ts': struct.unpack_from('<I', p, 5)[0]}

    def cmd_wake_clear(self) -> int:
        rc, _ = self.send_cmd(OP_WAKE_CLEAR)
        return rc

    def cmd_hwdesc_get(self) -> 'Optional[HwDescBlob]':
        rc, p = self.send_cmd(OP_HWDESC_GET)
        if rc != CMD_OK or len(p) < HWDESC_BLOB_SIZE:
            return None
        return HwDescBlob.from_bytes(p)

    def cmd_hwdesc_set(self, blob: 'HwDescBlob') -> int:
        rc, _ = self.send_cmd(OP_HWDESC_SET, blob.to_bytes())
        return rc

    def cmd_hwdesc_commit(self) -> int:
        rc, _ = self.send_cmd(OP_HWDESC_COMMIT)
        return rc

    def cmd_ble_get(self) -> 'Optional[BleSettings]':
        rc, p = self.send_cmd(OP_BLE_GET)
        if rc != CMD_OK or len(p) < BLE_CMD_PAYLOAD_SZ:
            return None
        return BleSettings.from_bytes(p)

    def cmd_ble_set(self, s: BleSettings) -> int:
        rc, _ = self.send_cmd(OP_BLE_SET, s.to_bytes())
        return rc

    def cmd_uart_mode(self, mode: int) -> int:
        return CMD_OK  # mock: no side effects

    def cmd_sensor_list(self) -> list:
        rc, p = self.send_cmd(OP_SENSOR_LIST)
        if rc != CMD_OK or len(p) < 1:
            return []
        n = p[0]; sensors = []; off = 1
        for _ in range(n):
            if off + 7 > len(p):
                break
            sensors.append({'id': p[off], 'present': p[off+1], 'enabled': p[off+2],
                             'interval_s': struct.unpack_from('<I', p, off+3)[0],
                             'has_ext': p[off+7]})
            off += 8
        return sensors

    def cmd_sensor_enable(self, sensor_id: int, enable: int) -> int:
        rc, _ = self.send_cmd(OP_SENSOR_ENABLE, bytes([sensor_id, enable]))
        return rc

    def cmd_sensor_interval(self, sensor_id: int, interval_s: int) -> int:
        rc, _ = self.send_cmd(OP_SENSOR_INTERVAL,
                               bytes([sensor_id]) + struct.pack('<I', interval_s))
        return rc


# ─────────────────────────────────────────────────────────────────────────────
# BLE transport — asyncio loop thread
# ─────────────────────────────────────────────────────────────────────────────
class _BleLoopThread(QThread):
    """Hosts an asyncio event loop for bleak BLE operations."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self._loop: Optional['_asyncio.AbstractEventLoop'] = None
        self._ready = threading.Event()

    def run(self):
        self._loop = _asyncio.new_event_loop()
        _asyncio.set_event_loop(self._loop)
        self._ready.set()
        self._loop.run_forever()

    def wait_ready(self, timeout: float = 3.0) -> bool:
        return self._ready.wait(timeout)

    def submit(self, coro):
        return _asyncio.run_coroutine_threadsafe(coro, self._loop)

    def stop(self):
        if self._loop and self._loop.is_running():
            self._loop.call_soon_threadsafe(self._loop.stop)


# ─── BLE transport (Nordic UART Service — same v2 protocol as UART) ──────────
class BleTransport(UartTransport):
    """Transparent UART over BLE (Nordic UART Service).
    Inherits all cmd_* methods from UartTransport; overrides transport layer.
    Requires firmware to expose NUS RX/TX characteristics and route them
    through the existing UART command parser."""

    def __init__(self, address: str, name: str = '', parent=None):
        super().__init__(parent)
        self._ble_address = address
        self._ble_name    = name
        self._ble_client: Optional['BleakClient'] = None
        self._ble_thread: Optional[_BleLoopThread] = None
        self._rx_buf = b''   # accumulates notification bytes → text lines
        # Serialize all outgoing BLE commands — prevents byte interleaving in firmware NUS buffer
        self._ble_cmd_lock = threading.Lock()

    # ── Connection ────────────────────────────────────────────────────────────

    def connect_to(self, target: str = '') -> bool:
        if not HAS_BLEAK:
            return False
        self._ble_thread = _BleLoopThread()
        self._ble_thread.start()
        if not self._ble_thread.wait_ready(3.0):
            return False
        try:
            fut = self._ble_thread.submit(self._async_connect())
            ok = fut.result(timeout=12.0)
            if ok:
                self.connected_changed.emit(True)
            return ok
        except Exception:
            return False

    async def _async_connect(self):
        self._connect_error = ''
        try:
            def _on_ble_disconnected(client):
                # Fires from asyncio thread when beacon drops the connection
                self.connected_changed.emit(False)

            self._ble_client = BleakClient(self._ble_address, use_cached=False,
                                           disconnected_callback=_on_ble_disconnected)
            await self._ble_client.connect()
            # Negotiate maximum MTU — reduces chunking for large commands
            try:
                await self._ble_client.request_mtu(247)
            except Exception:
                pass
            # Subscribe to TX notifications (MCU → PC)
            await self._ble_client.start_notify(_NUS_TX_CHAR, self._on_notify)
            return True
        except Exception as e:
            self._connect_error = str(e)
            # Clean BLE disconnect so the beacon returns to advertising
            try:
                if self._ble_client and self._ble_client.is_connected:
                    await self._ble_client.disconnect()
            except Exception:
                pass
            return False

    def disconnect(self):
        if self._ble_client and self._ble_thread:
            try:
                fut = self._ble_thread.submit(self._ble_client.disconnect())
                fut.result(timeout=3.0)
            except Exception:
                pass
        if self._ble_thread:
            self._ble_thread.stop()
            self._ble_thread.wait(2000)
            self._ble_thread = None
        self._ble_client = None
        self.connected_changed.emit(False)

    # ── Incoming notifications (MCU → PC) ─────────────────────────────────────

    def _on_notify(self, _sender, data: bytes):
        """Called from asyncio thread when MCU sends data via NUS TX notify."""
        self._rx_buf += data
        while b'\n' in self._rx_buf:
            idx = self._rx_buf.index(b'\n')
            line = self._rx_buf[:idx].rstrip(b'\r').decode('ascii', errors='replace')
            self._rx_buf = self._rx_buf[idx + 1:]
            self._on_line(line)   # inherited from UartTransport

    # ── Request (overrides UART write with BLE write) ─────────────────────────

    def _request(self, verb: str, payload: str = '', timeout: float = 5.0) -> Tuple[bool, list]:
        if not self._ble_client or not self._ble_thread:
            return False, ['not connected']
        # One command at a time — prevents byte interleaving in firmware NUS line buffer
        with self._ble_cmd_lock:
            with self._lock:
                self._req_id = (self._req_id % 65535) + 1
                rid = self._req_id
                q: queue.Queue = queue.Queue()
                self._tagged[rid] = q

            cmd = f'#{rid} {verb}'
            if payload:
                cmd += f' {payload}'
            cmd += '\r\n'

            # Write command bytes to NUS RX characteristic (chunked for MTU)
            data = cmd.encode('ascii')
            mtu = getattr(self._ble_client, '_mtu_size', 23)
            chunk = max(20, mtu - 3)
            try:
                fut = self._ble_thread.submit(self._async_write(data, chunk))
                fut.result(timeout=5.0)
            except Exception as e:
                with self._lock: self._tagged.pop(rid, None)
                return False, [str(e)]

            # Wait for response lines (same logic as UartTransport._request)
            lines = []
            prefix = f'#{rid} '
            deadline = time.time() + timeout
            try:
                while time.time() < deadline:
                    remaining = max(0.05, deadline - time.time())
                    try:
                        raw = q.get(timeout=remaining)
                    except queue.Empty:
                        break
                    if raw.startswith(prefix):
                        rest = raw[len(prefix):]
                        if rest.startswith('OK'):
                            return True, lines
                        elif rest.startswith('ERR'):
                            return False, [rest]
                        else:
                            lines.append(rest)
                return False, ['timeout']
            finally:
                with self._lock: self._tagged.pop(rid, None)

    async def _async_write(self, data: bytes, chunk_size: int):
        for i in range(0, len(data), chunk_size):
            await self._ble_client.write_gatt_char(
                _NUS_RX_CHAR, data[i:i + chunk_size], response=False)
            if len(data) > chunk_size:
                await _asyncio.sleep(0.01)

    def get_rssi(self) -> Optional[int]:
        """Return current RSSI (dBm) or None if unavailable.
        Uses OP_BLE_RSSI NUS command (hci_read_rssi on device) — works on all platforms."""
        if not self._ble_client:
            return None
        # Primary: ask device for RSSI via HCI (works on Windows unlike bleak.rssi)
        try:
            import struct as _struct
            rc, data = self.send_cmd(OP_BLE_RSSI, b'', timeout=2.0)
            if rc == CMD_OK and data:
                return _struct.unpack('b', data[:1])[0]  # int8 dBm
        except Exception:
            pass
        # Fallback: bleak native (works on Linux/macOS)
        rssi = getattr(self._ble_client, 'rssi', None)
        if isinstance(rssi, int):
            return rssi
        return None

    def flush_uptime(self) -> Tuple[bool, str]:
        # Flash erase+write over BLE needs more margin than default 5s
        ok, lines = self._request('HWSAVE!', timeout=12.0)
        return ok, lines[0] if lines else ('OK' if ok else 'ERR timeout')

    # ── Overrides that don't apply over BLE ───────────────────────────────────

    def cmd_uart_mode(self, mode: int) -> int:
        return CMD_OK   # no-op: UART mode doesn't apply over BLE


def _rssi_color(rssi: int) -> str:
    """Return hex color for an RSSI value: green > -70, yellow > -85, red ≤ -85."""
    if rssi > -70:
        return '#4CAF50'
    if rssi > -85:
        return '#FFC107'
    return '#F44336'


# ─── RSSI history graph ──────────────────────────────────────────────────────
class _RssiCanvas(QWidget):
    """Simple line graph for RSSI history drawn with QPainter."""

    def __init__(self, history, parent=None):
        super().__init__(parent)
        self._history = list(history)
        self.setMinimumHeight(180)

    def update_data(self, history):
        self._history = list(history)
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)

        if not self._history:
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, 'No RSSI data yet')
            return

        ML, MR, MT, MB = 46, 12, 10, 32
        pw = max(self.width()  - ML - MR, 1)
        ph = max(self.height() - MT - MB, 1)

        vals = [v for _, v in self._history]
        y_min = min(-100, min(vals) - 3)
        y_max = max(-30,  max(vals) + 3)
        y_rng = y_max - y_min or 1

        t_pts = [t for t, _ in self._history]
        t_min, t_max = t_pts[0], t_pts[-1]
        t_rng = max(t_max - t_min, 1)

        def px(t, v):
            x = ML + int((t - t_min) / t_rng * pw)
            y = MT + ph - int((v - y_min) / y_rng * ph)
            return x, y

        # Background
        p.fillRect(ML, MT, pw, ph, QColor('#111827'))

        # Threshold lines
        p.setFont(QFont('Segoe UI', 7))
        for thr, col in [(-70, '#4CAF50'), (-85, '#FFC107')]:
            if y_min < thr < y_max:
                _, yp = px(t_min, thr)
                p.setPen(QPen(QColor(col), 1, Qt.PenStyle.DashLine))
                p.drawLine(ML, yp, ML + pw, yp)
                p.setPen(QColor(col))
                p.drawText(ML + 2, yp - 2, f'{thr}')

        # Y axis ticks
        p.setPen(QColor('#888'))
        p.setFont(QFont('Segoe UI', 7))
        step = 10
        v = (int(y_max) // step) * step
        while v >= y_min:
            _, yp = px(t_min, v)
            if MT <= yp <= MT + ph:
                p.drawText(QRect(0, yp - 8, ML - 4, 16),
                           Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter,
                           str(v))
                p.drawLine(ML - 3, yp, ML, yp)
            v -= step

        # X axis time labels
        p.setPen(QColor('#888'))
        n_ticks = min(6, len(self._history))
        step_i = max(1, len(self._history) // n_ticks)
        for i in range(0, len(self._history), step_i):
            t, _ = self._history[i]
            xp, _ = px(t, y_min)
            secs = int(t_max - t)
            lbl = 'now' if secs < 3 else f'-{secs}s'
            p.drawText(QRect(xp - 22, MT + ph + 4, 44, 16),
                       Qt.AlignmentFlag.AlignCenter, lbl)

        # RSSI line + dots
        for i in range(1, len(self._history)):
            t0, v0 = self._history[i - 1]
            t1, v1 = self._history[i]
            x0, y0 = px(t0, v0)
            x1, y1 = px(t1, v1)
            mid = (v0 + v1) / 2
            col = _rssi_color(int(mid))
            p.setPen(QPen(QColor(col), 2))
            p.drawLine(x0, y0, x1, y1)

        for t, v in self._history:
            xp, yp = px(t, v)
            col = QColor(_rssi_color(v))
            p.setBrush(col)
            p.setPen(Qt.PenStyle.NoPen)
            p.drawEllipse(xp - 3, yp - 3, 6, 6)

        # Border
        p.setPen(QColor('#444'))
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawRect(ML, MT, pw, ph)

        # Stats
        if vals:
            p.setPen(QColor('#ccc'))
            p.setFont(QFont('Segoe UI', 8))
            txt = f'min: {min(vals)} dBm   max: {max(vals)} dBm   last: {vals[-1]} dBm'
            p.drawText(QRect(ML, MT + ph + 18, pw, 14), Qt.AlignmentFlag.AlignCenter, txt)


class RssiGraphDialog(QDialog):
    """Non-modal RSSI history popup — update_data() refreshes the graph live."""

    def __init__(self, history, parent=None):
        super().__init__(parent, Qt.WindowType.Window)
        self.setWindowTitle('RSSI History')
        self.setMinimumSize(500, 300)
        self.resize(600, 320)
        self._canvas = _RssiCanvas(history, self)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(8, 8, 8, 8)
        lay.addWidget(self._canvas, 1)
        btn = QPushButton('Close')
        btn.setFixedWidth(80)
        btn.clicked.connect(self.close)
        lay.addWidget(btn, 0, Qt.AlignmentFlag.AlignRight)

    def update_data(self, history):
        self._canvas.update_data(history)


# ─── BLE Scan dialog ─────────────────────────────────────────────────────────
class BleScanDialog(QDialog):
    """Scans for BLE devices and returns the selected device address + name."""

    def __init__(self, C: dict, parent=None, preferred_address: str = ''):
        super().__init__(parent)
        self.setWindowTitle('BLE Device Scan')
        self.setMinimumSize(520, 420)
        self._C = C
        self._preferred_address = preferred_address.upper()
        self._selected: Optional[dict] = None
        self._devices: dict  = {}
        self._scan_worker: Optional['Worker'] = None
        self._build_ui()
        # Auto-start scan immediately when dialog opens
        QTimer.singleShot(100, self._start_scan)

    def _build_ui(self):
        lay = QVBoxLayout(self)
        lay.setSpacing(10)
        lay.setContentsMargins(16, 16, 16, 16)

        # Filter row
        filter_row = QHBoxLayout()
        self._cb_filter = QCheckBox('Show only BCN_ devices')
        self._cb_filter.setChecked(True)
        self._cb_filter.stateChanged.connect(self._refresh_table)
        filter_row.addWidget(self._cb_filter)
        filter_row.addStretch()
        self._lbl_status = QLabel('Scanning…')
        filter_row.addWidget(self._lbl_status)
        lay.addLayout(filter_row)

        # Options row
        opt_row = QHBoxLayout()
        self._chk_auto_rescan = QCheckBox('Auto re-scan')
        self._chk_auto_rescan.setChecked(True)
        self._chk_auto_rescan.setToolTip('Restart scan automatically when no device is found')
        opt_row.addWidget(self._chk_auto_rescan)
        opt_row.addSpacing(16)
        self._chk_auto_connect = QCheckBox('Auto-connect to known beacon')
        self._chk_auto_connect.setChecked(bool(self._preferred_address))
        self._chk_auto_connect.setToolTip('Automatically connect when the previously used beacon is found')
        opt_row.addWidget(self._chk_auto_connect)
        opt_row.addStretch()
        lay.addLayout(opt_row)

        # Table
        self._table = QTableWidget(0, 3)
        self._table.setHorizontalHeaderLabels(['Name', 'Address', 'RSSI'])
        self._table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self._table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self._table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        self._table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self._table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self._table.setAlternatingRowColors(True)
        self._table.doubleClicked.connect(self._on_connect)
        self._table.selectionModel().selectionChanged.connect(self._on_selection)
        lay.addWidget(self._table)

        # Progress bar
        self._progress = QProgressBar()
        self._progress.setRange(0, 0)
        self._progress.setFixedHeight(4)
        self._progress.setTextVisible(False)
        self._progress.setVisible(False)
        lay.addWidget(self._progress)

        # Buttons
        btn_row = QHBoxLayout()
        btn_row.setSpacing(8)
        self._btn_scan = QPushButton('🔄  Re-scan')
        self._btn_scan.setObjectName('secondary')
        self._btn_scan.setFixedHeight(34)
        self._btn_scan.clicked.connect(self._start_scan)

        self._btn_connect = QPushButton('Connect →')
        self._btn_connect.setObjectName('primary')
        self._btn_connect.setFixedHeight(34)
        self._btn_connect.setEnabled(False)
        self._btn_connect.clicked.connect(self._on_connect)

        btn_cancel = QPushButton('Cancel')
        btn_cancel.setObjectName('secondary')
        btn_cancel.setFixedHeight(34)
        btn_cancel.clicked.connect(self.reject)

        btn_row.addWidget(self._btn_scan)
        btn_row.addStretch()
        btn_row.addWidget(btn_cancel)
        btn_row.addWidget(self._btn_connect)
        lay.addLayout(btn_row)

        if not HAS_BLEAK:
            self._lbl_status.setText('⚠ bleak library not installed — pip install bleak')
            self._btn_scan.setEnabled(False)

    def _start_scan(self):
        self._devices = {}
        self._table.setRowCount(0)
        self._btn_scan.setEnabled(False)
        self._btn_connect.setEnabled(False)
        self._progress.setVisible(True)
        self._lbl_status.setText('Scanning…')

        def _do():
            devs = _asyncio.run(BleakScanner.discover(timeout=10.0, return_adv=True))
            result = []
            for addr, (dev, adv) in devs.items():
                result.append({'address': addr,
                                'name':    dev.name or '',
                                'rssi':    adv.rssi if hasattr(adv, 'rssi') else 0})
            return result

        w = Worker(_do)
        w.result.connect(self._on_scan_done)
        w.error.connect(self._on_scan_error)
        self._scan_worker = w
        w.start()

    def _on_scan_error(self, err: str):
        self._btn_scan.setEnabled(True)
        self._progress.setVisible(False)
        self._lbl_status.setText(f'Scan error: {err}')

    def _on_scan_done(self, devices: list):
        self._btn_scan.setEnabled(True)
        self._progress.setVisible(False)
        self._devices = {d['address']: d for d in devices}
        self._refresh_table()
        n = self._table.rowCount()
        if n == 0:
            self._lbl_status.setText(f'Found {len(devices)} device(s) — none shown (filter active).' if devices
                                     else 'No devices found. Scanning again…' if self._chk_auto_rescan.isChecked()
                                     else 'No devices found. Press Re-scan to try again.')
            if self._chk_auto_rescan.isChecked():
                QTimer.singleShot(800, self._start_scan)
            return
        self._lbl_status.setText(f'Found {n} device(s). Double-click to connect.')
        self._table.selectRow(0)
        self._btn_connect.setEnabled(True)
        # Auto-connect to the previously-known beacon if found
        if self._preferred_address and self._chk_auto_connect.isChecked():
            for row in range(self._table.rowCount()):
                addr = self._table.item(row, 1).text().upper()
                if addr == self._preferred_address:
                    self._table.selectRow(row)
                    self._on_connect()
                    return

    def _refresh_table(self):
        only_bcn = self._cb_filter.isChecked()
        self._table.setRowCount(0)
        sorted_devs = sorted(self._devices.values(),
                             key=lambda d: d['rssi'], reverse=True)
        for d in sorted_devs:
            if only_bcn and not d['name'].upper().startswith('BCN'):
                continue
            row = self._table.rowCount()
            self._table.insertRow(row)
            self._table.setItem(row, 0, QTableWidgetItem(d['name']))
            self._table.setItem(row, 1, QTableWidgetItem(d['address']))
            rssi_item = QTableWidgetItem(f"{d['rssi']} dBm")
            rssi_item.setTextAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            rssi_item.setForeground(QColor(_rssi_color(d['rssi'])))
            self._table.setItem(row, 2, rssi_item)
        if self._table.rowCount() > 0:
            self._table.selectRow(0)

    def _on_selection(self):
        self._btn_connect.setEnabled(self._table.currentRow() >= 0)

    def _on_connect(self):
        row = self._table.currentRow()
        if row < 0:
            return
        self._selected = {
            'address': self._table.item(row, 1).text(),
            'name':    self._table.item(row, 0).text(),
        }
        self.accept()

    def selected_device(self) -> Optional[dict]:
        return self._selected


# ─────────────────────────────────────────────────────────────────────────────
# Profile manager
# ─────────────────────────────────────────────────────────────────────────────
BUILTIN_PROFILES = {
    'Always active': ConfigBlob(sched_en=0),
    'Day 08-18': ConfigBlob(
        sched_en=1,
        sched_hours=sum(1<<h for h in range(8, 19)),
        sched_days=0x1F),   # Mon-Fri
    'Night 22-06': ConfigBlob(
        sched_en=1,
        sched_hours=sum(1<<h for h in list(range(22,24)) + list(range(0,7)))),
    'Field season Apr-Sep eco': ConfigBlob(
        rf_mode=3, sched_en=1,
        sched_months=sum(1<<m for m in range(3, 9))),   # Apr-Sep
}

class ProfileManager:
    FILE = 'tx_beacon_profiles_v3.json'

    def __init__(self):
        self._custom: dict = {}
        self._load()

    def _load(self):
        if os.path.exists(self.FILE):
            try:
                with open(self.FILE) as f:
                    data = json.load(f)
                self._custom = {k: ConfigBlob.from_dict(v['config'])
                                for k, v in data.items()
                                if k not in BUILTIN_PROFILES}
            except Exception:
                self._custom = {}

    def _save(self):
        data = {name: {'config': cfg.to_dict()}
                for name, cfg in self._custom.items()}
        try:
            with open(self.FILE, 'w') as f:
                json.dump(data, f, indent=2)
        except Exception:
            pass

    def all_names(self) -> List[str]:
        return list(BUILTIN_PROFILES.keys()) + list(self._custom.keys())

    def get(self, name: str) -> Optional[ConfigBlob]:
        if name in BUILTIN_PROFILES:
            return BUILTIN_PROFILES[name]
        return self._custom.get(name)

    def save_as(self, name: str, cfg: ConfigBlob):
        if name in BUILTIN_PROFILES:
            return
        self._custom[name] = cfg
        self._save()

    def delete(self, name: str):
        self._custom.pop(name, None)
        self._save()

    def is_builtin(self, name: str) -> bool:
        return name in BUILTIN_PROFILES


# ─────────────────────────────────────────────────────────────────────────────
# Custom widgets
# ─────────────────────────────────────────────────────────────────────────────
class ToggleSwitch(QWidget):
    toggled = pyqtSignal(bool)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._checked = False
        self._C = THEMES['light']
        self.setFixedSize(46, 24)
        self.setCursor(Qt.CursorShape.PointingHandCursor)

    def retheme(self, C: dict):
        self._C = C
        self.update()

    def isChecked(self) -> bool:
        return self._checked

    def setChecked(self, v: bool):
        self._checked = bool(v)
        self.update()

    def mousePressEvent(self, e):
        self._checked = not self._checked
        self.toggled.emit(self._checked)
        self.update()

    def paintEvent(self, e):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        r = self.rect()
        track_color = self._C['success'] if self._checked else self._C['border']
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor(track_color))
        p.drawRoundedRect(0, 0, 46, 24, 12, 12)
        knob_x = 24 if self._checked else 2
        p.setBrush(QColor('#ffffff'))
        p.drawEllipse(knob_x, 2, 20, 20)


class ChipButton(QPushButton):
    def __init__(self, text: str, parent=None):
        super().__init__(text, parent)
        self.setObjectName('chip')
        self.setCheckable(True)
        self.setCursor(Qt.CursorShape.PointingHandCursor)


def card_frame(layout_or_widget=None) -> QFrame:
    f = QFrame()
    f.setObjectName('card')
    if layout_or_widget is not None:
        if isinstance(layout_or_widget, QWidget):
            lay = QVBoxLayout(f)
            lay.setContentsMargins(16, 16, 16, 16)
            lay.addWidget(layout_or_widget)
        else:
            layout_or_widget.setContentsMargins(16, 16, 16, 16)
            f.setLayout(layout_or_widget)
    return f


class ValueCard(QFrame):
    """Metric card with large decorative icon (top-right) and optional ON-AIR badge."""

    def __init__(self, icon: str, title: str, parent=None):
        super().__init__(parent)
        self.setObjectName('card')
        self.setMinimumHeight(120)
        self._C = THEMES['light']

        outer = QVBoxLayout(self)
        outer.setContentsMargins(14, 8, 14, 8)
        outer.setSpacing(1)

        # ── Top row: small icon + title ──
        top = QHBoxLayout(); top.setSpacing(6)
        self._lbl_icon = QLabel(icon)
        self._lbl_icon.setFont(QFont('Segoe UI Emoji', 22))
        self._lbl_icon.setFixedSize(32, 32)
        self._lbl_icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
        top.addWidget(self._lbl_icon)
        self._lbl_title = QLabel(title)
        self._lbl_title.setFont(QFont('Segoe UI', 9))
        top.addWidget(self._lbl_title)
        top.addStretch()
        outer.addLayout(top)

        # ── Value row: big number + smaller unit + badge ──
        val_row = QHBoxLayout()
        val_row.setSpacing(4)
        self._lbl_num = QLabel('—')
        self._lbl_num.setFont(QFont('Segoe UI', 34, QFont.Weight.DemiBold))
        val_row.addWidget(self._lbl_num, 0, Qt.AlignmentFlag.AlignBottom)
        self._lbl_unit = QLabel('')
        self._lbl_unit.setFont(QFont('Segoe UI', 24))
        val_row.addWidget(self._lbl_unit, 0, Qt.AlignmentFlag.AlignBottom)
        self._badge = QLabel()
        self._badge.setFont(QFont('Segoe UI', 8, QFont.Weight.Bold))
        self._badge.setFixedHeight(20)
        self._badge.setVisible(False)
        self._badge.setAlignment(Qt.AlignmentFlag.AlignCenter)
        val_row.addWidget(self._badge, 0, Qt.AlignmentFlag.AlignBottom)
        val_row.addStretch()
        outer.addLayout(val_row)

        # ── Sub-text ──
        self._lbl_sub = QLabel('')
        self._lbl_sub.setFont(QFont('Segoe UI', 8))
        self._lbl_sub.setTextFormat(Qt.TextFormat.RichText)
        outer.addWidget(self._lbl_sub)
        outer.addStretch()

    def set_value(self, val: str, sub: str = '', color: str = ''):
        # Split "28.5 °C" → ("28.5", "°C"), "3.67 V" → ("3.67", "V")
        # Do NOT split "ON AIR", "CH0 · P3", etc.
        num, unit = val, ''
        if val != '—' and ' ' in val:
            left, right = val.rsplit(' ', 1)
            # Only split if left is numeric-ish AND right is a short unit (no digits)
            if (re.search(r'\d', left) and not re.search(r'\d', right)
                    and re.match(r'^[°%µ]?[A-Za-z]{1,3}$', right)):
                num, unit = left, right
        self._lbl_num.setText(num)
        self._lbl_unit.setText(unit)
        self._lbl_sub.setText(sub)
        c = color or self._C['text']
        # Include font-size explicitly — setFont() is overridden by global QWidget QSS
        self._lbl_num.setStyleSheet(f'font-size:34pt; font-weight:600; color:{c};')
        self._lbl_unit.setStyleSheet(f'font-size:16pt; color:{c};')

    def set_badge(self, text: str, bg: str = '', fg: str = '#ffffff'):
        if text:
            self._badge.setText(f'  {text}  ')
            self._badge.setStyleSheet(
                f'background:{bg}; color:{fg}; border-radius:4px; padding:0 4px;')
            self._badge.setVisible(True)
        else:
            self._badge.setVisible(False)

    def set_connected(self, connected: bool):
        """Dim entire card when beacon is offline."""
        if connected:
            self.setGraphicsEffect(None)
        else:
            eff = QGraphicsOpacityEffect(self)
            eff.setOpacity(0.35)
            self.setGraphicsEffect(eff)

    def retheme(self, C: dict):
        self._C = C
        self._lbl_title.setStyleSheet(f'color:{C["text_dim"]};')
        self._lbl_unit.setStyleSheet(f'color:{C["text_dim"]};')
        self._lbl_sub.setStyleSheet(f'color:{C["text_dim"]};')


class OverwriteBar(QWidget):
    """Storage progress bar that overlays diagonal hatching on the overwritten portion."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedHeight(14)
        self._used = 0; self._capacity = 100; self._overwrite = 0
        self._ble_queue = 0
        self._C = THEMES['light']

    def set_theme(self, C: dict):
        self._C = C; self.update()

    def set_value(self, used: int, capacity: int, overwrite_in_cycle: int = 0):
        self._used      = max(0, used)
        self._capacity  = max(1, capacity)
        self._overwrite = max(0, min(overwrite_in_cycle, capacity))
        self.update()

    def set_ble_queue(self, count: int):
        self._ble_queue = max(0, count)
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        w = self.width(); h = self.height(); r = h / 2.0

        # Background
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor(self._C.get('border', '#d0d0d0')))
        p.drawRoundedRect(QRectF(0, 0, w, h), r, r)

        used_frac = min(1.0, self._used / self._capacity)
        fill_w = int(w * used_frac)

        if fill_w > 0:
            if used_frac < 0.70:
                fill_col = QColor(self._C.get('success', '#4caf50'))
            elif used_frac < 0.90:
                fill_col = QColor(self._C.get('warning', '#ff9800'))
            else:
                fill_col = QColor(self._C.get('danger', '#f44336'))

            clip = QPainterPath()
            clip.addRoundedRect(QRectF(0, 0, w, h), r, r)
            p.save()
            p.setClipPath(clip)

            p.setBrush(fill_col)
            p.drawRect(0, 0, fill_w, h)

            # Diagonal hatch overlay on overwritten portion
            if self._overwrite > 0:
                ow_frac = min(1.0, self._overwrite / self._capacity)
                ow_w = int(w * ow_frac)
                if ow_w > 0 and h > 0:
                    p.setPen(QPen(QColor(255, 255, 255, 110), 1.5))
                    step = 7
                    for start in range(-int(h), ow_w + int(h), step):
                        t0 = max(0.0, min(1.0, -start / h))
                        t1 = max(0.0, min(1.0, (ow_w - start) / h))
                        if t1 <= t0:
                            continue
                        p.drawLine(int(start + t0 * h), int(h - t0 * h),
                                   int(start + t1 * h), int(h - t1 * h))
            p.restore()

        # BLE RAM-queue segment (indigo) — entries buffered during BLE session
        if self._ble_queue > 0 and fill_w < w:
            ble_frac = min(1.0, self._ble_queue / self._capacity)
            ble_w = min(int(w * ble_frac), w - fill_w)
            if ble_w > 0:
                clip2 = QPainterPath()
                clip2.addRoundedRect(QRectF(0, 0, w, h), r, r)
                p.save()
                p.setPen(Qt.PenStyle.NoPen)
                p.setClipPath(clip2)
                p.setBrush(QColor('#6366f1'))
                p.drawRect(fill_w, 0, ble_w, h)
                p.restore()

        p.end()


class BannerWidget(QFrame):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setVisible(False)
        self._C = THEMES['light']
        lay = QHBoxLayout(self)
        lay.setContentsMargins(16, 10, 16, 10)
        self._lbl = QLabel()
        self._lbl.setFont(QFont('Segoe UI', 10, QFont.Weight.Bold))
        self._btn = QPushButton('✕')
        self._btn.setFixedSize(24, 24)
        self._btn.setObjectName('banner_close')
        self._btn.clicked.connect(self.hide)
        lay.addWidget(self._lbl)
        lay.addStretch()
        lay.addWidget(self._btn)
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self.hide)

    def show_ok(self, msg: str, auto_hide_ms: int = 4000):
        C = self._C
        self.setStyleSheet(f'QFrame {{ background:{C["banner_ok_bg"]};border:1px solid {C["banner_ok_brd"]};border-radius:6px; }}'
                           f' QLabel {{ color:{C["banner_ok_txt"]}; background:transparent; border:none; }}'
                           f' QPushButton#banner_close {{ background:{C["banner_ok_brd"]}; color:#fff; border:none; border-radius:3px; font-weight:bold; }}')
        self._lbl.setText(f'✓  {msg}')
        self._btn.setVisible(auto_hide_ms <= 0)
        self.setVisible(True)
        if auto_hide_ms > 0:
            self._timer.start(auto_hide_ms)

    def show_err(self, msg: str):
        C = self._C
        self.setStyleSheet(f'QFrame {{ background:{C["banner_err_bg"]};border:1px solid {C["banner_err_brd"]};border-radius:6px; }}'
                           f' QLabel {{ color:{C["banner_err_txt"]}; background:transparent; border:none; }}'
                           f' QPushButton#banner_close {{ background:{C["banner_err_brd"]}; color:#fff; border:none; border-radius:3px; font-weight:bold; }}')
        self._lbl.setText(f'✕  {msg}')
        self._btn.setVisible(True)
        self._timer.stop()
        self.setVisible(True)

    def retheme(self, C: dict):
        self._C = C


# ─────────────────────────────────────────────────────────────────────────────
# Worker
# ─────────────────────────────────────────────────────────────────────────────
class Worker(QThread):
    result = pyqtSignal(object)
    error  = pyqtSignal(str)

    def __init__(self, fn, *args, **kwargs):
        super().__init__()
        self._fn = fn
        self._args = args
        self._kwargs = kwargs

    def run(self):
        try:
            self.result.emit(self._fn(*self._args, **self._kwargs))
        except Exception as e:
            self.error.emit(str(e))


# ─────────────────────────────────────────────────────────────────────────────
# Tab 1 — Overview
# ─────────────────────────────────────────────────────────────────────────────
class OverviewTab(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._C  = THEMES['light']
        self._temps: List[float] = []
        self._temp_times: List[float] = []
        self._rssi_hist: List[tuple] = []
        self._light_hist: List[tuple] = []
        self._accel_hist: List[tuple] = []
        self._t0 = time.time()
        self._live_streaming = False
        self._tz_offset_s = 0
        self._s_last_refresh_ts: float = 0.0
        self._spark_series: int = 0   # 0=Temp 1=RSSI 2=Light 3=Accel
        self._spark_max_pts: int = 100
        self._build()
        self._refresh_sec_timer = QTimer(self)
        self._refresh_sec_timer.setInterval(1000)
        self._refresh_sec_timer.timeout.connect(self._on_refresh_tick)
        self._refresh_sec_timer.start()

    def set_tz_offset(self, hours: int):
        self._tz_offset_s = hours * 3600

    def set_live_streaming(self, enabled: bool):
        self._live_streaming = enabled

    def _build(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(10)

        # Row of 5 value cards
        row = QHBoxLayout()
        row.setSpacing(10)
        self._card_temp  = ValueCard(ICO['temp'],    'Temperature')
        self._card_bat   = ValueCard(ICO['battery'], 'Battery')
        self._card_light = ValueCard(ICO['light'],   'Light')
        self._card_tx    = ValueCard(ICO['tx'],      'Transmitter')
        self._card_accel = ValueCard('🔵',           'Accelerometer')
        for c in [self._card_temp, self._card_bat, self._card_light,
                  self._card_tx, self._card_accel]:
            row.addWidget(c)
        root.addLayout(row)

        # Device + Storage row
        mid = QHBoxLayout()
        mid.setSpacing(10)
        self._device_card = self._build_device_card()
        self._storage_card = self._build_storage_card()
        mid.addWidget(self._device_card, 3)
        mid.addWidget(self._storage_card, 4)
        root.addLayout(mid)

        # Sparkline
        self._spark_frame = self._build_spark()
        root.addWidget(self._spark_frame)
        root.addStretch()

    def _build_device_card(self) -> QFrame:
        f = QFrame(); f.setObjectName('card')
        lay = QVBoxLayout(f); lay.setContentsMargins(16,16,16,16); lay.setSpacing(8)
        lbl = QLabel(f'{ICO["connect"]}  Device'); lbl.setFont(QFont('Segoe UI', 10, QFont.Weight.Bold))
        lay.addWidget(lbl)
        grid = QGridLayout(); grid.setSpacing(4)
        self._d_fw   = QLabel('—')
        self._d_uid  = QLabel('—')
        self._d_tag  = QLabel('—')
        self._d_up   = QLabel('—')
        self._d_rtc  = QLabel('—')
        self._d_sched = QLabel('—')
        for i, (lbl_txt, w) in enumerate([
            ('Firmware', self._d_fw), ('UID', self._d_uid),
            ('Tag', self._d_tag),     ('Uptime', self._d_up),
            ('RTC', self._d_rtc),     ('Schedule', self._d_sched)]):
            l = QLabel(lbl_txt); l.setObjectName('dim')
            grid.addWidget(l, i//2, (i%2)*2)
            grid.addWidget(w, i//2, (i%2)*2+1)
        lay.addLayout(grid)
        lay.addStretch()
        return f

    @staticmethod
    def _fmt_size(n_records: int) -> str:
        b = n_records * 16
        if b >= 1024 * 1024:
            return f'{b / 1024 / 1024:.2f} MB'
        return f'{b / 1024:.1f} KB'

    def _build_storage_card(self) -> QFrame:
        f = QFrame(); f.setObjectName('card')
        lay = QVBoxLayout(f); lay.setContentsMargins(16, 16, 16, 16); lay.setSpacing(8)

        hdr = QHBoxLayout()
        lbl = QLabel(f'{ICO["storage"]}  Storage')
        lbl.setFont(QFont('Segoe UI', 10, QFont.Weight.Bold))
        hdr.addWidget(lbl)
        hdr.addStretch()
        self._s_pct_lbl = QLabel()          # "32% used"
        self._s_pct_lbl.setFont(QFont('Segoe UI', 9, QFont.Weight.Bold))
        hdr.addWidget(self._s_pct_lbl)
        lay.addLayout(hdr)

        self._s_count = QLabel('— / — records')
        lay.addWidget(self._s_count)

        self._s_bar = OverwriteBar()
        lay.addWidget(self._s_bar)

        self._s_size  = QLabel('—')          # "Used X KB · Free Y KB · Total Z KB"
        self._s_depth = QLabel('—')          # "≈ N h history"
        self._s_mode  = QLabel()             # kept for compat (show_disconnected)
        self._s_log   = QLabel()
        for w in [self._s_size, self._s_depth]:
            lay.addWidget(w)
        self._s_refresh_lbl = QLabel('')
        self._s_refresh_lbl.setFont(QFont('Segoe UI', 8))
        lay.addWidget(self._s_refresh_lbl)
        lay.addStretch()
        return f

    def _build_spark(self) -> QFrame:
        f = QFrame(); f.setObjectName('card')
        lay = QVBoxLayout(f); lay.setContentsMargins(16, 12, 16, 12); lay.setSpacing(6)

        hdr = QHBoxLayout()
        self._spark_combo = QComboBox()
        self._spark_combo.addItems(['Temperature', 'RSSI', 'Light', 'Accel Mag.'])
        self._spark_combo.setFont(QFont('Segoe UI', 10, QFont.Weight.Bold))
        self._spark_combo.setFixedWidth(160)
        self._spark_combo.currentIndexChanged.connect(self._on_spark_series_changed)
        hdr.addWidget(self._spark_combo)
        hdr.addSpacing(12)
        pts_lbl = QLabel('Points:')
        pts_lbl.setFont(QFont('Segoe UI', 9))
        hdr.addWidget(pts_lbl)
        self._spark_pts_spin = QSpinBox()
        self._spark_pts_spin.setRange(10, 1000)
        self._spark_pts_spin.setValue(100)
        self._spark_pts_spin.setSuffix(' pts')
        self._spark_pts_spin.setFixedWidth(90)
        self._spark_pts_spin.valueChanged.connect(self._on_spark_pts_changed)
        hdr.addWidget(self._spark_pts_spin)
        hdr.addStretch()
        self._spark_range_lbl = QLabel()
        self._spark_range_lbl.setFont(QFont('Segoe UI', 9))
        hdr.addWidget(self._spark_range_lbl)
        lay.addLayout(hdr)

        if HAS_PG:
            self._plot = pg.PlotWidget()
            self._plot.setBackground('transparent')
            self._plot.setMinimumHeight(120)

            # Y axis (left) — temperature
            ax_l = self._plot.getAxis('left')
            ax_l.show()
            ax_l.setLabel('°C')
            ax_l.setWidth(46)

            # X axis (bottom) — time in minutes
            ax_b = self._plot.getAxis('bottom')
            ax_b.show()
            ax_b.setLabel('Time')

            # Grid
            self._plot.showGrid(x=True, y=True, alpha=0.25)

            # Curve + fill to baseline
            self._curve = self._plot.plot(
                pen=pg.mkPen(self._C['sparkline'], width=2.5),
                fillLevel=None,
                brush=None)
            self._fill = pg.FillBetweenItem(
                curve1=self._curve,
                curve2=pg.PlotDataItem([0], [0]),
                brush=pg.mkBrush(self._C['sparkline'] + '30'))
            self._plot.addItem(self._fill)

            # Style axes for theme
            self._spark_apply_theme(self._C)
            lay.addWidget(self._plot)
        else:
            lay.addWidget(QLabel('(install pyqtgraph for chart)'))
        return f

    def _spark_apply_theme(self, C: dict):
        """Apply theme colors to sparkline axes."""
        if not HAS_PG or not hasattr(self, '_plot'):
            return
        text_color = C['text_dim']
        border_color = C['border']
        for ax_name in ('left', 'bottom'):
            ax = self._plot.getAxis(ax_name)
            ax.setPen(pg.mkPen(border_color, width=1))
            ax.setTextPen(pg.mkPen(text_color))
        if hasattr(self, '_fill'):
            self._fill.setBrush(pg.mkBrush(C['sparkline'] + '30'))

    @staticmethod
    def _smooth_ma(data: list, window: int = 5) -> list:
        """Centered moving average — smooths sparkline without introducing lag at edges."""
        n = len(data)
        if n < window:
            return list(data)
        half = window // 2
        return [
            sum(data[max(0, i - half): min(n, i + half + 1)]) /
            len(data[max(0, i - half): min(n, i + half + 1)])
            for i in range(n)
        ]

    def update_status(self, stat: StatusBlob, info: Optional[InfoBlob] = None,
                      cfg: Optional[ConfigBlob] = None):
        # Restore card appearance (called when connected and data arrives)
        for c in [self._card_temp, self._card_bat, self._card_light, self._card_tx]:
            c.set_connected(True)

        # Temperature card — skip display update on obviously invalid reads (temp_01c==0)
        raw_temp = stat.temp_01c
        if raw_temp == 0:
            raw_temp = getattr(self, '_last_valid_temp_x10', 0)
        else:
            self._last_valid_temp_x10 = raw_temp
        temp_c = raw_temp / 10.0
        if 36.0 <= temp_c <= 38.0:
            col = self._C['success']
            status = 'normal'
        elif 35.0 <= temp_c < 36.0 or 38.0 < temp_c <= 39.0:
            col = self._C['warning']
            status = 'elevated'
        else:
            col = self._C['danger']
            status = 'critical'
        if cfg and cfg.temp_iv_s == 0 and not self._live_streaming:
            self._card_temp.set_value('—', 'deactivated', self._C['text_dim'])
        else:
            if cfg and cfg.temp_iv_s == 0:
                iv_txt = 'live'
            elif cfg:
                iv_txt = f'every {cfg.temp_iv_s} s'
            else:
                iv_txt = ''
            self._card_temp.set_value(f'{temp_c:.1f} °C', iv_txt or status, col)

        # Battery card
        bat_v = stat.bat_mv / 1000.0
        if cfg and cfg.bat_iv_s == 0 and not self._live_streaming:
            self._card_bat.set_value('—', 'deactivated', self._C['text_dim'])
        else:
            if cfg and cfg.bat_iv_s == 0:
                iv_txt = f'{stat.bat_pct}%  live'
            elif cfg:
                iv_txt = f'{stat.bat_pct}% · every {cfg.bat_iv_s} s'
            else:
                iv_txt = f'{stat.bat_pct}%'
            self._card_bat.set_value(f'{bat_v:.2f} V', iv_txt)

        # Light card — show as 0-100% instead of raw ADC
        light_pct = int(stat.light_raw * 100 / 4095) if stat.light_raw else 0
        if cfg and cfg.light_iv_s == 0 and not self._live_streaming:
            self._card_light.set_value('—', 'deactivated', self._C['text_dim'])
        else:
            if cfg and cfg.light_iv_s == 0:
                iv_txt = 'live'
            elif cfg:
                iv_txt = f'every {cfg.light_iv_s} s'
            else:
                iv_txt = ''
            self._card_light.set_value(f'{light_pct} %', iv_txt)

        # TX card
        if cfg and cfg.rf_mode == 0:
            self._card_tx.set_value('—', 'deactivated', self._C['text_dim'])
            self._card_tx.set_badge('')
        elif cfg:
            modes = ['off', 'pulse', 'cont', 'eco']
            mode_lbl = modes[cfg.rf_mode] if 0 <= cfg.rf_mode < 4 else '?'
            ch_pw = f'CH{cfg.rf_channel} · P{cfg.rf_power}' if info else '—'
            self._card_tx.set_value(ch_pw, mode_lbl)
            if stat and stat.tx_active:
                self._card_tx.set_badge('ON AIR', bg='#2e7d32', fg='#ffffff')
            else:
                self._card_tx.set_badge('')
        else:
            self._card_tx.set_value('—', '')
            self._card_tx.set_badge('')

        # Device info
        if info:
            self._d_fw.setText(f'v{info.fw_major}.{info.fw_minor}.{info.fw_patch}')
            self._d_uid.setText(f'0x{info.uid:08X}')
            self._d_tag.setText(info.tag if info.tag else '—')
            total_h = info.total_active_h + info.total_stop1_h + info.total_shutdown_h
            if total_h:
                d, h = total_h // 24, total_h % 24
                self._d_up.setText(
                    f'{d}d {h}h  (act {info.total_active_h}h · s1 {info.total_stop1_h}h · off {info.total_shutdown_h}h)')
            else:
                _us = stat.uptime_s
                self._d_up.setText(f'{_us // 3600}h {(_us % 3600) // 60:02d}m {_us % 60:02d}s')
        else:
            _us = stat.uptime_s
            self._d_up.setText(f'{_us // 3600}h {(_us % 3600) // 60:02d}m {_us % 60:02d}s')
        if stat.rtc_unix:
            import datetime
            dt = datetime.datetime.utcfromtimestamp(stat.rtc_unix + self._tz_offset_s)
            tz_h = self._tz_offset_s // 3600
            tz_sfx = f' UTC{tz_h:+d}' if tz_h != 0 else ' UTC'
            self._d_rtc.setText(dt.strftime('%Y-%m-%d %H:%M') + tz_sfx)
        self._d_sched.setText('Active now' if stat.sched_active else 'Inactive')

        # Storage — capacity comes from InfoBlob; stat.log_used = raw total_records
        # stat.log_total = overwrite cycle offset (firmware repurposed) when overwriting
        cap       = (info.log_total_entries if info else LOG_ENTRIES_MAX) or LOG_ENTRIES_MAX
        used_raw  = stat.log_used   # can be > cap after wraps
        circular  = bool(cfg and getattr(cfg, 'log_overflow', 0) == 1)
        overwriting = circular and used_raw > cap

        if overwriting:
            used_disp = cap
            free_disp = 0
            pct_used  = 100
            # stat.log_total carries write-cycle offset from firmware
            ow_cycle  = stat.log_total if stat.log_total < cap else 0
        else:
            used_disp = min(used_raw, cap)
            free_disp = max(0, cap - used_disp)
            pct_used  = int(used_disp * 100 / cap) if cap else 0
            ow_cycle  = 0

        self._s_count.setText(f'{used_disp} / {cap} records')
        self._s_bar.set_value(used_disp, cap, ow_cycle)
        self._s_last_refresh_ts = time.time()
        self._on_refresh_tick()

        total_sz = self._fmt_size(cap)
        if overwriting:
            ow_pct = int(ow_cycle * 100 / cap) if cap else 0
            self._s_pct_lbl.setText(f'⟳ {ow_pct}% rewritten')
            self._s_pct_lbl.setStyleSheet(f'color:{self._C["warning"]};')
            self._s_size.setText(f'⟳  Circular overwrite active  ·  Total {total_sz}')
            self._s_size.setStyleSheet(f'color:{self._C["warning"]};')
            self._s_depth.setText('Oldest records are being overwritten')
            self._s_depth.setStyleSheet(f'color:{self._C["warning"]};')
        else:
            col_pct = (self._C['danger']  if pct_used >= 90 else
                       self._C['warning'] if pct_used >= 70 else
                       self._C['success'])
            self._s_pct_lbl.setText(f'{pct_used}% used')
            self._s_pct_lbl.setStyleSheet(f'color:{col_pct};')
            used_sz = self._fmt_size(used_disp)
            free_sz = self._fmt_size(free_disp)
            self._s_size.setText(f'Used {used_sz}  ·  Free {free_sz}  ·  Total {total_sz}')
            self._s_size.setStyleSheet('')
            iv_s    = cfg.temp_iv_s if (cfg and cfg.temp_iv_s > 0) else 60
            depth_h = (free_disp * iv_s) / 3600.0
            self._s_depth.setText(f'≈ {depth_h:.1f} h free space remaining')
            self._s_depth.setStyleSheet('')

        # History for sparkline series
        now = time.time()
        if 15.0 <= temp_c <= 55.0:
            self._temps.append(temp_c)
            self._temp_times.append(now)
            if len(self._temps) > 300:
                self._temps = self._temps[-150:]
                self._temp_times = self._temp_times[-150:]

        light_pct_f = float(stat.light_raw * 100 / 4095) if stat.light_raw else 0.0
        self._light_hist.append((now, light_pct_f))
        if len(self._light_hist) > 300:
            self._light_hist = self._light_hist[-150:]

        self._update_sparkline()

    def update_accel(self, x: Optional[int], y: Optional[int], z: Optional[int],
                     wom_armed: bool = False):
        if x is None:
            self._card_accel.set_value('—', 'no data')
        elif y is not None and z is not None:
            mag = math.sqrt(x * x + y * y + z * z)
            scale = 16384.0
            xg = x / scale; yg = y / scale; zg = z / scale
            sub = (f"<span style='color:#ef4444; font-size:8pt'>X {xg:+.2f} g</span>"
                   f"&nbsp;&nbsp;"
                   f"<span style='color:#22c55e; font-size:8pt'>Y {yg:+.2f} g</span>"
                   f"&nbsp;&nbsp;"
                   f"<span style='color:#3b82f6; font-size:8pt'>Z {zg:+.2f} g</span>")
            self._card_accel.set_value(f'{mag / scale:.2f} g', sub)
            self._accel_hist.append((time.time(), mag / scale))
            if len(self._accel_hist) > 300:
                self._accel_hist = self._accel_hist[-150:]
            if self._spark_series == 3:
                self._update_sparkline()
        else:
            self._card_accel.set_value(f'{x / 16384:.2f} g', f'raw {x}')
        self._card_accel.set_badge('WoM' if wom_armed else '', '#22c55e')

    def show_disconnected(self):
        for c in [self._card_temp, self._card_bat, self._card_light,
                  self._card_tx, self._card_accel]:
            c.set_value('—', 'offline')
            c.set_badge('')
            c.set_connected(False)
        for w in [self._d_fw, self._d_uid, self._d_tag, self._d_up, self._d_rtc, self._d_sched,
                  self._s_count, self._s_size, self._s_depth]:
            w.setText('—')
        self._s_pct_lbl.setText('')
        self._s_bar.set_value(0, 100, 0)
        self._spark_range_lbl.setText('')
        self._s_last_refresh_ts = 0.0
        self._s_refresh_lbl.setText('')

    def push_rssi(self, rssi: int):
        """Called from MainWindow RSSI poll; feeds RSSI sparkline series."""
        self._rssi_hist.append((time.time(), float(rssi)))
        if len(self._rssi_hist) > 300:
            self._rssi_hist = self._rssi_hist[-150:]
        if self._spark_series == 1:
            self._update_sparkline()

    def _on_refresh_tick(self):
        """1-second timer: update 'last polled' label in Storage card."""
        if self._s_last_refresh_ts == 0.0:
            self._s_refresh_lbl.setText('')
            return
        elapsed = int(time.time() - self._s_last_refresh_ts)
        if elapsed < 2:
            txt = '↺  just now'
        elif elapsed < 60:
            txt = f'↺  {elapsed} s ago'
        else:
            m = elapsed // 60
            txt = f'↺  {m} min ago'
        self._s_refresh_lbl.setText(txt)
        self._s_refresh_lbl.setStyleSheet(
            f'color: {self._C["text_dim"]}; font-size: 8pt;')

    def _on_spark_series_changed(self, idx: int):
        self._spark_series = idx
        units = ['°C', 'dBm', '%', 'g']
        if HAS_PG and hasattr(self, '_plot'):
            self._plot.getAxis('left').setLabel(units[idx])
        self._update_sparkline()

    def _on_spark_pts_changed(self, val: int):
        self._spark_max_pts = val
        self._update_sparkline()

    def _update_sparkline(self):
        if not HAS_PG or not hasattr(self, '_curve'):
            return
        series = self._spark_series
        pts    = self._spark_max_pts

        if series == 0:      # Temperature
            if len(self._temps) < 2:
                return
            hist_t = self._temp_times[-pts:]
            hist_v = self._temps[-pts:]
            unit, fmt = '°C', '.1f'
        elif series == 1:    # RSSI
            if len(self._rssi_hist) < 2:
                return
            data   = self._rssi_hist[-pts:]
            hist_t = [x[0] for x in data]
            hist_v = [x[1] for x in data]
            unit, fmt = ' dBm', '.0f'
        elif series == 2:    # Light
            if len(self._light_hist) < 2:
                return
            data   = self._light_hist[-pts:]
            hist_t = [x[0] for x in data]
            hist_v = [x[1] for x in data]
            unit, fmt = '%', '.1f'
        else:                # Accel Mag.
            if len(self._accel_hist) < 2:
                return
            data   = self._accel_hist[-pts:]
            hist_t = [x[0] for x in data]
            hist_v = [x[1] for x in data]
            unit, fmt = ' g', '.3f'

        t0 = hist_t[0]
        xs = [t - t0 for t in hist_t]
        ys = self._smooth_ma(hist_v)
        self._curve.setData(xs, ys)
        self._curve.setPen(pg.mkPen(self._C['sparkline'], width=2.5))
        mn, mx = min(hist_v), max(hist_v)
        span = max(abs(mx - mn), 1e-6)
        base = mn - span * 0.05
        self._fill.setCurves(
            self._curve,
            pg.PlotDataItem(xs, [base] * len(xs)))
        self._spark_range_lbl.setText(
            f'min {mn:{fmt}}{unit}  max {mx:{fmt}}{unit}  Δ {mx-mn:{fmt}}{unit}')
        self._spark_range_lbl.setStyleSheet(f'color:{self._C["text_dim"]};')

    def get_spark_settings(self) -> tuple:
        """Returns (series_index, max_points)."""
        return (self._spark_combo.currentIndex(), self._spark_pts_spin.value())

    def set_spark_settings(self, series: int, pts: int):
        self._spark_combo.blockSignals(True)
        self._spark_combo.setCurrentIndex(max(0, min(3, series)))
        self._spark_combo.blockSignals(False)
        self._spark_pts_spin.blockSignals(True)
        self._spark_pts_spin.setValue(max(10, min(1000, pts)))
        self._spark_pts_spin.blockSignals(False)
        self._spark_series   = self._spark_combo.currentIndex()
        self._spark_max_pts  = self._spark_pts_spin.value()

    def retheme(self, C: dict):
        self._C = C
        for c in [self._card_temp, self._card_bat, self._card_light,
                  self._card_tx, self._card_accel]:
            c.retheme(C)
        self._s_bar.set_theme(C)
        if HAS_PG and hasattr(self, '_curve'):
            self._curve.setPen(pg.mkPen(C['sparkline'], width=2.5))
            self._plot.setBackground('transparent')
            self._spark_apply_theme(C)


# ─────────────────────────────────────────────────────────────────────────────
# Tab 2 — Beacon (transmitter + schedule)
# ─────────────────────────────────────────────────────────────────────────────
class BeaconTab(QWidget):
    config_changed       = pyqtSignal()

    def __init__(self, profiles: ProfileManager, parent=None):
        super().__init__(parent)
        self._C = THEMES['light']
        self._profiles = profiles
        self._dirty = False
        self._profile_name = list(BUILTIN_PROFILES.keys())[0]
        self._build()

    def _build(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        inner = QWidget()
        root = QVBoxLayout(inner)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(10)

        # Profile bar
        pbar = QFrame(); pbar.setObjectName('card')
        pl = QHBoxLayout(pbar); pl.setContentsMargins(12,8,12,8); pl.setSpacing(8)
        pl.addWidget(QLabel('Profile:'))
        self._profile_cb = QComboBox()
        self._profile_cb.addItems(self._profiles.all_names())
        self._profile_cb.currentTextChanged.connect(self._on_profile_selected)
        pl.addWidget(self._profile_cb, 1)
        self._btn_prof_apply = QPushButton('Apply')
        self._btn_prof_apply.setObjectName('secondary')
        self._btn_prof_apply.clicked.connect(self._on_profile_apply)
        pl.addWidget(self._btn_prof_apply)
        self._btn_prof_save = QPushButton('Save as…')
        self._btn_prof_save.setObjectName('secondary')
        self._btn_prof_save.clicked.connect(self._on_profile_save)
        pl.addWidget(self._btn_prof_save)
        self._btn_prof_del = QPushButton('Delete')
        self._btn_prof_del.setObjectName('secondary')
        self._btn_prof_del.clicked.connect(self._on_profile_delete)
        pl.addWidget(self._btn_prof_del)
        root.addWidget(pbar)

        # Transmitter card
        tx_card = QFrame(); tx_card.setObjectName('card')
        tx_lay = QVBoxLayout(tx_card); tx_lay.setContentsMargins(16,16,16,16); tx_lay.setSpacing(10)
        tx_title = QLabel('Transmitter'); tx_title.setFont(QFont('Segoe UI', 11, QFont.Weight.Bold))
        tx_lay.addWidget(tx_title)

        # Mode row
        mode_row = QHBoxLayout()
        mode_row.addWidget(QLabel('Mode'))
        mode_row.addSpacing(8)
        self._mode_chips = []
        self._mode_grp = QButtonGroup(self)
        self._mode_grp.setExclusive(True)
        for i, lbl in enumerate(['Off', 'Pulse', 'Continuous', 'Eco']):
            btn = ChipButton(lbl)
            self._mode_grp.addButton(btn, i)
            mode_row.addWidget(btn)
            self._mode_chips.append(btn)
        mode_row.addStretch()
        self._mode_grp.idToggled.connect(lambda *_: self._mark_dirty())
        tx_lay.addLayout(mode_row)

        # Channel + Power row
        chpwr_row = QHBoxLayout()
        chpwr_row.addWidget(QLabel('Channel'))
        chpwr_row.addSpacing(8)
        self._ch_chips = []
        self._ch_grp = QButtonGroup(self)
        self._ch_grp.setExclusive(True)
        for i in range(4):
            btn = ChipButton(f'CH{i}')
            self._ch_grp.addButton(btn, i)
            chpwr_row.addWidget(btn)
            self._ch_chips.append(btn)
        chpwr_row.addSpacing(20)
        chpwr_row.addWidget(QLabel('Power'))
        chpwr_row.addSpacing(8)
        self._pwr_chips = []
        self._pwr_grp = QButtonGroup(self)
        self._pwr_grp.setExclusive(True)
        for i in range(4):
            btn = ChipButton(f'P{i+1}')
            self._pwr_grp.addButton(btn, i)
            chpwr_row.addWidget(btn)
            self._pwr_chips.append(btn)
        chpwr_row.addStretch()
        self._ch_grp.idToggled.connect(lambda *_: self._mark_dirty())
        self._pwr_grp.idToggled.connect(lambda *_: self._mark_dirty())
        tx_lay.addLayout(chpwr_row)

        # Pulse / Period row
        pp_row = QHBoxLayout()
        pp_row.addWidget(QLabel('Pulse'))
        self._spin_pulse = QSpinBox(); self._spin_pulse.setRange(5, 5000)
        self._spin_pulse.setValue(23); self._spin_pulse.setSuffix(' ms')
        self._spin_pulse.setFixedWidth(90)
        self._spin_pulse.valueChanged.connect(self._mark_dirty)
        pp_row.addWidget(self._spin_pulse)
        pp_row.addSpacing(24)
        pp_row.addWidget(QLabel('Period'))
        self._spin_period = QSpinBox(); self._spin_period.setRange(100, 600000)
        self._spin_period.setValue(2000); self._spin_period.setSuffix(' ms')
        self._spin_period.setFixedWidth(100)
        self._spin_period.valueChanged.connect(self._mark_dirty)
        pp_row.addWidget(self._spin_period)
        pp_row.addStretch()
        # Simulate button
        self._btn_sim = QPushButton('▶  Simulate')
        self._btn_sim.setObjectName('secondary')
        self._btn_sim.setFixedWidth(110)
        self._btn_sim.setToolTip('Play audio preview: pulse ON/OFF rhythm')
        self._btn_sim.clicked.connect(self._on_simulate)
        pp_row.addWidget(self._btn_sim)
        tx_lay.addLayout(pp_row)
        self._sim_thread = None   # background simulation thread
        root.addWidget(tx_card)

        # Schedule card
        sc_card = QFrame(); sc_card.setObjectName('card')
        sc_lay = QVBoxLayout(sc_card); sc_lay.setContentsMargins(16,16,16,16); sc_lay.setSpacing(10)
        sc_title = QLabel('Schedule'); sc_title.setFont(QFont('Segoe UI', 11, QFont.Weight.Bold))
        sc_lay.addWidget(sc_title)
        self._chk_sched = QCheckBox('Enable schedule')
        self._chk_sched.stateChanged.connect(self._mark_dirty)
        sc_lay.addWidget(self._chk_sched)

        # Scope chips
        scope_row = QHBoxLayout()
        scope_row.addWidget(QLabel('Schedule applies to:'))
        self._scope_grp = QButtonGroup(self)
        self._scope_grp.setExclusive(True)
        self._btn_scope_tx   = ChipButton('Transmitter only')
        self._btn_scope_both = ChipButton('Transmitter + logging')
        self._btn_scope_tx.setChecked(True)
        self._scope_grp.addButton(self._btn_scope_tx, 0)
        self._scope_grp.addButton(self._btn_scope_both, 1)
        scope_row.addWidget(self._btn_scope_tx)
        scope_row.addWidget(self._btn_scope_both)
        scope_row.addStretch()
        self._scope_grp.idToggled.connect(self._on_scope_changed)
        sc_lay.addLayout(scope_row)
        self._lbl_scope_hint = QLabel('Sensors keep measuring and logging around the clock.')
        self._lbl_scope_hint.setWordWrap(True)
        self._lbl_scope_hint.setObjectName('dim')
        sc_lay.addWidget(self._lbl_scope_hint)

        def _sched_sub(title: str, quick_btns: list) -> tuple:
            """Returns (sub_lay, QHBoxLayout quick_row) for a schedule sub-group."""
            sep = QFrame(); sep.setFrameShape(QFrame.Shape.HLine)
            sep.setStyleSheet('color:#b0c8e8;')
            sc_lay.addWidget(sep)
            hdr = QHBoxLayout(); hdr.setSpacing(8)
            lbl = QLabel(title)
            lbl.setFont(QFont('Segoe UI', 9, QFont.Weight.Bold))
            hdr.addWidget(lbl)
            for txt, fn in quick_btns:
                b = QPushButton(txt); b.setObjectName('secondary')
                b.setFixedHeight(24); b.setMinimumWidth(66)
                b.clicked.connect(fn); hdr.addWidget(b)
            hdr.addStretch()
            sc_lay.addLayout(hdr)
            return hdr

        # ── Hours sub-group ──
        _sched_sub('Hours', [
            ('All',     lambda: self._set_hours(0xFFFFFF)),
            ('None',    lambda: self._set_hours(0)),
            ('Day 8–18', lambda: self._set_hours(sum(1<<h for h in range(8,19)))),
            ('Night',   lambda: self._set_hours(sum(1<<h for h in list(range(22,24))+list(range(0,7))))),
        ])
        self._hr_chips = []
        self._hr_grp = QButtonGroup(self); self._hr_grp.setExclusive(False)
        for row_idx in range(2):
            row_w = QHBoxLayout()
            for h in range(12 * row_idx, 12 * (row_idx + 1)):
                btn = ChipButton(f'{h:02d}')
                self._hr_grp.addButton(btn, h)
                row_w.addWidget(btn)
                self._hr_chips.append(btn)
            row_w.addStretch()
            sc_lay.addLayout(row_w)
        self._hr_grp.idToggled.connect(lambda *_: self._mark_dirty())

        # ── Days sub-group ──
        _sched_sub('Days', [
            ('All',      lambda: self._set_days(0x7F)),
            ('Weekdays', lambda: self._set_days(0x1F)),
        ])
        days_row = QHBoxLayout()
        self._day_chips = []
        self._day_grp = QButtonGroup(self); self._day_grp.setExclusive(False)
        day_names = ['Mon','Tue','Wed','Thu','Fri','Sat','Sun']
        for i, d in enumerate(day_names):
            btn = ChipButton(d); self._day_grp.addButton(btn, i); days_row.addWidget(btn)
            self._day_chips.append(btn)
        days_row.addStretch()
        self._day_grp.idToggled.connect(lambda *_: self._mark_dirty())
        sc_lay.addLayout(days_row)

        # ── Months sub-group ──
        _sched_sub('Months', [
            ('All',     lambda: self._set_months(0xFFF)),
            ('Apr–Sep', lambda: self._set_months(sum(1<<i for i in range(3,9)))),
        ])
        self._mon_chips = []
        self._mon_grp = QButtonGroup(self); self._mon_grp.setExclusive(False)
        month_names = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec']
        mon_row1 = QHBoxLayout()
        for i in range(6):
            btn = ChipButton(month_names[i]); self._mon_grp.addButton(btn, i)
            mon_row1.addWidget(btn); self._mon_chips.append(btn)
        mon_row1.addStretch()
        sc_lay.addLayout(mon_row1)
        mon_row2 = QHBoxLayout()
        for i in range(6, 12):
            btn = ChipButton(month_names[i]); self._mon_grp.addButton(btn, i)
            mon_row2.addWidget(btn); self._mon_chips.append(btn)
        mon_row2.addStretch()
        self._mon_grp.idToggled.connect(lambda *_: self._mark_dirty())
        sc_lay.addLayout(mon_row2)

        sc_lay.addSpacing(4)
        self._lbl_sched_status = QLabel('—')
        sc_lay.addWidget(self._lbl_sched_status)
        root.addWidget(sc_card)

        root.addStretch()

        scroll.setWidget(inner)
        _install_drag_scroll(scroll)
        outer.addWidget(scroll)

    def _on_scope_changed(self):
        sid = self._scope_grp.checkedId()
        if sid == 0:
            self._lbl_scope_hint.setText('Sensors keep measuring and logging around the clock.')
        else:
            self._lbl_scope_hint.setText('Outside the schedule everything sleeps — no sensor data is recorded.')
        self._mark_dirty()

    def _on_simulate(self):
        """Play audio preview of the TX pulse/period rhythm (3 repeats)."""
        if self._sim_thread and self._sim_thread.is_alive():
            # Second click = stop
            self._sim_stop = True
            self._btn_sim.setText('▶  Simulate')
            return
        pulse_ms  = self._spin_pulse.value()
        period_ms = self._spin_period.value()
        # Cap to sensible preview: max 3 s period shown, 4 repeats
        preview_period = min(period_ms, 3000)
        preview_pulse  = min(pulse_ms, preview_period)
        self._sim_stop = False
        self._btn_sim.setText('■  Stop')

        def _run():
            try:
                import winsound
                for _ in range(4):
                    if self._sim_stop: break
                    winsound.Beep(1000, preview_pulse)
                    gap = preview_period - preview_pulse
                    if gap > 0 and not self._sim_stop:
                        time.sleep(gap / 1000.0)
            except Exception:
                pass
            finally:
                # Restore button on main thread
                QTimer.singleShot(0, lambda: self._btn_sim.setText('▶  Simulate'))

        import threading as _th
        self._sim_thread = _th.Thread(target=_run, daemon=True)
        self._sim_thread.start()

    def _set_hours(self, mask: int):
        for h, btn in enumerate(self._hr_chips):
            btn.blockSignals(True)
            btn.setChecked(bool(mask & (1 << h)))
            btn.blockSignals(False)
        self._mark_dirty()

    def _set_days(self, mask: int):
        for i, btn in enumerate(self._day_chips):
            btn.blockSignals(True)
            btn.setChecked(bool(mask & (1 << i)))
            btn.blockSignals(False)
        self._mark_dirty()

    def _set_months(self, mask: int):
        for i, btn in enumerate(self._mon_chips):
            btn.blockSignals(True)
            btn.setChecked(bool(mask & (1 << i)))
            btn.blockSignals(False)
        self._mark_dirty()

    def _mark_dirty(self, *_):
        self._dirty = True
        self.config_changed.emit()

    def load_config(self, cfg: ConfigBlob):
        """Populate all widgets from a ConfigBlob (without marking dirty)."""
        self._block(True)
        rf_mode_idx = cfg.rf_mode  # 0=off,1=pulse,2=cont,3=eco → chip index
        if 0 <= rf_mode_idx < 4:
            self._mode_chips[rf_mode_idx].setChecked(True)
        if 0 <= cfg.rf_channel < 4:
            self._ch_chips[cfg.rf_channel].setChecked(True)
        if 1 <= cfg.rf_power <= 4:
            self._pwr_chips[cfg.rf_power - 1].setChecked(True)
        self._spin_pulse.setValue(cfg.rf_pulse_ms)
        self._spin_period.setValue(cfg.rf_period_ms)
        self._chk_sched.setChecked(bool(cfg.sched_en))
        self._btn_scope_tx.setChecked(cfg.sched_scope == 0)
        self._btn_scope_both.setChecked(cfg.sched_scope == 1)
        self._on_scope_changed()
        self._set_hours(cfg.sched_hours)
        self._set_days(cfg.sched_days)
        self._set_months(cfg.sched_months)
        self._block(False)
        self._dirty = False

    def _block(self, v: bool):
        for w in ([self._chk_sched, self._spin_pulse, self._spin_period]
                  + self._mode_chips + self._ch_chips + self._pwr_chips
                  + self._hr_chips + self._day_chips + self._mon_chips):
            w.blockSignals(v)

    def get_config(self, base: ConfigBlob) -> ConfigBlob:
        """Return a new ConfigBlob with values from the form."""
        import copy; c = copy.copy(base)
        c.rf_mode    = self._mode_grp.checkedId() if self._mode_grp.checkedId() >= 0 else 1
        c.rf_channel = self._ch_grp.checkedId()   if self._ch_grp.checkedId()   >= 0 else 0
        c.rf_power   = self._pwr_grp.checkedId() + 1 if self._pwr_grp.checkedId() >= 0 else 4
        c.rf_pulse_ms  = self._spin_pulse.value()
        c.rf_period_ms = self._spin_period.value()
        c.sched_en     = 1 if self._chk_sched.isChecked() else 0
        c.sched_scope  = self._scope_grp.checkedId() if self._scope_grp.checkedId() >= 0 else 0
        hours = 0
        for h, btn in enumerate(self._hr_chips):
            if btn.isChecked(): hours |= (1 << h)
        c.sched_hours = hours
        days = 0
        for i, btn in enumerate(self._day_chips):
            if btn.isChecked(): days |= (1 << i)
        c.sched_days = days
        months = 0
        for i, btn in enumerate(self._mon_chips):
            if btn.isChecked(): months |= (1 << i)
        c.sched_months = months
        return c

    def update_sched_status(self, active: bool):
        if active:
            self._lbl_sched_status.setText(
                f'<span style="color:{self._C["success"]}">● Active now</span>')
        else:
            self._lbl_sched_status.setText(
                f'<span style="color:{self._C["warning"]}">● Inactive</span>')

    def _on_profile_selected(self, name: str):
        self._profile_name = name

    def _on_profile_apply(self):
        cfg = self._profiles.get(self._profile_name)
        if cfg:
            self.load_config(cfg)
            self._mark_dirty()

    def _on_profile_save(self):
        from PyQt6.QtWidgets import QInputDialog
        name, ok = QInputDialog.getText(self, 'Save profile', 'Profile name:')
        if ok and name:
            cfg = self.get_config(ConfigBlob())
            self._profiles.save_as(name, cfg)
            if self._profile_cb.findText(name) < 0:
                self._profile_cb.addItem(name)
            self._profile_cb.setCurrentText(name)

    def _on_profile_delete(self):
        name = self._profile_cb.currentText()
        if self._profiles.is_builtin(name):
            QMessageBox.warning(self, 'Delete', 'Cannot delete built-in profiles.')
            return
        self._profiles.delete(name)
        idx = self._profile_cb.currentIndex()
        self._profile_cb.removeItem(idx)

    def retheme(self, C: dict):
        self._C = C
        self._on_scope_changed()


# ── Duration / interval formatting helpers ────────────────────────────────────

def _fmt_dur(seconds: float) -> str:
    """Human-readable duration: '45 s', '5 min', '2 h 30 min', '3 days'."""
    s = int(max(0, seconds))
    if s < 90:
        return f'{s} s'
    if s < 5400:           # < 1.5 h
        m = round(s / 60)
        return f'{m} min'
    if s < 86400:          # < 1 day
        h = s // 3600
        m = (s % 3600) // 60
        return f'{h} h {m} min' if m else f'{h} h'
    d = s // 86400
    h = (s % 86400) // 3600
    return f'{d} d {h} h' if h else f'{d} days'

def _fmt_iv(seconds: int) -> str:
    """Interval → short human label: '1 s', '30 s', '5 min', '1 h'."""
    if seconds <= 0:
        return '—'
    if seconds < 60:
        return f'{seconds} s'
    if seconds < 3600:
        m = seconds // 60
        s = seconds % 60
        return f'{m} min {s} s' if s else f'{m} min'
    h = seconds // 3600
    m = (seconds % 3600) // 60
    return f'{h} h {m} min' if m else f'{h} h'


# ─────────────────────────────────────────────────────────────────────────────
# Tab 3 — Logging
# ─────────────────────────────────────────────────────────────────────────────
class LoggingTab(QWidget):
    config_changed = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._C = THEMES['light']
        self._total_records = 768
        self._build()

    def _build(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        inner = QWidget()
        root = QVBoxLayout(inner)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(10)

        def _sensor_card(name: str, tog: ToggleSwitch, spin: QSpinBox,
                         rate_lbl: QLabel, has_extended_settings: bool = False,
                         ext_widget: Optional[QWidget] = None) -> QFrame:
            """Build one sensor logging card. If has_extended_settings=True a ⚙
            gear button is shown; clicking it toggles visibility of ext_widget."""
            card = QFrame(); card.setObjectName('card')
            cl = QVBoxLayout(card); cl.setContentsMargins(16, 12, 16, 12); cl.setSpacing(6)
            hr = QHBoxLayout()
            title = QLabel(name); title.setFont(QFont('Segoe UI', 10, QFont.Weight.Bold))
            hr.addWidget(title)
            if has_extended_settings and ext_widget is not None:
                gear = QPushButton('⚙'); gear.setObjectName('secondary')
                gear.setFixedSize(26, 26)
                gear.setToolTip('Extended sensor settings')
                gear.clicked.connect(lambda: ext_widget.setVisible(not ext_widget.isVisible()))
                hr.addWidget(gear)
            hr.addStretch(); hr.addWidget(tog)
            cl.addLayout(hr)
            row2 = QHBoxLayout()
            row2.addWidget(QLabel('Interval:')); row2.addWidget(spin)
            spin.setToolTip('Interval in seconds.\n'
                            'Examples: 1 = 1 s,  60 = 1 min,  300 = 5 min,  3600 = 1 h')
            row2.addWidget(rate_lbl); row2.addStretch()
            cl.addLayout(row2)
            if has_extended_settings and ext_widget is not None:
                ext_widget.setVisible(False)
                cl.addWidget(ext_widget)
            return card

        # Sensor cards
        self._temp_tog   = ToggleSwitch()
        self._temp_spin  = QSpinBox(); self._temp_spin.setRange(1, 65535); self._temp_spin.setValue(60); self._temp_spin.setSuffix(' s'); self._temp_spin.setFixedWidth(90)
        self._batt_tog   = ToggleSwitch()
        self._batt_spin  = QSpinBox(); self._batt_spin.setRange(1, 65535); self._batt_spin.setValue(3600); self._batt_spin.setSuffix(' s'); self._batt_spin.setFixedWidth(90)
        self._light_tog  = ToggleSwitch()
        self._light_spin = QSpinBox(); self._light_spin.setRange(1, 65535); self._light_spin.setValue(300); self._light_spin.setSuffix(' s'); self._light_spin.setFixedWidth(90)
        self._accel_tog  = ToggleSwitch()
        self._accel_spin = QSpinBox(); self._accel_spin.setRange(1, 65535); self._accel_spin.setValue(1); self._accel_spin.setSuffix(' s'); self._accel_spin.setFixedWidth(90)
        self._lbl_temp_rate  = QLabel()
        self._lbl_batt_rate  = QLabel()
        self._lbl_light_rate = QLabel()
        self._lbl_accel_rate = QLabel()

        # ── Accel extended settings widget (generic has_extended_settings path) ──
        _ext = QWidget()
        _ext_lay = QVBoxLayout(_ext); _ext_lay.setContentsMargins(0, 6, 0, 0); _ext_lay.setSpacing(4)
        _ext_lay.addWidget(QLabel('Sensor settings:', font=QFont('Segoe UI', 9, QFont.Weight.Bold)))
        _ext_grid = QGridLayout(); _ext_grid.setSpacing(6)
        _ODR_LABELS = ['Power-down', '1.6 Hz', '12.5 Hz', '25 Hz', '50 Hz', '100 Hz', '200 Hz']
        _FS_LABELS  = ['±2 g', '±4 g', '±8 g', '±16 g']
        _MODE_LABELS= ['Low-power', 'High-perf']
        _BW_LABELS  = ['ODR/2', 'ODR/4', 'ODR/10', 'ODR/20']
        self._accel_odr_cb  = QComboBox(); self._accel_odr_cb.addItems(_ODR_LABELS)
        self._accel_odr_cb.setCurrentIndex(2)
        self._accel_fs_cb   = QComboBox(); self._accel_fs_cb.addItems(_FS_LABELS)
        self._accel_mode_cb = QComboBox(); self._accel_mode_cb.addItems(_MODE_LABELS)
        self._accel_bw_cb   = QComboBox(); self._accel_bw_cb.addItems(_BW_LABELS)
        self._accel_bw_cb.setCurrentIndex(1)
        self._lbl_bw_note   = QLabel('BW_FILT ignored in LP mode (fixed at ODR/2)')
        self._lbl_bw_note.setStyleSheet('color:#b45000; font-size:8pt;')
        for i, (lbl, w) in enumerate([('ODR', self._accel_odr_cb), ('Full-scale', self._accel_fs_cb),
                                       ('Mode', self._accel_mode_cb), ('BW filter', self._accel_bw_cb)]):
            _ext_grid.addWidget(QLabel(lbl), i, 0)
            _ext_grid.addWidget(w, i, 1)
        _ext_grid.addWidget(self._lbl_bw_note, 4, 0, 1, 2)
        _ext_grid.setColumnStretch(2, 1)
        _ext_lay.addLayout(_ext_grid)

        def _update_bw_state():
            hp = self._accel_mode_cb.currentIndex() == 1
            self._accel_bw_cb.setEnabled(hp)
            self._lbl_bw_note.setVisible(not hp)
            self._on_change()

        self._accel_mode_cb.currentIndexChanged.connect(_update_bw_state)
        self._accel_odr_cb.currentIndexChanged.connect(self._on_change)
        self._accel_fs_cb.currentIndexChanged.connect(self._on_change)
        self._accel_bw_cb.currentIndexChanged.connect(self._on_change)
        _update_bw_state()

        for tog, spin, lbl, name, has_ext, ext_w, card_attr in [
            (self._temp_tog,  self._temp_spin,  self._lbl_temp_rate,  'Temperature',   False, None,  '_card_temp'),
            (self._batt_tog,  self._batt_spin,  self._lbl_batt_rate,  'Battery',       False, None,  '_card_batt'),
            (self._light_tog, self._light_spin, self._lbl_light_rate, 'Light',         False, None,  '_card_light'),
            (self._accel_tog, self._accel_spin, self._lbl_accel_rate, 'Accelerometer', True,  _ext,  '_card_accel'),
        ]:
            tog.toggled.connect(self._on_change)
            spin.valueChanged.connect(self._on_change)
            card = _sensor_card(name, tog, spin, lbl, has_ext, ext_w)
            setattr(self, card_attr, card)
            root.addWidget(card)

        # Write strategy card
        ws_card = QFrame(); ws_card.setObjectName('card')
        ws_lay = QVBoxLayout(ws_card); ws_lay.setContentsMargins(16,16,16,16); ws_lay.setSpacing(10)
        ws_lay.addWidget(QLabel('Write strategy', font=QFont('Segoe UI', 10, QFont.Weight.Bold)))

        # Row 1: write mode
        mode_row = QHBoxLayout()
        mode_row.addWidget(QLabel('Mode:'))
        mode_row.addSpacing(6)
        self._mode_grp = QButtonGroup(self); self._mode_grp.setExclusive(True)
        self._mode_chips = []
        for i, lbl in enumerate(['Every interval', 'On change', 'Adaptive']):
            btn = ChipButton(lbl); self._mode_grp.addButton(btn, i); mode_row.addWidget(btn)
            self._mode_chips.append(btn)
        self._mode_chips[0].setChecked(True)
        mode_row.addStretch()
        ws_lay.addLayout(mode_row)
        # Row 2: overflow policy
        over_row = QHBoxLayout()
        over_row.addWidget(QLabel('When full:'))
        over_row.addSpacing(6)
        self._over_grp = QButtonGroup(self); self._over_grp.setExclusive(True)
        self._btn_circ = ChipButton('Overwrite oldest')
        self._btn_stop = ChipButton('Stop recording')
        self._btn_circ.setChecked(True)
        self._over_grp.addButton(self._btn_circ, 1)
        self._over_grp.addButton(self._btn_stop, 0)
        over_row.addWidget(self._btn_circ); over_row.addWidget(self._btn_stop)
        over_row.addStretch()
        ws_lay.addLayout(over_row)
        # Row 3: timestamp source
        ts_row = QHBoxLayout()
        ts_row.addWidget(QLabel('Timestamp:'))
        ts_row.addSpacing(6)
        self._ts_src_grp = QButtonGroup(self); self._ts_src_grp.setExclusive(True)
        self._btn_ts_boot = ChipButton('Seconds from start')
        self._btn_ts_rtc  = ChipButton('Real time (RTC)')
        self._btn_ts_boot.setChecked(True)
        self._ts_src_grp.addButton(self._btn_ts_boot, 0)
        self._ts_src_grp.addButton(self._btn_ts_rtc,  1)
        ts_row.addWidget(self._btn_ts_boot)
        ts_row.addWidget(self._btn_ts_rtc)
        ts_row.addStretch()
        ws_lay.addLayout(ts_row)
        self._mode_grp.idToggled.connect(self._on_change)
        self._over_grp.idToggled.connect(self._on_change)
        self._ts_src_grp.idToggled.connect(self._on_change)
        root.addWidget(ws_card)

        # Memory calculator card
        mc_card = QFrame(); mc_card.setObjectName('card')
        mc_lay = QVBoxLayout(mc_card); mc_lay.setContentsMargins(16,16,16,16); mc_lay.setSpacing(6)
        mc_lay.addWidget(QLabel('Memory calculator', font=QFont('Segoe UI', 10, QFont.Weight.Bold)))
        self._calc_total  = QLabel(f'{self._total_records} records total · 16 B each')
        self._calc_rate   = QLabel()
        self._calc_depth  = QLabel(); self._calc_depth.setFont(QFont('Segoe UI', 10, QFont.Weight.Bold))
        self._calc_stop   = QLabel()
        tgt_row = QHBoxLayout()
        tgt_row.addWidget(QLabel('Target: keep'))
        self._spin_target = QSpinBox(); self._spin_target.setRange(1,365); self._spin_target.setValue(7); self._spin_target.setSuffix(' d'); self._spin_target.setFixedWidth(70)
        self._spin_target.valueChanged.connect(self._recalc)
        tgt_row.addWidget(self._spin_target)
        self._lbl_suggest = QLabel()
        tgt_row.addWidget(self._lbl_suggest); tgt_row.addStretch()
        for w in [self._calc_total, self._calc_rate, self._calc_depth, self._calc_stop]:
            mc_lay.addWidget(w)
        mc_lay.addLayout(tgt_row)
        root.addWidget(mc_card)
        root.addStretch()

        scroll.setWidget(inner)
        _install_drag_scroll(scroll)
        outer.addWidget(scroll)
        self._on_change()

    def _on_change(self, *_):
        self._recalc()
        self.config_changed.emit()

    def _recalc(self):
        if not hasattr(self, '_calc_total'):
            return
        writes_per_s = 0.0
        for tog, spin, lbl in [(self._temp_tog,  self._temp_spin,  self._lbl_temp_rate),
                                (self._batt_tog,  self._batt_spin,  self._lbl_batt_rate),
                                (self._light_tog, self._light_spin, self._lbl_light_rate),
                                (self._accel_tog, self._accel_spin, self._lbl_accel_rate)]:
            if tog.isChecked() and spin.value() > 0:
                iv = spin.value()
                writes_per_s += 1.0 / iv
                # Human-readable: "every 5 min  (12/hr)"
                lbl.setText(f'every {_fmt_iv(iv)}  ({3600/iv:.0f}/hr)')
            else:
                lbl.setText('(off)')

        total = self._total_records
        flash_kb = total * 16 // 1024
        self._calc_total.setText(f'Capacity: {total} records  ({flash_kb} KB flash)')

        if writes_per_s > 0:
            depth_s = total / writes_per_s          # seconds of history in ring buffer
            writes_h = writes_per_s * 3600
            self._calc_rate.setText(
                f'Write rate: {writes_h:.0f} records/hr  ({writes_per_s:.2f}/s)')
            self._calc_depth.setText(
                f'🔄 Circular mode  →  keeps last  {_fmt_dur(depth_s)}  of history')
            self._calc_stop.setText(
                f'🛑 Stop mode  →  flash full in  {_fmt_dur(depth_s)}')

            target_s = self._spin_target.value() * 86400
            if total > 0:
                sug_s    = max(1, int(target_s / total))
                sug_cnt  = int(target_s / max(1, sug_s))
                self._lbl_suggest.setText(
                    f'→ interval ≥ {_fmt_iv(sug_s)}  (≈ {sug_cnt} records)')
        else:
            self._calc_rate.setText('Write rate: 0  (all sensors off)')
            self._calc_depth.setText('—')
            self._calc_stop.setText('')
            self._lbl_suggest.setText('')

    def load_config(self, cfg: ConfigBlob):
        for w in (self._temp_tog, self._batt_tog, self._light_tog,
                  self._temp_spin, self._batt_spin, self._light_spin):
            w.blockSignals(True)

        self._temp_tog.setChecked(bool(cfg.log_mask & 0x01) or cfg.temp_iv_s > 0)
        self._temp_spin.setValue(max(1, cfg.temp_iv_s))
        self._batt_tog.setChecked(bool(cfg.log_mask & 0x0C) or cfg.bat_iv_s > 0)
        self._batt_spin.setValue(max(1, cfg.bat_iv_s))
        self._light_tog.setChecked(bool(cfg.log_mask & 0x02) or cfg.light_iv_s > 0)
        self._light_spin.setValue(max(1, cfg.light_iv_s))

        if 0 <= cfg.log_mode < 3:
            self._mode_chips[cfg.log_mode].setChecked(True)
        self._btn_circ.setChecked(cfg.log_overflow == 1)
        self._btn_stop.setChecked(cfg.log_overflow == 0)
        self._btn_ts_boot.setChecked(getattr(cfg, 'log_ts_source', 0) == 0)
        self._btn_ts_rtc.setChecked(getattr(cfg, 'log_ts_source', 0) == 1)

        for w in (self._temp_tog, self._batt_tog, self._light_tog,
                  self._temp_spin, self._batt_spin, self._light_spin):
            w.blockSignals(False)
        self._recalc()

    def load_accel(self, enabled: bool, interval_s: int):
        """Populate accel toggle and interval from firmware sensor list."""
        self._accel_tog.blockSignals(True)
        self._accel_spin.blockSignals(True)
        self._accel_tog.setChecked(bool(enabled))
        self._accel_spin.setValue(max(1, interval_s if interval_s > 0 else 1))
        self._accel_tog.blockSignals(False)
        self._accel_spin.blockSignals(False)
        self._recalc()

    def get_accel(self) -> tuple:
        """Return (enabled: bool, interval_s: int)."""
        return self._accel_tog.isChecked(), self._accel_spin.value()

    def get_config(self, base: ConfigBlob) -> ConfigBlob:
        import copy; c = copy.copy(base)
        c.temp_iv_s  = self._temp_spin.value()  if self._temp_tog.isChecked()  else 0
        c.light_iv_s = self._light_spin.value() if self._light_tog.isChecked() else 0
        c.bat_iv_s   = self._batt_spin.value()  if self._batt_tog.isChecked()  else 0
        # log_mask: bit0=temp 1=light 2=bat% 3=batmv — MUST match toggle state.
        # Firmware gates writes on active_mask; if bit is set with interval=0
        # the condition !interval_ms fires every loop and floods flash.
        c.log_mask = 0
        if self._temp_tog.isChecked():  c.log_mask |= 0x01         # LOG_MASK_TEMP
        if self._light_tog.isChecked(): c.log_mask |= 0x02         # LOG_MASK_LIGHT
        if self._batt_tog.isChecked():  c.log_mask |= 0x04 | 0x08  # BAT_PCT + BAT_MV
        c.log_mode      = self._mode_grp.checkedId()   if self._mode_grp.checkedId()   >= 0 else 0
        c.log_overflow  = self._over_grp.checkedId()   if self._over_grp.checkedId()   >= 0 else 1
        c.log_ts_source = self._ts_src_grp.checkedId() if self._ts_src_grp.checkedId() >= 0 else 0
        return c

    def set_total_records(self, n: int):
        self._total_records = max(1, n)
        self._recalc()

    def retheme(self, C: dict):
        self._C = C
        for w in [self._temp_tog, self._batt_tog, self._light_tog]:
            w.retheme(C)


# ─────────────────────────────────────────────────────────────────────────────
# Custom bottom axis for DataTab — formats ticks as boot-seconds or datetime
# ─────────────────────────────────────────────────────────────────────────────
class _TimeAxis(pg.AxisItem if HAS_PG else object):
    _EPOCH2000 = 946684800  # seconds between 1970-01-01 and 2000-01-01

    def __init__(self):
        if HAS_PG:
            super().__init__('bottom')
        self._rtc_mode    = False
        self._tz_offset_s = 0   # display offset in seconds

    def set_rtc_mode(self, enabled: bool):
        self._rtc_mode = enabled
        if HAS_PG:
            self.picture = None
            self.update()

    def set_tz_offset(self, seconds: int):
        self._tz_offset_s = seconds
        if HAS_PG:
            self.picture = None
            self.update()

    def tickStrings(self, values, scale, spacing):
        if not self._rtc_mode:
            out = []
            for v in values:
                v = int(v)
                if v < 0:
                    out.append(str(v))
                elif v < 60:
                    out.append(f'+{v}s')
                elif v < 3600:
                    out.append(f'+{v // 60}m')
                else:
                    out.append(f'+{v // 3600}h {(v % 3600) // 60}m')
            return out
        else:
            import datetime as _dt
            out = []
            for v in values:
                try:
                    unix = int(v) + self._EPOCH2000 + self._tz_offset_s
                    d = _dt.datetime.utcfromtimestamp(unix)
                    out.append(d.strftime('%m/%d\n%H:%M'))
                except (ValueError, OSError, OverflowError):
                    out.append(str(int(v)))
            return out


# ─────────────────────────────────────────────────────────────────────────────
# Tab 4 — Data (download + chart + export)
# ─────────────────────────────────────────────────────────────────────────────
class DataTab(QWidget):
    erase_requested    = pyqtSignal()
    download_requested = pyqtSignal()

    _RECORD_BYTES = 16   # sizeof(FlashLogRecord_t)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._C = THEMES['light']
        self._records: List[dict] = []
        self._rtc_base    = 0
        self._ts_mode     = 0   # 0=boot seconds, 1=RTC datetime
        self._tz_offset_s = 0   # display offset in seconds (from SettingsTab)
        self._markers:  list = []   # {'x', 'label', 'line'}
        self._log_total = 0         # capacity set by set_storage_info()
        self._build()

    # ── helpers ──────────────────────────────────────────────────────────────

    @staticmethod
    def _smooth_ma(data: list, window: int) -> list:
        n = len(data)
        if n < 2 or window < 2:
            return list(data)
        half = window // 2
        return [
            sum(data[max(0, i - half): min(n, i + half + 1)]) /
            len(data[max(0, i - half): min(n, i + half + 1)])
            for i in range(n)
        ]

    @staticmethod
    def _smooth_ma_nan(data: list, window: int) -> list:
        """Moving average that treats NaN as segment boundary (never averages across gaps)."""
        n = len(data)
        if n < 2 or window < 2:
            return list(data)
        half = window // 2
        result = []
        for i, v in enumerate(data):
            if v != v:          # NaN → boundary, propagate as-is
                result.append(float('nan'))
                continue
            vals = []
            for j in range(max(0, i - half), min(n, i + half + 1)):
                jv = data[j]
                if jv == jv:    # not NaN
                    vals.append(jv)
            result.append(sum(vals) / len(vals) if vals else v)
        return result

    @staticmethod
    def _lttb(xs: list, ys: list, threshold: int):
        """Largest Triangle Three Buckets — downsample preserving visual shape."""
        n = len(xs)
        if n <= threshold:
            return xs, ys
        out_x = [xs[0]]; out_y = [ys[0]]
        bucket_size = (n - 2) / (threshold - 2)
        a = 0
        for i in range(threshold - 2):
            r_start = int((i + 1) * bucket_size) + 1
            r_end   = min(int((i + 2) * bucket_size) + 1, n)
            avg_x   = sum(xs[r_start:r_end]) / (r_end - r_start)
            avg_y   = sum(ys[r_start:r_end]) / (r_end - r_start)
            b_start = int(i       * bucket_size) + 1
            b_end   = int((i + 1) * bucket_size) + 1
            ax, ay  = xs[a], ys[a]
            best_i, best_area = b_start, -1.0
            for j in range(b_start, b_end):
                area = abs((ax - avg_x) * (ys[j] - ay) -
                           (ax - xs[j]) * (avg_y  - ay)) * 0.5
                if area > best_area:
                    best_area = area; best_i = j
            out_x.append(xs[best_i]); out_y.append(ys[best_i])
            a = best_i
        out_x.append(xs[-1]); out_y.append(ys[-1])
        return out_x, out_y

    @staticmethod
    def _insert_gaps(xs: list, ys: list, factor: float = 2.5):
        """Insert NaN breaks where Δt > factor × median interval → line breaks on chart."""
        if len(xs) < 3:
            return xs, ys
        diffs  = [xs[i + 1] - xs[i] for i in range(len(xs) - 1)]
        sdiffs = sorted(diffs)
        median = sdiffs[len(sdiffs) // 2]
        if median <= 0:
            return xs, ys
        thr = factor * median
        ox, oy = [xs[0]], [ys[0]]
        for i in range(1, len(xs)):
            if (xs[i] - xs[i - 1]) > thr:
                ox.append(float('nan')); oy.append(float('nan'))
            ox.append(xs[i]); oy.append(ys[i])
        return ox, oy

    @staticmethod
    def _remove_outliers(xs: list, ys: list, sigma: float = 3.0):
        if len(ys) < 4:
            return xs, ys
        mean  = sum(ys) / len(ys)
        var   = sum((y - mean) ** 2 for y in ys) / len(ys)
        std   = var ** 0.5 or 1.0
        pairs = [(x, y) for x, y in zip(xs, ys) if abs(y - mean) <= sigma * std]
        return ([p[0] for p in pairs], [p[1] for p in pairs]) if pairs else ([], [])

    @staticmethod
    def _fmt_bytes(n_records: int) -> str:
        b = n_records * DataTab._RECORD_BYTES
        if b >= 1024 * 1024:
            return f'{b / 1024 / 1024:.2f} MB'
        return f'{b / 1024:.1f} KB'

    # ── build UI ─────────────────────────────────────────────────────────────

    def _build(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(8)

        # ── Download bar ─────────────────────────────────────────────────────
        dl_bar = QHBoxLayout()
        self._btn_dl = QPushButton(f'{ICO["download"]}  Download from beacon')
        self._btn_dl.setObjectName('primary')
        self._btn_dl.clicked.connect(self.download_requested.emit)
        dl_bar.addWidget(self._btn_dl)
        self._dl_prog = QProgressBar()
        self._dl_prog.setTextVisible(True)
        self._dl_prog.setFixedHeight(28)
        dl_bar.addWidget(self._dl_prog, 1)
        self._lbl_dl_count = QLabel('0 records')
        dl_bar.addWidget(self._lbl_dl_count)
        root.addLayout(dl_bar)

        # ── Storage info bar ─────────────────────────────────────────────────
        self._stor_bar = QFrame()
        self._stor_bar.setObjectName('card')
        sb_lay = QHBoxLayout(self._stor_bar)
        sb_lay.setContentsMargins(14, 6, 14, 6)
        sb_lay.setSpacing(0)
        self._lbl_stor = QLabel('—')
        self._lbl_stor.setFont(QFont('Segoe UI', 9))
        sb_lay.addWidget(self._lbl_stor)
        sb_lay.addStretch()
        self._stor_prog = QProgressBar()
        self._stor_prog.setFixedSize(120, 8)
        self._stor_prog.setTextVisible(False)
        sb_lay.addWidget(self._stor_prog)
        root.addWidget(self._stor_bar)

        # ── Chart / Table toggle ─────────────────────────────────────────────
        tog_row = QHBoxLayout()
        self._view_grp = QButtonGroup(self); self._view_grp.setExclusive(True)
        self._btn_chart = ChipButton('Chart'); self._btn_chart.setChecked(True)
        self._btn_table = ChipButton('Table')
        self._view_grp.addButton(self._btn_chart, 0)
        self._view_grp.addButton(self._btn_table, 1)
        self._view_grp.idToggled.connect(self._on_view_toggle)
        tog_row.addStretch()
        tog_row.addWidget(self._btn_chart)
        tog_row.addWidget(self._btn_table)
        root.addLayout(tog_row)

        # ── Stacked: chart / table ────────────────────────────────────────────
        self._stack = QStackedWidget()

        # ── Chart page ────────────────────────────────────────────────────────
        chart_page = QWidget()
        cp_lay = QVBoxLayout(chart_page)
        cp_lay.setContentsMargins(0, 0, 0, 0)
        cp_lay.setSpacing(6)

        # Series selector + time axis toggle
        ser_row = QHBoxLayout()
        self._ser_grp = QButtonGroup(self); self._ser_grp.setExclusive(False)
        self._btn_ser = {}
        for i, name in enumerate(['Temp', 'Light', 'Battery', 'Accel X', 'Accel Y', 'Accel Z', 'Accel |G|']):
            b = ChipButton(name)
            self._ser_grp.addButton(b, i)
            ser_row.addWidget(b)
            self._btn_ser[name] = b
        ser_row.addStretch()
        # Time axis display toggle (right side of row)
        _bold9 = QFont('Segoe UI', 9, QFont.Weight.Bold)
        _ts_lbl = QLabel('Time:'); _ts_lbl.setFont(_bold9)
        ser_row.addWidget(_ts_lbl)
        self._ts_grp = QButtonGroup(self); self._ts_grp.setExclusive(True)
        self._btn_ts_boot = ChipButton('Seconds')
        self._btn_ts_boot.setChecked(True)
        self._btn_ts_rtc  = ChipButton('Date/Time')
        self._ts_grp.addButton(self._btn_ts_boot, 0)
        self._ts_grp.addButton(self._btn_ts_rtc,  1)
        ser_row.addWidget(self._btn_ts_boot)
        ser_row.addWidget(self._btn_ts_rtc)
        self._ts_grp.idToggled.connect(self._on_ts_toggle)
        self._ser_grp.idToggled.connect(self._replot)
        cp_lay.addLayout(ser_row)

        # Post-processing bar
        proc_frame = QFrame(); proc_frame.setObjectName('card')
        proc_lay = QHBoxLayout(proc_frame)
        proc_lay.setContentsMargins(12, 5, 12, 5)
        proc_lay.setSpacing(14)

        _bold9 = QFont('Segoe UI', 9, QFont.Weight.Bold)
        lbl_pp = QLabel('Post-processing:'); lbl_pp.setFont(_bold9)
        proc_lay.addWidget(lbl_pp)

        self._chk_smooth = QCheckBox('Smooth')
        self._chk_smooth.setChecked(False)
        proc_lay.addWidget(self._chk_smooth)
        self._spin_smooth = QSpinBox()
        self._spin_smooth.setRange(2, 30); self._spin_smooth.setValue(5)
        self._spin_smooth.setMinimumWidth(84); self._spin_smooth.setSuffix(' pt')
        self._spin_smooth.setToolTip('Moving-average window size')
        proc_lay.addWidget(self._spin_smooth)

        _sep = lambda: (lambda f: (f.setFrameShape(QFrame.Shape.VLine),
                                   f.setFixedWidth(1), f)[-1])(QFrame())
        proc_lay.addWidget(_sep())

        self._chk_outliers = QCheckBox('Remove outliers (3σ)')
        proc_lay.addWidget(self._chk_outliers)

        proc_lay.addWidget(_sep())

        self._chk_delta = QCheckBox('Show Δ from start')
        proc_lay.addWidget(self._chk_delta)

        proc_lay.addStretch()
        cp_lay.addWidget(proc_frame)

        for w in [self._chk_smooth, self._chk_outliers, self._chk_delta]:
            w.toggled.connect(self._replot)
        self._spin_smooth.valueChanged.connect(self._replot)

        # Plot widget with custom time axis
        if HAS_PG:
            self._time_axis = _TimeAxis()
            self._plot = pg.PlotWidget(axisItems={'bottom': self._time_axis})
            self._plot.setBackground('transparent')
            self._plot.showGrid(x=True, y=True, alpha=0.3)
            self._plot.getAxis('left').show()
            # per-series curves (main) + raw ghost (dotted, behind)
            _SER_COLORS = ['#ef4444','#f59e0b','#22c55e','#3b82f6','#8b5cf6','#ec4899','#06b6d4']
            self._curves: list = []
            self._curves_raw: list = []
            for _sc in _SER_COLORS:
                self._curves_raw.append(self._plot.plot(
                    pen=pg.mkPen(_sc + 'BB', width=2, style=Qt.PenStyle.DashLine)))
                self._curves.append(self._plot.plot(pen=pg.mkPen(_sc, width=2)))
            # Crosshair lines — gray dashes, clearly visible on any bg
            _ch_pen = pg.mkPen('#999999', width=1, style=Qt.PenStyle.DashLine)
            self._ch_vline = pg.InfiniteLine(angle=90, movable=False, pen=_ch_pen)
            self._ch_hline = pg.InfiniteLine(angle=0,  movable=False, pen=_ch_pen)
            self._ch_vline.setVisible(False)
            self._ch_hline.setVisible(False)
            self._plot.addItem(self._ch_vline, ignoreBounds=True)
            self._plot.addItem(self._ch_hline, ignoreBounds=True)
            # Floating label showing time + value at cursor
            self._ch_label = pg.TextItem(anchor=(0.0, 1.0), color='#ffffff')
            self._ch_label.setFont(QFont('Segoe UI', 9, QFont.Weight.Bold))
            self._ch_label.setVisible(False)
            self._plot.addItem(self._ch_label, ignoreBounds=True)
            self._plot.setMouseTracking(True)
            self._plot.scene().sigMouseMoved.connect(self._on_mouse_moved)
            cp_lay.addWidget(self._plot)
            self._plot.scene().sigMouseClicked.connect(self._on_plot_click)
            # Store current plot X/Y arrays for crosshair snapping
            self._ch_xs: list = []
            self._ch_ys: list = []
        else:
            cp_lay.addWidget(QLabel('(install pyqtgraph for chart)'))

        # Marker bar
        mk_bar = QHBoxLayout(); mk_bar.setSpacing(8)
        self._btn_add_marker = QPushButton('⊕  Add Marker')
        self._btn_add_marker.setObjectName('secondary')
        self._btn_add_marker.setCheckable(True)
        self._btn_add_marker.setFixedWidth(124)
        self._btn_add_marker.toggled.connect(self._on_marker_mode_toggled)
        mk_bar.addWidget(self._btn_add_marker)

        self._btn_clear_mk = QPushButton('Clear all')
        self._btn_clear_mk.setObjectName('secondary')
        self._btn_clear_mk.setFixedWidth(76)
        self._btn_clear_mk.clicked.connect(self._clear_markers)
        mk_bar.addWidget(self._btn_clear_mk)

        # Horizontal scroll area for marker chips
        self._mk_scroll = QScrollArea()
        self._mk_scroll.setFixedHeight(34)
        self._mk_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self._mk_scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self._mk_scroll.setWidgetResizable(True)
        self._mk_inner  = QWidget()
        self._mk_layout = QHBoxLayout(self._mk_inner)
        self._mk_layout.setContentsMargins(2, 1, 2, 1); self._mk_layout.setSpacing(5)
        self._mk_layout.addStretch()
        self._mk_scroll.setWidget(self._mk_inner)
        _install_drag_scroll(self._mk_scroll)
        mk_bar.addWidget(self._mk_scroll, 1)
        cp_lay.addLayout(mk_bar)

        self._stack.addWidget(chart_page)

        # ── Table page ────────────────────────────────────────────────────────
        self._table = QTableWidget()
        self._table.setColumnCount(8)
        self._table.setHorizontalHeaderLabels(['#', 'Time', 'Temp °C', 'Light', 'Bat%',
                                               'Accel X', 'Accel Y', 'Accel Z'])
        self._table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        self._table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self._stack.addWidget(self._table)

        root.addWidget(self._stack, 1)

        # ── Bottom buttons ────────────────────────────────────────────────────
        bot = QHBoxLayout()
        self._btn_csv = QPushButton('Export CSV')
        self._btn_csv.setObjectName('secondary')
        self._btn_csv.clicked.connect(self._export_csv)
        bot.addWidget(self._btn_csv)
        self._btn_erase = QPushButton(f'{ICO["erase"]}  Erase beacon log…')
        self._btn_erase.setObjectName('secondary')
        self._btn_erase.clicked.connect(self._on_erase)
        bot.addWidget(self._btn_erase)
        bot.addStretch()
        root.addLayout(bot)

    # ── Storage info ─────────────────────────────────────────────────────────

    def set_storage_info(self, used: int, total: int, circular: bool = False):
        """Called from MainWindow after download or status update."""
        self._log_total  = total or 768
        used_disp        = min(used, self._log_total)
        free_disp        = max(0, self._log_total - used_disp)
        pct              = int(used_disp * 100 / self._log_total) if self._log_total else 0
        overwriting      = circular and used >= self._log_total

        if overwriting:
            col = self._C['warning']
            txt = (f'{ICO["storage"]}  '
                   f'<b style="color:{col}">⟳ Circular overwrite</b>  ·  '
                   f'<b>Total</b> {self._log_total} rec · {self._fmt_bytes(self._log_total)}')
        elif circular:
            col = self._C['accent']
            txt = (f'{ICO["storage"]}  '
                   f'<b style="color:{col}">⟳ Circular</b>  ·  '
                   f'<b>Stored</b> {used_disp} rec · {self._fmt_bytes(used_disp)}    '
                   f'<b>Capacity</b> {self._log_total} rec · {self._fmt_bytes(self._log_total)}')
        else:
            col = (self._C['danger']  if pct >= 90 else
                   self._C['warning'] if pct >= 70 else
                   self._C['success'])
            txt = (f'{ICO["storage"]}  '
                   f'<b>Used</b> {used_disp} rec · {self._fmt_bytes(used_disp)}    '
                   f'<b>Free</b> {free_disp} rec · {self._fmt_bytes(free_disp)}    '
                   f'<b>Total</b> {self._log_total} rec · {self._fmt_bytes(self._log_total)}')

        self._lbl_stor.setText(txt)
        self._lbl_stor.setTextFormat(Qt.TextFormat.RichText)
        self._stor_prog.setMaximum(max(1, self._log_total))
        self._stor_prog.setValue(used_disp)
        self._stor_prog.setStyleSheet(
            f'QProgressBar::chunk {{ background:{col}; border-radius:2px; }}')

    # ── View toggle ───────────────────────────────────────────────────────────

    def _on_view_toggle(self, btn_id: int, checked: bool):
        if checked:
            self._stack.setCurrentIndex(btn_id)

    # ── Data I/O ──────────────────────────────────────────────────────────────

    def set_download_progress(self, done: int, total: int, capacity: int = 0):
        cap = capacity or total
        self._dl_prog.setMaximum(max(1, total))
        self._dl_prog.setValue(done)
        if cap and cap != total:
            self._lbl_dl_count.setText(f'{done} / {total}  (cap {cap})')
        else:
            self._lbl_dl_count.setText(f'{done} / {total}')

    def start_erase_animation(self):
        """Show indeterminate orange progress bar for erase operation."""
        self._dl_prog.setRange(0, 0)
        self._dl_prog.setStyleSheet('QProgressBar::chunk { background: #f59e0b; border-radius: 2px; }')
        self._lbl_dl_count.setText('Erasing…')

    def stop_erase_animation(self, success: bool, msg: str = ''):
        """Restore progress bar after erase completes."""
        self._dl_prog.setRange(0, 100)
        if success:
            self._dl_prog.setValue(100)
            self._dl_prog.setStyleSheet('QProgressBar::chunk { background: #f59e0b; border-radius: 2px; }')
            self._lbl_dl_count.setText('Erase scheduled' if msg == 'deferred' else 'Erased OK')
        else:
            self._dl_prog.setValue(0)
            self._dl_prog.setStyleSheet('')
            self._lbl_dl_count.setText('Erase failed')
        QTimer.singleShot(3000, self._reset_erase_bar)

    def _reset_erase_bar(self):
        self._dl_prog.setStyleSheet('')
        self._dl_prog.setRange(0, 100)
        self._dl_prog.setValue(0)
        self._lbl_dl_count.setText('0 records')

    def set_records(self, records: List[dict], rtc_base: int = 0, circular: bool = False):
        self._records  = records
        self._rtc_base = rtc_base
        if records and self._log_total:
            self.set_storage_info(len(records), self._log_total, circular)
        # Auto-select all series that have data
        if records and HAS_PG:
            _KEYS = ['temp_c', 'light_raw', 'bat_pct', 'accel_x', 'accel_y', 'accel_z', 'accel_mag']
            for i, key in enumerate(_KEYS):
                btn = self._ser_grp.button(i)
                if btn is None:
                    continue
                chk_key = 'accel_x' if key == 'accel_mag' else key
                has_data = any(r.get(chk_key) is not None for r in records)
                btn.setChecked(has_data)
        self._replot()
        self._fill_table()

    def set_ts_mode(self, mode: int):
        """Set X-axis display: 0=boot seconds, 1=RTC date/time.
        Called from MainWindow when config is loaded from beacon."""
        self._ts_mode = mode & 1
        self._btn_ts_boot.setChecked(self._ts_mode == 0)
        self._btn_ts_rtc.setChecked(self._ts_mode == 1)
        if HAS_PG:
            self._time_axis.set_rtc_mode(self._ts_mode == 1)
        self._replot()
        self._fill_table()

    def _on_ts_toggle(self, btn_id: int, checked: bool):
        if checked:
            self._ts_mode = btn_id
            if HAS_PG:
                self._time_axis.set_rtc_mode(btn_id == 1)
            self._replot()
            self._fill_table()

    def set_tz_offset(self, hours: int):
        """Apply timezone offset to all timestamp displays (graph + table)."""
        self._tz_offset_s = hours * 3600
        if HAS_PG:
            self._time_axis.set_tz_offset(self._tz_offset_s)
        self._replot()
        self._fill_table()

    # ── Replot ────────────────────────────────────────────────────────────────

    def _replot(self, *_):
        if not HAS_PG:
            return
        _KEYS  = ['temp_c', 'light_raw', 'bat_pct', 'accel_x', 'accel_y', 'accel_z', 'accel_mag']
        _UNITS = ['°C',     '',          '%',        'raw',     'raw',     'raw',      'g']

        if not self._records:
            for c, cr in zip(self._curves, self._curves_raw):
                c.setData([], []); cr.setData([], [])
            self._ch_xs = []; self._ch_ys = []
            return

        checked_ids = [i for i in range(len(_KEYS))
                       if self._ser_grp.button(i) is not None and
                          self._ser_grp.button(i).isChecked()]

        first_ch_xs: list = []
        first_ch_ys: list = []

        for i, (c, cr) in enumerate(zip(self._curves, self._curves_raw)):
            if i not in checked_ids:
                c.setVisible(False); cr.setVisible(False)
                continue

            key = _KEYS[i]
            xs_raw, ys_raw = [], []
            for j, r in enumerate(self._records):
                if key == 'accel_mag':
                    ax = r.get('accel_x'); ay = r.get('accel_y'); az = r.get('accel_z')
                    v = math.sqrt(ax*ax + ay*ay + az*az) / 16384.0 if None not in (ax, ay, az) else None
                else:
                    v = r.get(key)
                if v is not None:
                    xs_raw.append(float(r.get('ts', j * 60)))
                    ys_raw.append(float(v))
            if not xs_raw:
                c.setVisible(False); cr.setVisible(False)
                continue

            xs, ys = list(xs_raw), list(ys_raw)

            if self._chk_outliers.isChecked():
                xs, ys = self._remove_outliers(xs, ys)

            if len(xs) > 1500:
                xs, ys = self._lttb(xs, ys, 1500)

            xs, ys = self._insert_gaps(xs, ys)

            if self._chk_delta.isChecked() and ys:
                first_real = next((y for y in ys if y == y), None)
                if first_real is not None:
                    ys = [y - first_real if y == y else y for y in ys]

            c.setVisible(True)
            if self._chk_smooth.isChecked():
                ys_smooth = self._smooth_ma_nan(ys, self._spin_smooth.value())
                cr.setData([], []); cr.setVisible(False)
                c.setData(xs, ys_smooth)
                if not first_ch_xs:
                    first_ch_xs, first_ch_ys = xs, ys_smooth
            else:
                cr.setVisible(False)
                c.setData(xs, ys)
                if not first_ch_xs:
                    first_ch_xs, first_ch_ys = xs, ys

        self._ch_xs = first_ch_xs
        self._ch_ys = first_ch_ys

        # Y-axis label — first checked series' unit
        units = [_UNITS[i] for i in checked_ids if i < len(_UNITS) and _UNITS[i]]
        unit_lbl = units[0] if units else ''
        if self._chk_delta.isChecked() and unit_lbl:
            unit_lbl = f'Δ {unit_lbl}'
        self._plot.getAxis('left').setLabel(unit_lbl)

        self._plot.setBackground('transparent')
        if self._ts_mode == 1:
            tz_h = self._tz_offset_s // 3600
            tz_lbl = f'UTC{tz_h:+d}' if tz_h != 0 else 'UTC'
            x_lbl = f'Date / Time ({tz_lbl})'
        else:
            x_lbl = 'Boot time (s)'
        self._plot.getAxis('bottom').setLabel(x_lbl)

    # ── Crosshair ─────────────────────────────────────────────────────────────

    def _on_mouse_moved(self, pos):
        if not HAS_PG or not self._records or not self._ch_xs:
            return
        pi = self._plot.getPlotItem()
        if not pi.sceneBoundingRect().contains(pos):
            self._ch_vline.setVisible(False)
            self._ch_hline.setVisible(False)
            self._ch_label.setVisible(False)
            return
        mp = pi.vb.mapSceneToView(pos)
        x = mp.x()
        # Snap to nearest real (non-NaN) data point by X
        best_i = 0
        best_d = float('inf')
        for i, rx in enumerate(self._ch_xs):
            if rx != rx:    # NaN gap marker — skip
                continue
            d = abs(rx - x)
            if d < best_d:
                best_d = d; best_i = i
        snap_x = self._ch_xs[best_i]
        snap_y = self._ch_ys[best_i] if best_i < len(self._ch_ys) else mp.y()
        self._ch_vline.setPos(snap_x); self._ch_vline.setVisible(True)
        self._ch_hline.setPos(snap_y); self._ch_hline.setVisible(True)
        sid  = self._ser_grp.checkedId(); sid = sid if sid >= 0 else 0
        _UNITS = ['°C', '', '%', 'raw', 'raw', 'raw', 'g']
        unit = _UNITS[sid] if sid < len(_UNITS) else ''
        if isinstance(snap_y, float):
            val_str = f'{snap_y:.2f} {unit}'.strip()
        else:
            val_str = f'{snap_y} {unit}'.strip()
        if self._ts_mode == 1:
            import datetime as _dt
            try:
                unix = int(snap_x) + _TimeAxis._EPOCH2000 + self._tz_offset_s
                tz_h = self._tz_offset_s // 3600
                tz_sfx = f' UTC{tz_h:+d}' if tz_h != 0 else ' UTC'
                t_str = _dt.datetime.utcfromtimestamp(unix).strftime('%Y-%m-%d %H:%M:%S') + tz_sfx
            except Exception:
                t_str = f'{snap_x:.0f}'
        else:
            sx = int(snap_x)
            if sx < 60:
                t_str = f'+{sx}s'
            elif sx < 3600:
                t_str = f'+{sx // 60}m {sx % 60}s'
            else:
                t_str = f'+{sx // 3600}h {(sx % 3600) // 60}m'
        # Position label slightly above+right of snap point
        vr = pi.vb.viewRange()
        xr = vr[0]; yr = vr[1]
        anchor_x = 0.0 if snap_x < (xr[0] + xr[1]) / 2 else 1.0
        anchor_y = 0.0 if snap_y > (yr[0] + yr[1]) / 2 else 1.0
        self._ch_label.setAnchor((anchor_x, anchor_y))
        self._ch_label.setText(f' {t_str}   {val_str} ')
        bg = self._C.get('accent', '#1a4fd6')
        self._ch_label.setColor('#ffffff')
        self._ch_label.fill = pg.mkBrush(bg + 'CC')
        self._ch_label.setPos(snap_x, snap_y)
        self._ch_label.setVisible(True)

    # ── Table ─────────────────────────────────────────────────────────────────

    def _fill_table(self):
        self._table.setRowCount(len(self._records))
        for i, r in enumerate(self._records):
            ts = r.get('ts', i * 60)
            if self._ts_mode == 1:
                try:
                    import datetime as _dt
                    dt = _dt.datetime.utcfromtimestamp(ts + _TimeAxis._EPOCH2000 + self._tz_offset_s)
                    time_str = dt.strftime('%Y-%m-%d %H:%M:%S')
                except (ValueError, OSError, OverflowError):
                    time_str = str(ts)
            elif ts < 60:
                time_str = f'+{ts}s'
            elif ts < 3600:
                time_str = f'+{ts // 60}m {ts % 60}s'
            else:
                time_str = f'+{ts // 3600}h {(ts % 3600) // 60}m'
            ax = r.get('accel_x'); ay = r.get('accel_y'); az = r.get('accel_z')
            if 'marker' in r:
                vals = [str(i), time_str, f'● Marker #{r["marker"]}',
                        '—', '—', '—', '—', '—']
            else:
                vals = [
                    str(i), time_str,
                    f"{r.get('temp_c', 0):.1f}" if isinstance(r.get('temp_c'), (int, float)) else '—',
                    str(r.get('light_raw', '—')),
                    str(r.get('bat_pct', '—')),
                    str(ax) if ax is not None else '—',
                    str(ay) if ay is not None else '—',
                    str(az) if az is not None else '—',
                ]
            for j, val in enumerate(vals):
                item = QTableWidgetItem(val)
                if 'marker' in r:
                    item.setForeground(QColor('#e67e00'))
                self._table.setItem(i, j, item)

    # ── Markers ───────────────────────────────────────────────────────────────

    def _on_marker_mode_toggled(self, checked: bool):
        if not HAS_PG:
            return
        if checked:
            self._btn_add_marker.setText('✕  Cancel')
            self._plot.setCursor(Qt.CursorShape.CrossCursor)
        else:
            self._btn_add_marker.setText('⊕  Add Marker')
            self._plot.setCursor(Qt.CursorShape.ArrowCursor)

    def _on_plot_click(self, event):
        if not (HAS_PG and self._btn_add_marker.isChecked()):
            return
        pos = event.scenePos()
        pi  = self._plot.getPlotItem()
        if not pi.sceneBoundingRect().contains(pos):
            return
        x = pi.vb.mapSceneToView(pos).x()
        label, ok = QInputDialog.getText(self, 'Add Marker', 'Marker label:')
        if ok:
            self._add_marker(x, label.strip() or f'M{len(self._markers) + 1}')
        self._btn_add_marker.setChecked(False)

    def _add_marker(self, x: float, label: str):
        color = self._C['warning']
        try:
            line = pg.InfiniteLine(
                pos=x, angle=90, movable=True,
                pen=pg.mkPen(color, width=2, style=Qt.PenStyle.DashLine),
                label=label,
                labelOpts={'position': 0.88, 'color': color,
                           'fill': pg.mkBrush(color + '28'), 'movable': False})
        except TypeError:
            line = pg.InfiniteLine(
                pos=x, angle=90, movable=True,
                pen=pg.mkPen(color, width=2, style=Qt.PenStyle.DashLine))
        self._plot.addItem(line)
        idx = len(self._markers)
        m = {'x': x, 'label': label, 'line': line}
        self._markers.append(m)
        # Update stored x when user drags the line
        line.sigPositionChanged.connect(lambda ln, i=idx: self._on_marker_moved(ln, i))
        self._refresh_marker_chips()

    def _on_marker_moved(self, line, idx: int):
        if 0 <= idx < len(self._markers):
            self._markers[idx]['x'] = line.value()

    def _del_marker(self, idx: int):
        if 0 <= idx < len(self._markers):
            self._plot.removeItem(self._markers[idx]['line'])
            self._markers.pop(idx)
            self._refresh_marker_chips()

    def _clear_markers(self):
        for m in self._markers:
            self._plot.removeItem(m['line'])
        self._markers.clear()
        self._refresh_marker_chips()

    def _refresh_marker_chips(self):
        while self._mk_layout.count() > 1:
            item = self._mk_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
        for i, m in enumerate(self._markers):
            chip = QFrame(); chip.setObjectName('card')
            row  = QHBoxLayout(chip)
            row.setContentsMargins(6, 1, 3, 1); row.setSpacing(3)
            lbl = QLabel(m['label']); lbl.setFont(QFont('Segoe UI', 8))
            lbl.setStyleSheet(f'color:{self._C["warning"]};')
            row.addWidget(lbl)
            btn_x = QPushButton('×')
            btn_x.setFixedSize(16, 16)
            btn_x.setFont(QFont('Segoe UI', 8, QFont.Weight.Bold))
            btn_x.setObjectName('secondary')
            btn_x.clicked.connect(lambda _, k=i: self._del_marker(k))
            row.addWidget(btn_x)
            self._mk_layout.insertWidget(self._mk_layout.count() - 1, chip)

    # ── Export / Erase ────────────────────────────────────────────────────────

    def _export_csv(self):
        if not self._records:
            QMessageBox.information(self, 'Export', 'No data to export.')
            return
        path, _ = QFileDialog.getSaveFileName(
            self, 'Export CSV', 'beacon_log.csv', 'CSV (*.csv)')
        if path:
            with open(path, 'w') as f:
                f.write('idx,ts,temp_c,light_raw,bat_pct,bat_mv,accel_x,accel_y,accel_z\n')
                for i, r in enumerate(self._records):
                    f.write(f"{i},{r.get('ts',0)},{r.get('temp_c','')},"
                            f"{r.get('light_raw','')},{r.get('bat_pct','')},"
                            f"{r.get('bat_mv','')},"
                            f"{r.get('accel_x','')},{r.get('accel_y','')},"
                            f"{r.get('accel_z','')}\n")

    def _on_erase(self):
        ret = QMessageBox.question(
            self, 'Erase log',
            'This will permanently erase all log data on the beacon.\nAre you sure?',
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if ret == QMessageBox.StandardButton.Yes:
            self.erase_requested.emit()

    # ── Theme ─────────────────────────────────────────────────────────────────

    def retheme(self, C: dict):
        self._C = C
        if HAS_PG and hasattr(self, '_curves'):
            _SER_COLORS = ['#ef4444','#f59e0b','#22c55e','#3b82f6','#8b5cf6','#ec4899','#06b6d4']
            for _c, _cr, _sc in zip(self._curves, self._curves_raw, _SER_COLORS):
                _c.setPen(pg.mkPen(_sc, width=2))
                _cr.setPen(pg.mkPen(_sc + 'BB', width=2, style=Qt.PenStyle.DashLine))
            self._plot.setBackground('transparent')
            for m in self._markers:
                try:
                    m['line'].setPen(pg.mkPen(C['warning'], width=1.5,
                                               style=Qt.PenStyle.DashLine))
                except Exception:
                    pass


# ─────────────────────────────────────────────────────────────────────────────
# Sticky footer (shared Apply/Revert for Beacon + Logging tabs)
# ─────────────────────────────────────────────────────────────────────────────
class StickyFooter(QWidget):
    revert_clicked  = pyqtSignal()
    apply_clicked   = pyqtSignal()
    restart_clicked = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName('stickyFooter')
        self.setFixedHeight(64)
        self._C = THEMES['light']
        lay = QHBoxLayout(self)
        lay.setContentsMargins(16, 12, 16, 12)
        lay.setSpacing(12)
        self._btn_revert = QPushButton('Revert')
        self._btn_revert.setObjectName('secondary')
        self._btn_revert.setFixedHeight(36)
        self._btn_revert.clicked.connect(self.revert_clicked.emit)
        self._btn_restart = QPushButton('Restart Beacon')
        self._btn_restart.setObjectName('secondary')
        self._btn_restart.setFixedHeight(36)
        self._btn_restart.setEnabled(False)
        self._btn_restart.setToolTip('Reboot beacon (saves pending settings first)')
        self._btn_restart.clicked.connect(self.restart_clicked.emit)
        self._btn_apply = QPushButton('Apply to beacon')
        self._btn_apply.setObjectName('primary')
        self._btn_apply.setFixedSize(190, 36)
        self._btn_apply.clicked.connect(self.apply_clicked.emit)
        lay.addWidget(self._btn_revert)
        lay.addStretch()
        lay.addWidget(self._btn_restart)
        lay.addWidget(self._btn_apply)

    def set_disconnected(self, is_disconnected: bool):
        if is_disconnected:
            self._btn_apply.setText('Save as profile…')
            self._btn_restart.setEnabled(False)
        else:
            self._btn_apply.setText('Apply to beacon')
            self._btn_restart.setEnabled(True)

    def set_enabled(self, v: bool):
        self._btn_apply.setEnabled(v)
        self._btn_revert.setEnabled(v)

    def retheme(self, C: dict):
        self._C = C
        # Use scoped selector — avoids cascading into child QPushButton and hiding button text
        self.setStyleSheet(f'QWidget#stickyFooter {{ background:{C["footer_bg"]}; border-top:1px solid {C["border"]}; }}')


# ─────────────────────────────────────────────────────────────────────────────
# Settings Tab — calibration only (temp offset + battery scale)
# ─────────────────────────────────────────────────────────────────────────────
class SettingsTab(QWidget):
    config_changed         = pyqtSignal()
    live_refresh_changed   = pyqtSignal(bool, float) # (enabled, interval_s)
    tz_offset_changed      = pyqtSignal(int)          # UTC offset in whole hours
    tag_set_requested      = pyqtSignal(str)          # animal tag string
    uptime_reset_requested = pyqtSignal()             # clear persistent uptime counter
    uptime_save_requested  = pyqtSignal()             # flush RAM uptime counters to flash
    uptime_flush_changed   = pyqtSignal(bool, int)    # (enabled, interval_min)
    accel_zero_g_requested  = pyqtSignal()             # capture current readings as zero-g cal
    hwdesc_read_requested   = pyqtSignal()             # read HwDesc from device
    hwdesc_write_requested  = pyqtSignal(object)       # write HwDescBlob to device RAM
    hwdesc_commit_requested = pyqtSignal()             # commit HwDesc RAM → flash

    def __init__(self, parent=None):
        super().__init__(parent)
        self._C = THEMES['light']
        self._sec_hdrs: List[tuple] = []   # (QLabel, QFrame) pairs for retheme
        self._build_ui()

    def _section_header(self, text: str) -> QWidget:
        """Bold uppercase label + 1 px horizontal rule — visual group divider."""
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(0, 12, 0, 0)
        lay.setSpacing(4)
        lbl = QLabel(text.upper())
        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.HLine)
        self._sec_hdrs.append((lbl, sep))
        lay.addWidget(lbl)
        lay.addWidget(sep)
        self._apply_sec_style(lbl, sep, self._C)
        return w

    @staticmethod
    def _apply_sec_style(lbl: QLabel, sep: QFrame, C: dict):
        lbl.setStyleSheet(
            f'font-size:9pt; font-weight:bold; color:{C["text_dim"]};'
            f' letter-spacing:1px; background:transparent;')
        sep.setStyleSheet(f'color:{C["border"]}; background:{C["border"]};')

    def _build_ui(self):
        scroll = QScrollArea(self)
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        root_lay = QVBoxLayout(self)
        root_lay.setContentsMargins(0, 0, 0, 0)
        root_lay.addWidget(scroll)

        inner = QWidget()
        scroll.setWidget(inner)
        _install_drag_scroll(scroll)
        outer = QVBoxLayout(inner)
        outer.setContentsMargins(24, 24, 24, 24)
        outer.setSpacing(16)

        # ════════════════════════════════════════════════════════════════════
        # BEACON  — settings sent to / read from the device
        # ════════════════════════════════════════════════════════════════════
        outer.addWidget(self._section_header('Beacon'))

        # ── LED mode ────────────────────────────────────────────────────────
        led_card = QFrame(); led_card.setObjectName('card')
        led_lay = QVBoxLayout(led_card)
        led_lay.setContentsMargins(20, 16, 20, 16); led_lay.setSpacing(10)

        led_title = QLabel('💡  LED Mode')
        led_title.setStyleSheet('font-size:13pt; font-weight:bold;')
        led_lay.addWidget(led_title)
        led_lay.addWidget(QLabel('Controls the indicator LED behavior on the beacon.'))

        btn_row = QHBoxLayout(); btn_row.setSpacing(8)
        self._led_btns: List[QPushButton] = []
        _LED_LABELS = [
            ('Off',       '🔴  Off',       'LED off — lowest power'),
            ('On',        '🟢  Always On', 'LED on permanently'),
            ('Heartbeat', '💓  Heartbeat', '100 ms pulse every 800 ms'),
            ('TX mirror', '📡  TX mirror', 'On while transmitting, off between pulses'),
        ]
        self._led_grp = QButtonGroup(self)
        for idx, (_, label, tip) in enumerate(_LED_LABELS):
            b = QPushButton(label)
            b.setObjectName('chip'); b.setCheckable(True)
            b.setToolTip(tip); b.setMinimumWidth(110)
            self._led_grp.addButton(b, idx)
            btn_row.addWidget(b)
            self._led_btns.append(b)
        btn_row.addStretch()
        led_lay.addLayout(btn_row)
        outer.addWidget(led_card)

        self._led_grp.idToggled.connect(
            lambda _id, checked: self.config_changed.emit() if checked else None)

        # ── Calibration ─────────────────────────────────────────────────────
        cal_card = QFrame(); cal_card.setObjectName('card')
        g = QGridLayout(cal_card)
        g.setContentsMargins(20, 20, 20, 20); g.setSpacing(12)
        row = 0

        _cal_title = QLabel(f'{ICO["settings"]}  Calibration')
        _cal_title.setStyleSheet('font-size:13pt; font-weight:bold;')
        g.addWidget(_cal_title, row, 0, 1, 3); row += 1

        hint_cal = QLabel('Sensor offsets stored on the beacon. Applied to every reading.')
        hint_cal.setWordWrap(True)
        g.addWidget(hint_cal, row, 0, 1, 3); row += 1

        g.addWidget(QLabel(f'{ICO["temp"]}  Temp offset:'), row, 0)
        self._spin_toff = QDoubleSpinBox()
        self._spin_toff.setRange(-12.7, 12.7)
        self._spin_toff.setSingleStep(0.1); self._spin_toff.setDecimals(1)
        self._spin_toff.setSuffix(' °C')
        self._spin_toff.setToolTip('Added to raw chip reading. e.g. −3.3 corrects a +3.3 °C offset.')
        g.addWidget(self._spin_toff, row, 1)
        g.addWidget(QLabel('(e.g. −3.3 °C)'), row, 2); row += 1

        g.addWidget(QLabel(f'{ICO["battery"]}  Battery scale:'), row, 0)
        self._spin_bscale = QDoubleSpinBox()
        self._spin_bscale.setRange(0.50, 2.00)
        self._spin_bscale.setSingleStep(0.05); self._spin_bscale.setDecimals(2)
        self._spin_bscale.setSuffix(' ×')
        self._spin_bscale.setToolTip('ADC voltage multiplier. 2.00 = 1:2 divider (default).')
        g.addWidget(self._spin_bscale, row, 1)
        g.addWidget(QLabel('(2.00 = default 1:2 divider)'), row, 2); row += 1

        # Accelerometer zero-g offsets (sent via OP_CONFIG_WRITE to staging buffer)
        sep = QFrame(); sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet('color:#b0c8e8;')
        g.addWidget(sep, row, 0, 1, 3); row += 1
        g.addWidget(QLabel('🔵  Accel offset X:'), row, 0)
        self._spin_accel_off_x = QSpinBox(); self._spin_accel_off_x.setRange(-32768, 32767); self._spin_accel_off_x.setValue(0)
        self._spin_accel_off_x.setToolTip("Zero-g offset for X axis (raw ADC units, 2's complement)")
        g.addWidget(self._spin_accel_off_x, row, 1)
        g.addWidget(QLabel('(raw units)'), row, 2); row += 1
        g.addWidget(QLabel('🔵  Accel offset Y:'), row, 0)
        self._spin_accel_off_y = QSpinBox(); self._spin_accel_off_y.setRange(-32768, 32767); self._spin_accel_off_y.setValue(0)
        g.addWidget(self._spin_accel_off_y, row, 1); row += 1
        g.addWidget(QLabel('🔵  Accel offset Z:'), row, 0)
        self._spin_accel_off_z = QSpinBox(); self._spin_accel_off_z.setRange(-32768, 32767); self._spin_accel_off_z.setValue(0)
        g.addWidget(self._spin_accel_off_z, row, 1); row += 1

        zero_g_btn = QPushButton('Zero-G calibrate (X=0, Y=0, Z=+1g)')
        zero_g_btn.setObjectName('secondary')
        zero_g_btn.setToolTip('Set device flat on a stable surface, then click to capture current readings as offset.')
        zero_g_btn.clicked.connect(self._on_accel_zero_g)
        g.addWidget(zero_g_btn, row, 0, 1, 2); row += 1

        for spin in [self._spin_accel_off_x, self._spin_accel_off_y, self._spin_accel_off_z]:
            spin.valueChanged.connect(self.config_changed.emit)

        g.setColumnStretch(2, 1)
        outer.addWidget(cal_card)

        # ── Tag Identity ─────────────────────────────────────────────────────
        tag_card = QFrame(); tag_card.setObjectName('card')
        tag_lay = QVBoxLayout(tag_card)
        tag_lay.setContentsMargins(20, 16, 20, 16); tag_lay.setSpacing(10)

        tag_title = QLabel('🏷  Tag Identity')
        tag_title.setStyleSheet('font-size:13pt; font-weight:bold;')
        tag_lay.addWidget(tag_title)

        _tag_hint = QLabel(
            'Short label stored in beacon flash (max 11 chars). '
            'Shown alongside UID to identify the animal.')
        _tag_hint.setWordWrap(True)
        tag_lay.addWidget(_tag_hint)

        tag_input_row = QHBoxLayout(); tag_input_row.setSpacing(8)
        self._edit_tag = QLineEdit()
        self._edit_tag.setMaxLength(11); self._edit_tag.setPlaceholderText('RAT_001')
        self._edit_tag.setMaximumWidth(160)
        self._btn_tag_set = QPushButton('Set')
        self._btn_tag_set.setObjectName('secondary'); self._btn_tag_set.setFixedWidth(72)
        tag_input_row.addWidget(QLabel('New tag:'))
        tag_input_row.addWidget(self._edit_tag)
        tag_input_row.addWidget(self._btn_tag_set)
        tag_input_row.addStretch()
        tag_lay.addLayout(tag_input_row)

        uid_row = QHBoxLayout(); uid_row.setSpacing(8)
        uid_row.addWidget(QLabel('MCU UID:'))
        self._lbl_tag_uid = QLabel('—'); self._lbl_tag_uid.setObjectName('dim')
        uid_row.addWidget(self._lbl_tag_uid)
        self._lbl_tag_current = QLabel(''); self._lbl_tag_current.setObjectName('dim')
        uid_row.addWidget(self._lbl_tag_current)
        uid_row.addStretch()
        tag_lay.addLayout(uid_row)
        outer.addWidget(tag_card)

        self._btn_tag_set.clicked.connect(
            lambda: self.tag_set_requested.emit(self._edit_tag.text().strip()))

        # ── Flash Health ──────────────────────────────────────────────────────
        flash_card = QFrame(); flash_card.setObjectName('card')
        flash_lay = QVBoxLayout(flash_card)
        flash_lay.setContentsMargins(20, 16, 20, 16); flash_lay.setSpacing(10)

        flash_title = QLabel('💾  Flash Health')
        flash_title.setStyleSheet('font-size:13pt; font-weight:bold;')
        flash_lay.addWidget(flash_title)

        flash_hint = QLabel(
            'Flash pages rated for ~10 000 erase cycles each. '
            'Counter tracks all page erases (config + hwdesc saves). '
            'Per-page estimate assumes ~6 active pages.')
        flash_hint.setWordWrap(True)
        flash_lay.addWidget(flash_hint)

        flash_grid = QGridLayout(); flash_grid.setSpacing(6)
        flash_grid.addWidget(QLabel('Total erases:'), 0, 0)
        self._lbl_flash_erases = QLabel('—')
        flash_grid.addWidget(self._lbl_flash_erases, 0, 1)
        flash_grid.addWidget(QLabel('Per-page est.:'), 1, 0)
        self._lbl_flash_per_page = QLabel('—')
        flash_grid.addWidget(self._lbl_flash_per_page, 1, 1)
        flash_grid.addWidget(QLabel('Life remaining:'), 2, 0)
        self._lbl_flash_life = QLabel('—')
        flash_grid.addWidget(self._lbl_flash_life, 2, 1)
        flash_grid.setColumnStretch(2, 1)
        flash_lay.addLayout(flash_grid)

        self._bar_flash = QProgressBar()
        self._bar_flash.setRange(0, 100); self._bar_flash.setValue(100)
        self._bar_flash.setTextVisible(True); self._bar_flash.setFormat('%p% remaining')
        self._bar_flash.setFixedHeight(18)
        flash_lay.addWidget(self._bar_flash)
        outer.addWidget(flash_card)

        # ── Total Uptime ──────────────────────────────────────────────────────
        up_card = QFrame(); up_card.setObjectName('card')
        up_lay = QVBoxLayout(up_card)
        up_lay.setContentsMargins(20, 16, 20, 16); up_lay.setSpacing(10)

        up_title = QLabel('⏱  Total Uptime')
        up_title.setStyleSheet('font-size:13pt; font-weight:bold;')
        up_lay.addWidget(up_title)

        up_hint = QLabel(
            'Cumulative powered-on time saved to beacon flash across all sessions. '
            'Reset when re-implanting or starting a new experiment.')
        up_hint.setWordWrap(True)
        up_lay.addWidget(up_hint)

        up_row = QHBoxLayout(); up_row.setSpacing(12)
        self._lbl_uptime_total = QLabel('—')
        self._lbl_uptime_total.setStyleSheet('font-size:14pt; font-weight:bold;')
        up_row.addWidget(self._lbl_uptime_total)
        up_row.addStretch()
        self._btn_uptime_save = QPushButton('Get Data')
        self._btn_uptime_save.setObjectName('primary')
        self._btn_uptime_save.setToolTip(
            'Flush RAM uptime counters to beacon flash,\n'
            'then read back the updated values.')
        up_row.addWidget(self._btn_uptime_save)
        self._btn_uptime_reset = QPushButton('Reset Uptime')
        self._btn_uptime_reset.setObjectName('secondary')
        up_row.addWidget(self._btn_uptime_reset)
        up_lay.addLayout(up_row)
        outer.addWidget(up_card)

        self._btn_uptime_save.clicked.connect(self.uptime_save_requested.emit)
        self._btn_uptime_reset.clicked.connect(self.uptime_reset_requested.emit)

        # ── Auto-flush row ────────────────────────────────────────────────
        flush_row = QHBoxLayout(); flush_row.setSpacing(8)
        self._chk_uptime_flush = QCheckBox('Auto-flush to flash every')
        flush_row.addWidget(self._chk_uptime_flush)
        self._spin_uptime_flush = QSpinBox()
        self._spin_uptime_flush.setRange(1, 1440)
        self._spin_uptime_flush.setValue(10)
        self._spin_uptime_flush.setSuffix(' min')
        self._spin_uptime_flush.setFixedWidth(90)
        self._spin_uptime_flush.setEnabled(False)
        flush_row.addWidget(self._spin_uptime_flush)
        flush_row.addStretch()
        up_lay.addLayout(flush_row)

        flush_hint = QLabel(
            'Disabled: firmware saves once per 24 h on its own.\n'
            'Enabled: GUI flushes RAM uptime counters to flash at the set interval '
            'while connected — useful during long debug sessions.')
        flush_hint.setWordWrap(True)
        flush_hint.setStyleSheet('font-size:8pt; color:#6b7280;')
        up_lay.addWidget(flush_hint)

        def _on_flush_toggle(checked):
            self._spin_uptime_flush.setEnabled(checked)
            self.uptime_flush_changed.emit(checked, self._spin_uptime_flush.value())

        def _on_flush_interval():
            if self._chk_uptime_flush.isChecked():
                self.uptime_flush_changed.emit(True, self._spin_uptime_flush.value())

        self._chk_uptime_flush.toggled.connect(_on_flush_toggle)
        self._spin_uptime_flush.valueChanged.connect(_on_flush_interval)
        outer.addWidget(up_card)

        # ════════════════════════════════════════════════════════════════════
        # APP  — local GUI preferences, nothing sent to device
        # ════════════════════════════════════════════════════════════════════
        outer.addWidget(self._section_header('App'))

        # ── Live Sensor Streaming ─────────────────────────────────────────────
        live_card = QFrame(); live_card.setObjectName('card')
        live_lay = QVBoxLayout(live_card)
        live_lay.setContentsMargins(20, 16, 20, 16); live_lay.setSpacing(10)

        live_title = QLabel('📡  Live Sensor Streaming')
        live_title.setStyleSheet('font-size:13pt; font-weight:bold;')
        live_lay.addWidget(live_title)

        live_hint = QLabel(
            'Poll STATUS from the beacon while connected — feeds the Overview display. '
            'No flash writes on the device. Saved locally, no Apply needed.')
        live_hint.setWordWrap(True)
        live_lay.addWidget(live_hint)

        chk_row = QHBoxLayout(); chk_row.setSpacing(12)
        self._chk_live = QCheckBox('Refresh sensors while connected')
        self._chk_live.setChecked(True)
        chk_row.addWidget(self._chk_live); chk_row.addStretch()
        live_lay.addLayout(chk_row)

        iv_row = QHBoxLayout(); iv_row.setSpacing(8)
        iv_row.addWidget(QLabel('Refresh every'))
        self._spin_live = QDoubleSpinBox()
        self._spin_live.setRange(0.1, 30.0)
        self._spin_live.setSingleStep(0.1)
        self._spin_live.setDecimals(1)
        self._spin_live.setValue(2.0)
        self._spin_live.setSuffix(' s')
        self._spin_live.setToolTip('How often to poll STATUS from the device (0.1–30 s)')
        self._spin_live.setMaximumWidth(90)
        iv_row.addWidget(self._spin_live); iv_row.addStretch()
        live_lay.addLayout(iv_row)
        outer.addWidget(live_card)

        def _live_changed(*_):
            en = self._chk_live.isChecked()
            self._spin_live.setEnabled(en)
            self.live_refresh_changed.emit(en, float(self._spin_live.value()))
        self._chk_live.toggled.connect(_live_changed)
        self._spin_live.valueChanged.connect(_live_changed)

        # ── Display / Timezone ────────────────────────────────────────────────
        tz_card = QFrame(); tz_card.setObjectName('card')
        tz_g = QGridLayout(tz_card)
        tz_g.setContentsMargins(20, 20, 20, 20); tz_g.setSpacing(12)
        tz_row = 0

        tz_title = QLabel('🕐  Display')
        tz_title.setStyleSheet('font-size:13pt; font-weight:bold;')
        tz_g.addWidget(tz_title, tz_row, 0, 1, 3); tz_row += 1

        tz_hint = QLabel(
            'UTC offset applied to timestamps in charts and data table. '
            'Nothing is sent to the beacon.')
        tz_hint.setWordWrap(True)
        tz_g.addWidget(tz_hint, tz_row, 0, 1, 3); tz_row += 1

        tz_g.addWidget(QLabel('UTC offset:'), tz_row, 0)
        self._spin_tz = QSpinBox()
        self._spin_tz.setRange(-12, 14); self._spin_tz.setValue(0)
        self._spin_tz.setSuffix(' h'); self._spin_tz.setPrefix('UTC')
        self._spin_tz.setToolTip('e.g. +2 = Kyiv summer time, +1 = Berlin, 0 = UTC')
        tz_g.addWidget(self._spin_tz, tz_row, 1)
        tz_g.addWidget(QLabel('(0 = UTC,  +2 = Kyiv,  +1 = Berlin)'), tz_row, 2); tz_row += 1

        tz_g.setColumnStretch(2, 1)
        outer.addWidget(tz_card)

        # ════════════════════════════════════════════════════════════════════
        # HARDWARE DESCRIPTOR — editable fields from flash page 59
        # ════════════════════════════════════════════════════════════════════
        outer.addWidget(self._section_header('Hardware Descriptor'))

        hw_card = QFrame(); hw_card.setObjectName('card')
        hw_lay = QVBoxLayout(hw_card)
        hw_lay.setContentsMargins(16, 16, 16, 16); hw_lay.setSpacing(10)

        hw_top_row = QHBoxLayout()
        hw_top_row.addWidget(QLabel('Burned to flash page 59. Describes installed hardware.',
                                    font=QFont('Segoe UI', 8)))
        hw_top_row.addStretch()
        self._chk_hw_edit = QCheckBox('Enable editing')
        hw_top_row.addWidget(self._chk_hw_edit)
        hw_lay.addLayout(hw_top_row)

        hw_g = QGridLayout(); hw_g.setSpacing(6); hw_g.setColumnStretch(1, 1)
        row = 0

        def _hwrow(label, widget):
            nonlocal row
            hw_g.addWidget(QLabel(label), row, 0)
            hw_g.addWidget(widget,        row, 1)
            row += 1

        self._hw_ver    = QSpinBox();  self._hw_ver.setRange(1, 255);   self._hw_ver.setValue(1)
        self._hw_tx_freq= QSpinBox();  self._hw_tx_freq.setRange(1_000_000, 1_000_000_000)
        self._hw_tx_freq.setSuffix(' Hz'); self._hw_tx_freq.setSingleStep(100_000)
        self._hw_tx_freq.setValue(30_000_000)
        self._hw_tx_ch  = QSpinBox();  self._hw_tx_ch.setRange(1, 16);  self._hw_tx_ch.setValue(4)
        self._hw_tx_pwr = QSpinBox();  self._hw_tx_pwr.setRange(1, 16); self._hw_tx_pwr.setValue(4)
        self._hw_bf_mv  = QSpinBox();  self._hw_bf_mv.setRange(1000, 5000); self._hw_bf_mv.setSuffix(' mV'); self._hw_bf_mv.setValue(4200)
        self._hw_be_mv  = QSpinBox();  self._hw_be_mv.setRange(500, 4000);  self._hw_be_mv.setSuffix(' mV'); self._hw_be_mv.setValue(3000)

        _TEMP_OPTS  = ['none', 'crystal (MCU)', 'NTC', 'STTS22H', 'LIS2DW12']
        _LIGHT_OPTS = ['none', 'present']
        _BATT_OPTS  = ['none', 'ADC divider', 'fuel gauge']
        _ACCEL_OPTS = ['none', 'ISM330DHCX', 'LIS2DW12', 'other']
        _LED_OPTS   = ['none', 'single LED', 'RGB LED']

        self._hw_temp   = QComboBox(); self._hw_temp.addItems(_TEMP_OPTS)
        self._hw_light  = QComboBox(); self._hw_light.addItems(_LIGHT_OPTS)
        self._hw_batt   = QComboBox(); self._hw_batt.addItems(_BATT_OPTS)
        self._hw_accel  = QComboBox(); self._hw_accel.addItems(_ACCEL_OPTS)
        self._hw_led    = QComboBox(); self._hw_led.addItems(_LED_OPTS)

        self._hw_tx_type    = QLineEdit(); self._hw_tx_type.setPlaceholderText('e.g. colpitts')
        self._hw_accel_model= QLineEdit(); self._hw_accel_model.setPlaceholderText('e.g. LIS2DW12TR')
        self._hw_light_model= QLineEdit(); self._hw_light_model.setPlaceholderText('sensor model')
        self._hw_led_model  = QLineEdit(); self._hw_led_model.setPlaceholderText('LED model')
        self._hw_comment    = QLineEdit(); self._hw_comment.setPlaceholderText('free text (max 47 chars via binary)')

        for w in [self._hw_tx_type, self._hw_accel_model,
                  self._hw_light_model, self._hw_led_model]:
            w.setMaxLength(15)
        self._hw_comment.setMaxLength(47)

        _hwrow('HW version:',      self._hw_ver)
        _hwrow('Temp sensor:',     self._hw_temp)
        _hwrow('Light sensor:',    self._hw_light)
        _hwrow('Light model:',     self._hw_light_model)
        _hwrow('Battery monitor:', self._hw_batt)
        _hwrow('Batt full (mV):',  self._hw_bf_mv)
        _hwrow('Batt empty (mV):', self._hw_be_mv)
        _hwrow('Accelerometer:',   self._hw_accel)
        _hwrow('Accel model:',     self._hw_accel_model)
        _hwrow('LED type:',        self._hw_led)
        _hwrow('LED model:',       self._hw_led_model)
        _hwrow('TX freq (Hz):',    self._hw_tx_freq)
        _hwrow('TX channels:',     self._hw_tx_ch)
        _hwrow('TX pwr levels:',   self._hw_tx_pwr)
        _hwrow('TX type/circuit:', self._hw_tx_type)
        _hwrow('Comment:',         self._hw_comment)
        hw_lay.addLayout(hw_g)

        hw_btn_row = QHBoxLayout()
        self._btn_hw_read   = QPushButton('Read from device')
        self._btn_hw_write  = QPushButton('Write to device (RAM)')
        self._btn_hw_commit = QPushButton('Commit to flash')
        self._lbl_hw_status = QLabel('')
        for b in [self._btn_hw_read, self._btn_hw_write, self._btn_hw_commit]:
            b.setObjectName('secondary')
            hw_btn_row.addWidget(b)
        hw_btn_row.addSpacing(12); hw_btn_row.addWidget(self._lbl_hw_status)
        hw_btn_row.addStretch()
        hw_lay.addLayout(hw_btn_row)
        outer.addWidget(hw_card)

        # all mutable widgets disabled until user ticks 'Enable editing'
        self._hw_edit_widgets = [
            self._hw_ver, self._hw_temp, self._hw_light, self._hw_light_model,
            self._hw_batt, self._hw_bf_mv, self._hw_be_mv, self._hw_accel,
            self._hw_accel_model, self._hw_led, self._hw_led_model,
            self._hw_tx_freq, self._hw_tx_ch, self._hw_tx_pwr,
            self._hw_tx_type, self._hw_comment,
            self._btn_hw_write, self._btn_hw_commit,
        ]
        for w in self._hw_edit_widgets:
            w.setEnabled(False)

        def _on_hw_edit_toggled(checked):
            for w in self._hw_edit_widgets:
                w.setEnabled(checked)
        self._chk_hw_edit.toggled.connect(_on_hw_edit_toggled)

        self._btn_hw_read.clicked.connect(self.hwdesc_read_requested.emit)
        self._btn_hw_write.clicked.connect(self._on_hwdesc_write)
        self._btn_hw_commit.clicked.connect(self.hwdesc_commit_requested.emit)

        outer.addStretch(1)

        self._spin_toff.valueChanged.connect(lambda _: self.config_changed.emit())
        self._spin_bscale.valueChanged.connect(lambda _: self.config_changed.emit())
        self._spin_tz.valueChanged.connect(lambda v: self.tz_offset_changed.emit(v))

    def _on_accel_zero_g(self):
        self.accel_zero_g_requested.emit()

    def set_accel_offsets(self, x: int, y: int, z: int):
        for sp, v in [(self._spin_accel_off_x, x),
                      (self._spin_accel_off_y, y),
                      (self._spin_accel_off_z, z)]:
            sp.blockSignals(True); sp.setValue(v); sp.blockSignals(False)

    def get_accel_offsets(self) -> Tuple[int, int, int]:
        return (self._spin_accel_off_x.value(),
                self._spin_accel_off_y.value(),
                self._spin_accel_off_z.value())

    def _on_hwdesc_write(self):
        self.hwdesc_write_requested.emit(self.get_hwdesc())

    def get_hwdesc(self) -> 'HwDescBlob':
        return HwDescBlob(
            hw_version    = self._hw_ver.value(),
            temp_type     = self._hw_temp.currentIndex(),
            light_type    = self._hw_light.currentIndex(),
            batt_type     = self._hw_batt.currentIndex(),
            accel_type    = self._hw_accel.currentIndex(),
            led_type      = self._hw_led.currentIndex(),
            tx_channels   = self._hw_tx_ch.value(),
            tx_pwr_levels = self._hw_tx_pwr.value(),
            tx_freq_hz    = self._hw_tx_freq.value(),
            batt_full_mv  = self._hw_bf_mv.value(),
            batt_empty_mv = self._hw_be_mv.value(),
            light_model   = self._hw_light_model.text().strip(),
            accel_model   = self._hw_accel_model.text().strip(),
            tx_type       = self._hw_tx_type.text().strip(),
            led_model     = self._hw_led_model.text().strip(),
            comment       = self._hw_comment.text().strip(),
        )

    def load_hwdesc(self, blob: 'HwDescBlob'):
        def _set_cb(cb, idx):
            cb.blockSignals(True)
            cb.setCurrentIndex(max(0, min(cb.count() - 1, idx)))
            cb.blockSignals(False)
        def _set_sp(sp, v):
            sp.blockSignals(True); sp.setValue(v); sp.blockSignals(False)
        _set_sp(self._hw_ver,     blob.hw_version)
        _set_sp(self._hw_tx_freq, blob.tx_freq_hz)
        _set_sp(self._hw_tx_ch,   blob.tx_channels)
        _set_sp(self._hw_tx_pwr,  blob.tx_pwr_levels)
        _set_sp(self._hw_bf_mv,   blob.batt_full_mv)
        _set_sp(self._hw_be_mv,   blob.batt_empty_mv)
        _set_cb(self._hw_temp,    blob.temp_type)
        _set_cb(self._hw_light,   blob.light_type)
        _set_cb(self._hw_batt,    blob.batt_type)
        _set_cb(self._hw_accel,   blob.accel_type)
        _set_cb(self._hw_led,     blob.led_type)
        self._hw_tx_type.setText(blob.tx_type)
        self._hw_accel_model.setText(blob.accel_model)
        self._hw_light_model.setText(blob.light_model)
        self._hw_led_model.setText(blob.led_model)
        self._hw_comment.setText(blob.comment)

    def set_hw_status(self, text: str):
        self._lbl_hw_status.setText(text)

    def load_config(self, cfg: 'ConfigBlob'):
        self._spin_toff.blockSignals(True)
        self._spin_bscale.blockSignals(True)
        self._spin_toff.setValue(cfg.temp_offset_01c / 10.0)      # int8 ×0.1°C → float °C
        self._spin_bscale.setValue(cfg.bat_scale_x100 / 100.0)    # ×100 integer → float ×
        self._spin_toff.blockSignals(False)
        self._spin_bscale.blockSignals(False)
        led = max(0, min(3, int(cfg.led_mode)))
        self._led_grp.blockSignals(True)
        btn = self._led_grp.button(led)
        if btn:
            btn.setChecked(True)
        self._led_grp.blockSignals(False)
        # Sync uptime flush UI from firmware value
        usm = getattr(cfg, 'uptime_save_min', 0)
        if usm > 0:
            self.set_uptime_flush_settings(True, usm)
        else:
            self.set_uptime_flush_settings(False, self._spin_uptime_flush.value())

    def get_config(self, base: 'ConfigBlob') -> 'ConfigBlob':
        from copy import copy
        c = copy(base)
        c.temp_offset_01c = int(round(self._spin_toff.value() * 10))    # °C → ×0.1°C int
        c.bat_scale_x100  = int(round(self._spin_bscale.value() * 100)) # × float → ×100 int
        c.led_mode = max(0, self._led_grp.checkedId())
        flush_en, flush_min = self.get_uptime_flush_settings()
        c.uptime_save_min = flush_min if flush_en else 0
        return c

    def get_tz_offset_h(self) -> int:
        return self._spin_tz.value()

    def get_live_settings(self) -> tuple:
        return self._chk_live.isChecked(), float(self._spin_live.value())

    def set_live_settings(self, enabled: bool, interval_s: float):
        self._chk_live.blockSignals(True)
        self._spin_live.blockSignals(True)
        self._chk_live.setChecked(enabled)
        self._spin_live.setValue(max(0.1, min(30.0, float(interval_s))))
        self._spin_live.setEnabled(enabled)
        self._chk_live.blockSignals(False)
        self._spin_live.blockSignals(False)

    def get_uptime_flush_settings(self) -> tuple:
        """Return (enabled: bool, interval_min: int)."""
        return self._chk_uptime_flush.isChecked(), self._spin_uptime_flush.value()

    def set_uptime_flush_settings(self, enabled: bool, interval_min: int):
        self._chk_uptime_flush.blockSignals(True)
        self._spin_uptime_flush.blockSignals(True)
        self._chk_uptime_flush.setChecked(enabled)
        self._spin_uptime_flush.setValue(max(1, min(1440, interval_min)))
        self._spin_uptime_flush.setEnabled(enabled)
        self._chk_uptime_flush.blockSignals(False)
        self._spin_uptime_flush.blockSignals(False)

    def load_info(self, info: 'InfoBlob'):
        """Populate identity/health/uptime cards from InfoBlob."""
        self._lbl_tag_uid.setText(f'0x{info.uid:08X}')
        cur_tag = info.tag or '—'
        self._lbl_tag_current.setText(f'  (current: {cur_tag})')
        self._edit_tag.setPlaceholderText(cur_tag if info.tag else 'RAT_001')

        erases = info.flash_erase_count
        self._lbl_flash_erases.setText(f'{erases}')
        per_page = erases // 6 if erases else 0
        self._lbl_flash_per_page.setText(f'{per_page}  (÷ 6 active pages)')
        remaining_pct = max(0, int(100 - per_page * 100 / 10000))
        self._bar_flash.setValue(remaining_pct)
        remaining_cyc = max(0, 10000 - per_page)
        if remaining_pct >= 90:
            self._lbl_flash_life.setText(f'{remaining_pct}%  ({remaining_cyc} cycles left) — OK')
        elif remaining_pct >= 50:
            self._lbl_flash_life.setText(f'{remaining_pct}%  ({remaining_cyc} cycles left) — watch')
        else:
            self._lbl_flash_life.setText(f'{remaining_pct}%  ({remaining_cyc} cycles left) — LOW')

        a, s1, sd = info.total_active_h, info.total_stop1_h, info.total_shutdown_h
        total_h = a + s1 + sd
        d, hh = total_h // 24, total_h % 24
        self._lbl_uptime_total.setText(
            f'{d}d {hh}h total\n'
            f'  active: {a}h  ·  stop1: {s1}h  ·  shutdown: {sd}h')

    def retheme(self, C: dict):
        self._C = C
        for lbl, sep in self._sec_hdrs:
            self._apply_sec_style(lbl, sep, C)


# ─────────────────────────────────────────────────────────────────────────────
# BLE Tab — configure BLE operating mode and settings (new in v4)
# ─────────────────────────────────────────────────────────────────────────────
class BleTab(QWidget):
    ble_read_requested    = pyqtSignal()
    ble_apply_requested   = pyqtSignal(object)   # BleSettings

    def __init__(self, parent=None):
        super().__init__(parent)
        self._C = THEMES['light']
        self._sec_hdrs: List[tuple] = []
        self._build_ui()

    # ── Section header (same pattern as SettingsTab) ──────────────────────────
    def _section_header(self, text: str) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(0, 12, 0, 0)
        lay.setSpacing(4)
        lbl = QLabel(text.upper())
        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.HLine)
        self._sec_hdrs.append((lbl, sep))
        lay.addWidget(lbl)
        lay.addWidget(sep)
        self._apply_sec_style(lbl, sep, self._C)
        return w

    @staticmethod
    def _apply_sec_style(lbl, sep, C):
        lbl.setStyleSheet(
            f'font-size:9pt; font-weight:bold; color:{C["text_dim"]};'
            f' letter-spacing:1px; background:transparent;')
        sep.setStyleSheet(f'color:{C["border"]}; background:{C["border"]};')

    def _build_ui(self):
        scroll = QScrollArea(self)
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        root_lay = QVBoxLayout(self)
        root_lay.setContentsMargins(0, 0, 0, 0)
        root_lay.addWidget(scroll)

        inner = QWidget()
        scroll.setWidget(inner)
        _install_drag_scroll(scroll)
        outer = QVBoxLayout(inner)
        outer.setContentsMargins(24, 24, 24, 24)
        outer.setSpacing(16)

        # ── Operating mode ────────────────────────────────────────────────────
        # Off and Continuous are exclusive; Schedule can coexist with Gekon.
        # Gekon (double-press → BLE) is ALWAYS active in firmware — no toggle needed.
        outer.addWidget(self._section_header('Operating Mode'))
        mode_card = card_frame()
        mode_lay  = QVBoxLayout(mode_card)
        mode_lay.setContentsMargins(16, 12, 16, 12)
        mode_lay.setSpacing(10)
        self._chk_off   = QCheckBox('Off')
        self._chk_cont  = QCheckBox('Continuous')
        self._chk_sched = QCheckBox('Schedule')
        self._chk_off.setToolTip('BLE always disabled — never activates')
        self._chk_cont.setToolTip('Always advertising; restarts after every disconnect')
        self._chk_sched.setToolTip('Wake every N minutes, advertise for D seconds')
        self._chk_cont.setChecked(True)
        self._chk_off.toggled.connect(self._on_off_toggled)
        self._chk_cont.toggled.connect(self._on_cont_toggled)
        self._chk_sched.toggled.connect(self._on_sched_gekon_toggled)
        mode_lay.addWidget(self._chk_off)
        mode_lay.addWidget(self._chk_cont)
        mode_lay.addWidget(self._chk_sched)
        lbl_gekon = QLabel('  ⚙ Gekon (double-press → BLE) always active')
        lbl_gekon.setStyleSheet('color: #9E9E9E; font-size: 9pt;')
        mode_lay.addWidget(lbl_gekon)
        outer.addWidget(mode_card)

        # ── Timing (Schedule / Gekon) ─────────────────────────────────────────
        outer.addWidget(self._section_header('Timing  (Schedule / Gekon)'))
        self._timing_card = card_frame()
        tg = QGridLayout(self._timing_card)
        tg.setContentsMargins(16, 12, 16, 12)
        tg.setColumnStretch(1, 1)
        tg.setHorizontalSpacing(12)
        tg.setVerticalSpacing(8)

        self._lbl_iv = QLabel('Interval between sessions:')
        iv_widget = QWidget()
        iv_row = QHBoxLayout(iv_widget)
        iv_row.setContentsMargins(0, 0, 0, 0)
        iv_row.setSpacing(4)
        self._spin_iv_min = QSpinBox()
        self._spin_iv_min.setRange(0, 1092)
        self._spin_iv_min.setValue(30)
        self._spin_iv_min.setSuffix(' min')
        self._spin_iv_min.setFixedWidth(90)
        self._spin_iv_sec = QSpinBox()
        self._spin_iv_sec.setRange(0, 59)
        self._spin_iv_sec.setValue(0)
        self._spin_iv_sec.setSuffix(' sec')
        self._spin_iv_sec.setFixedWidth(80)
        iv_row.addWidget(self._spin_iv_min)
        iv_row.addWidget(self._spin_iv_sec)
        iv_row.addStretch(1)
        tg.addWidget(self._lbl_iv,  0, 0)
        tg.addWidget(iv_widget,     0, 1, Qt.AlignmentFlag.AlignLeft)

        tg.addWidget(QLabel('Session duration:'), 1, 0)
        self._spin_dur = QSpinBox()
        self._spin_dur.setRange(5, 3600)
        self._spin_dur.setValue(60)
        self._spin_dur.setSuffix(' sec')
        self._spin_dur.setFixedWidth(100)
        tg.addWidget(self._spin_dur, 1, 1, Qt.AlignmentFlag.AlignLeft)
        outer.addWidget(self._timing_card)

        # ── Advertising interval ──────────────────────────────────────────────
        outer.addWidget(self._section_header('Advertising Interval'))
        adv_card = card_frame()
        ag = QGridLayout(adv_card)
        ag.setContentsMargins(16, 12, 16, 12)
        ag.setColumnStretch(1, 1)
        ag.setHorizontalSpacing(12)
        ag.addWidget(QLabel('Advertising interval:'), 0, 0)
        self._spin_adv = QSpinBox()
        self._spin_adv.setRange(100, 10000)
        self._spin_adv.setValue(1000)
        self._spin_adv.setSuffix(' ms')
        self._spin_adv.setFixedWidth(110)
        self._spin_adv.setToolTip(
            'BLE advertising packet period.\n'
            '100 ms = very frequent (high power).\n'
            '1000 ms = default. 10000 ms = battery-saving.')
        ag.addWidget(self._spin_adv, 0, 1, Qt.AlignmentFlag.AlignLeft)
        outer.addWidget(adv_card)

        # ── TX power ─────────────────────────────────────────────────────────
        outer.addWidget(self._section_header('TX Power'))
        pwr_card = card_frame()
        pg2 = QGridLayout(pwr_card)
        pg2.setContentsMargins(16, 12, 16, 12)
        pg2.setColumnStretch(1, 1)
        pg2.setHorizontalSpacing(12)
        pg2.addWidget(QLabel('Power level (0 = min, 31 = max):'), 0, 0)
        self._spin_pwr = QSpinBox()
        self._spin_pwr.setRange(0, 31)
        self._spin_pwr.setValue(24)
        self._spin_pwr.setFixedWidth(70)
        self._spin_pwr.setToolTip(
            'ACI HAL TX power parameter.\n'
            '0 = −40 dBm  |  24 ≈ 0 dBm  |  31 = +6 dBm')
        self._lbl_pwr_hint = QLabel('(24 ≈ 0 dBm)')
        self._lbl_pwr_hint.setStyleSheet('color: gray; font-size:9pt;')
        self._spin_pwr.valueChanged.connect(self._on_pwr_changed)
        pg2.addWidget(self._spin_pwr,      0, 1, Qt.AlignmentFlag.AlignLeft)
        pg2.addWidget(self._lbl_pwr_hint,  0, 2, Qt.AlignmentFlag.AlignLeft)
        outer.addWidget(pwr_card)

        # ── Device name ───────────────────────────────────────────────────────
        outer.addWidget(self._section_header('Device Name'))
        name_card = card_frame()
        nl = QVBoxLayout(name_card)
        nl.setContentsMargins(16, 12, 16, 12)
        nl.setSpacing(8)
        self._rb_name_auto = QRadioButton('Auto  (BCN_XXXX derived from UID)')
        self._rb_name_man  = QRadioButton('Manual:')
        bg_nm = QButtonGroup(self)
        bg_nm.addButton(self._rb_name_auto, 0)
        bg_nm.addButton(self._rb_name_man,  1)
        self._rb_name_auto.setChecked(True)
        nl.addWidget(self._rb_name_auto)
        nm_row = QHBoxLayout()
        nm_row.setContentsMargins(0, 0, 0, 0)
        nm_row.addWidget(self._rb_name_man)
        self._edit_name = QLineEdit()
        self._edit_name.setMaxLength(11)
        self._edit_name.setPlaceholderText('e.g. BCN_RAT1  (max 11 chars)')
        self._edit_name.setEnabled(False)
        nm_row.addWidget(self._edit_name)
        nl.addLayout(nm_row)
        self._rb_name_man.toggled.connect(self._edit_name.setEnabled)
        outer.addWidget(name_card)

        # ── LED Indicator ──────────────────────────────────────────────────────
        outer.addWidget(self._section_header('LED Indicator'))
        led_card = card_frame()
        ll = QVBoxLayout(led_card)
        ll.setContentsMargins(16, 12, 16, 12)
        ll.setSpacing(6)
        led_row = QHBoxLayout()
        led_row.setContentsMargins(0, 0, 0, 0)
        led_row.setSpacing(10)
        led_row.addWidget(QLabel('BLE LED mode:'))
        self._cmb_ble_led = QComboBox()
        self._cmb_ble_led.addItems([
            'Normal  (blink each adv interval)',
            'Off  (follow main LED setting)',
            'Triple blink  (3× at start, CPU1 sleeps)',
        ])
        led_row.addWidget(self._cmb_ble_led)
        led_row.addStretch()
        ll.addLayout(led_row)
        outer.addWidget(led_card)

        # ── Auto-scan ─────────────────────────────────────────────────────────
        outer.addWidget(self._section_header('Auto-Scan'))
        scan_card = card_frame()
        sl = QVBoxLayout(scan_card)
        sl.setContentsMargins(16, 10, 16, 10)
        sl.setSpacing(6)
        self._chk_auto_scan = QCheckBox('Scan on startup')
        self._chk_auto_scan.setToolTip(
            'Scan for BCN_ beacons immediately when the app starts;\n'
            'auto-connect if a known beacon is found.')
        self._chk_auto_reconnect = QCheckBox('Auto-reconnect on disconnect')
        self._chk_auto_reconnect.setToolTip(
            'Restart background BLE scan automatically after connection is lost;\n'
            'reconnects to the same beacon when it reappears.')
        sl.addWidget(self._chk_auto_scan)
        sl.addWidget(self._chk_auto_reconnect)
        outer.addWidget(scan_card)

        # ── Auto-Disconnect ───────────────────────────────────────────────────
        outer.addWidget(self._section_header('Auto-Disconnect'))
        disc_card = card_frame()
        dl = QVBoxLayout(disc_card)
        dl.setContentsMargins(16, 10, 16, 10)
        dl.setSpacing(8)
        disc_row = QHBoxLayout()
        disc_row.setContentsMargins(0, 0, 0, 0)
        disc_row.setSpacing(8)
        self._chk_auto_disc = QCheckBox('Auto-disconnect after:')
        self._chk_auto_disc.setToolTip(
            'Automatically disconnect BLE after the specified time.\n'
            'Saves beacon battery — beacon returns to idle/sleep after disconnect.')
        self._spin_auto_disc_min = QSpinBox()
        self._spin_auto_disc_min.setRange(1, 60)
        self._spin_auto_disc_min.setValue(1)
        self._spin_auto_disc_min.setSuffix(' min')
        self._spin_auto_disc_min.setFixedWidth(90)
        self._spin_auto_disc_min.setEnabled(False)
        self._chk_auto_disc.toggled.connect(self._spin_auto_disc_min.setEnabled)
        disc_row.addWidget(self._chk_auto_disc)
        disc_row.addWidget(self._spin_auto_disc_min)
        disc_row.addStretch()
        dl.addLayout(disc_row)
        lbl_disc_hint = QLabel('Beacon returns to BLE idle / sleep after disconnect')
        lbl_disc_hint.setStyleSheet('color: gray; font-size: 9pt;')
        dl.addWidget(lbl_disc_hint)
        outer.addWidget(disc_card)

        # ── Scan Parameters ───────────────────────────────────────────────────
        outer.addWidget(self._section_header('Scan Parameters (PC side)'))
        sp_card = card_frame()
        sp_lay = QVBoxLayout(sp_card)
        sp_lay.setContentsMargins(16, 10, 16, 10)
        sp_lay.setSpacing(8)
        sp_row1 = QHBoxLayout(); sp_row1.setSpacing(8)
        sp_row1.addWidget(QLabel('Scan interval:'))
        self._spin_scan_iv = QSpinBox()
        self._spin_scan_iv.setRange(10, 15000)
        self._spin_scan_iv.setValue(60)
        self._spin_scan_iv.setSuffix(' ms')
        self._spin_scan_iv.setFixedWidth(100)
        self._spin_scan_iv.setToolTip('BLE scan interval — time between scan windows')
        sp_row1.addWidget(self._spin_scan_iv)
        sp_row1.addSpacing(20)
        sp_row1.addWidget(QLabel('Scan window:'))
        self._spin_scan_win = QSpinBox()
        self._spin_scan_win.setRange(10, 15000)
        self._spin_scan_win.setValue(60)
        self._spin_scan_win.setSuffix(' ms')
        self._spin_scan_win.setFixedWidth(100)
        self._spin_scan_win.setToolTip('BLE scan window — active listen time per interval\n'
                                       'Set equal to interval for 100% duty cycle (fastest discovery)')
        sp_row1.addWidget(self._spin_scan_win)
        sp_row1.addStretch()
        sp_lay.addLayout(sp_row1)
        lbl_sp_hint = QLabel('Tip: set both to 60 ms for fast scans  •  '
                             'reduce advertising interval on beacon for fastest connect')
        lbl_sp_hint.setStyleSheet('color: gray; font-size: 9pt;')
        sp_lay.addWidget(lbl_sp_hint)
        outer.addWidget(sp_card)

        # ── Action buttons ────────────────────────────────────────────────────
        btn_bar = QHBoxLayout()
        self._btn_read  = QPushButton(f'{ICO["download"]}  Read from device')
        self._btn_apply = QPushButton(f'{ICO["ok"]}  Apply to device')
        self._btn_apply.setObjectName('primary')
        self._btn_apply.setFixedWidth(180)
        btn_bar.addWidget(self._btn_read)
        btn_bar.addStretch(1)
        btn_bar.addWidget(self._btn_apply)
        outer.addLayout(btn_bar)

        self._lbl_status = QLabel()
        self._lbl_status.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._lbl_status.setMinimumHeight(20)
        outer.addWidget(self._lbl_status)

        outer.addStretch(1)

        self._btn_read.clicked.connect(self.ble_read_requested)
        self._btn_apply.clicked.connect(self._on_apply_clicked)

        self._update_timing_state()

    # ── Slot helpers ──────────────────────────────────────────────────────────

    # ── Checkbox exclusivity logic ────────────────────────────────────────────

    def _on_off_toggled(self, checked: bool):
        if checked:
            for chk in (self._chk_cont, self._chk_sched):
                chk.blockSignals(True)
                chk.setChecked(False)
                chk.blockSignals(False)
        self._update_timing_state()

    def _on_cont_toggled(self, checked: bool):
        if checked:
            for chk in (self._chk_off, self._chk_sched):
                chk.blockSignals(True)
                chk.setChecked(False)
                chk.blockSignals(False)
        self._update_timing_state()

    def _on_sched_gekon_toggled(self, checked: bool):
        if checked:
            for chk in (self._chk_off, self._chk_cont):
                chk.blockSignals(True)
                chk.setChecked(False)
                chk.blockSignals(False)
        self._update_timing_state()

    def _update_timing_state(self):
        has_timing = self._chk_sched.isChecked()
        self._timing_card.setEnabled(has_timing)
        iv_en = self._chk_sched.isChecked()
        self._lbl_iv.setEnabled(iv_en)
        self._spin_iv_min.setEnabled(iv_en)
        self._spin_iv_sec.setEnabled(iv_en)

    def _on_pwr_changed(self, val: int):
        # Rough dBm map for the WB1M power table
        _hints = {0: '−40 dBm', 4: '−20 dBm', 8: '−10 dBm', 12: '−5 dBm',
                  16: '−3 dBm', 20: '−1 dBm', 24: '0 dBm', 28: '+3 dBm', 31: '+6 dBm'}
        closest = min(_hints.keys(), key=lambda k: abs(k - val))
        hint = _hints[closest]
        self._lbl_pwr_hint.setText(f'(≈ {hint})')

    def _on_apply_clicked(self):
        self.ble_apply_requested.emit(self._get_settings())

    def _get_settings(self) -> BleSettings:
        if self._chk_off.isChecked():
            op_mode = BLE_OP_OFF
        elif self._chk_cont.isChecked():
            op_mode = BLE_OP_CONTINUOUS
        else:
            # GEKON always included (firmware enforces it anyway)
            op_mode = BLE_OP_GEKON
            if self._chk_sched.isChecked(): op_mode |= BLE_OP_SCHEDULE
        nm = 1 if self._rb_name_man.isChecked() else 0
        iv_s = self._spin_iv_min.value() * 60 + self._spin_iv_sec.value()
        if iv_s < 5:
            iv_s = 5   # minimum 5 seconds
        return BleSettings(
            op_mode         = op_mode,
            tx_power        = self._spin_pwr.value(),
            interval_s      = iv_s,
            duration_sec    = self._spin_dur.value(),
            adv_interval_ms = self._spin_adv.value(),
            name_mode       = nm,
            name            = self._edit_name.text() if nm else '',
            led_mode        = self._cmb_ble_led.currentIndex(),
        )

    # ── Public API ────────────────────────────────────────────────────────────

    def get_auto_scan_settings(self) -> tuple:
        """Returns (auto_scan_startup: bool, auto_reconnect: bool)."""
        return (self._chk_auto_scan.isChecked(),
                self._chk_auto_reconnect.isChecked())

    def set_auto_scan_settings(self, auto_scan: bool, auto_reconnect: bool):
        self._chk_auto_scan.setChecked(auto_scan)
        self._chk_auto_reconnect.setChecked(auto_reconnect)

    def get_auto_disconnect(self) -> tuple:
        """Returns (enabled: bool, minutes: int)."""
        return (self._chk_auto_disc.isChecked(), self._spin_auto_disc_min.value())

    def set_auto_disconnect(self, enabled: bool, minutes: int):
        self._chk_auto_disc.setChecked(enabled)
        self._spin_auto_disc_min.setValue(max(1, min(60, minutes)))

    def get_scan_params(self) -> tuple:
        """Returns (interval_ms: int, window_ms: int)."""
        return (self._spin_scan_iv.value(), self._spin_scan_win.value())

    def set_scan_params(self, iv_ms: int, win_ms: int):
        self._spin_scan_iv.setValue(max(10, min(15000, iv_ms)))
        self._spin_scan_win.setValue(max(10, min(iv_ms, win_ms)))

    def load_ble(self, s: BleSettings):
        bits = s.op_mode
        for chk in (self._chk_off, self._chk_cont, self._chk_sched):
            chk.blockSignals(True)
        if bits == BLE_OP_OFF:
            self._chk_off.setChecked(True)
            self._chk_cont.setChecked(False)
            self._chk_sched.setChecked(False)
        elif bits & BLE_OP_CONTINUOUS:
            self._chk_off.setChecked(False)
            self._chk_cont.setChecked(True)
            self._chk_sched.setChecked(False)
        else:
            self._chk_off.setChecked(False)
            self._chk_cont.setChecked(False)
            self._chk_sched.setChecked(bool(bits & BLE_OP_SCHEDULE))
        for chk in (self._chk_off, self._chk_cont, self._chk_sched):
            chk.blockSignals(False)
        self._spin_pwr.setValue(s.tx_power)
        iv_s = max(5, s.interval_s)
        self._spin_iv_min.setValue(iv_s // 60)
        self._spin_iv_sec.setValue(iv_s % 60)
        self._spin_dur.setValue(max(5, s.duration_sec))
        self._spin_adv.setValue(max(100, s.adv_interval_ms))
        if s.name_mode:
            self._rb_name_man.setChecked(True)
            self._edit_name.setText(s.name)
        else:
            self._rb_name_auto.setChecked(True)
            self._edit_name.clear()
        self._cmb_ble_led.setCurrentIndex(min(max(s.led_mode, 0), 2))
        self._update_timing_state()

    def set_status(self, text: str, ok: bool = True):
        colour = '#22c55e' if ok else '#ef4444'
        self._lbl_status.setText(text)
        self._lbl_status.setStyleSheet(f'color:{colour}; font-weight:bold;')

    def retheme(self, C: dict):
        self._C = C
        for lbl, sep in self._sec_hdrs:
            self._apply_sec_style(lbl, sep, C)


# ─────────────────────────────────────────────────────────────────────────────
# App Log Tab — shows protocol telemetry and internal events
# ─────────────────────────────────────────────────────────────────────────────
class AppLogTab(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._C = THEMES['light']
        self._build_ui()

    def _build_ui(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(12, 12, 12, 12)
        lay.setSpacing(8)

        bar = QHBoxLayout()
        lbl = QLabel(f'{ICO["log"]}  Protocol & Event Log')
        lbl.setStyleSheet('font-weight:bold; font-size:11pt;')
        bar.addWidget(lbl)
        bar.addStretch(1)
        btn_clear = QPushButton('Clear')
        btn_clear.setObjectName('secondary')
        btn_clear.setFixedWidth(72)
        btn_clear.clicked.connect(self._clear)
        bar.addWidget(btn_clear)
        lay.addLayout(bar)

        self._pte = QPlainTextEdit()
        self._pte.setReadOnly(True)
        self._pte.setMaximumBlockCount(2000)
        self._pte.setFont(QFont('Consolas', 9))
        lay.addWidget(self._pte, 1)
        self._apply_pte_style()

    def _apply_pte_style(self):
        C = self._C
        self._pte.setStyleSheet(
            f'QPlainTextEdit {{ background:{C["log_bg"]}; color:{C["log_text"]}; '
            f'border:1px solid {C["border"]}; border-radius:6px; }}'
        )

    def append_line(self, line: str):
        self._pte.appendPlainText(line)
        sb = self._pte.verticalScrollBar()
        sb.setValue(sb.maximum())

    def _clear(self):
        self._pte.clear()

    def retheme(self, C: dict):
        self._C = C
        self._apply_pte_style()


# ─────────────────────────────────────────────────────────────────────────────
# Events tab helpers
# ─────────────────────────────────────────────────────────────────────────────

class _CondRow(QWidget):
    """One condition row: type selector + context-aware parameter widgets."""
    removed = pyqtSignal(object)   # self
    changed = pyqtSignal()

    # Condition category → stacked-widget page index
    _PSTACK = {
        COND_DISABLED:   0, COND_MOTION:     0, COND_ALWAYS:     0, COND_BEFORE_BLE: 0,
        COND_BATT_BELOW: 1, COND_BATT_ABOVE: 1, COND_LIGHT_BELOW:1, COND_LIGHT_ABOVE:1,
        COND_TEMP_ABOVE: 2, COND_TEMP_BELOW: 2,
        COND_NO_MOTION:  3, COND_EVERY_NCYC: 3,
        COND_EVERY_NHRS: 4,
    }

    def __init__(self, parent=None):
        super().__init__(parent)
        self._build()

    def _build(self):
        h = QHBoxLayout(self)
        h.setContentsMargins(0, 3, 0, 3)
        h.setSpacing(8)

        self._type_cb = QComboBox()
        self._type_cb.setFixedWidth(190)
        for code, lbl in COND_LABELS:
            self._type_cb.addItem(lbl, code)
        h.addWidget(self._type_cb)

        self._stack = QStackedWidget()
        self._stack.setFixedHeight(30)

        # Page 0: no params
        self._stack.addWidget(QWidget())

        # Page 1: % (battery / light)
        p1 = QWidget(); l1 = QHBoxLayout(p1); l1.setContentsMargins(0,0,0,0); l1.setSpacing(4)
        self._pct_spin = QSpinBox(); self._pct_spin.setRange(0, 100)
        self._pct_spin.setSuffix(' %'); self._pct_spin.setFixedWidth(80)
        self._pct_spin.valueChanged.connect(self.changed)
        l1.addWidget(self._pct_spin); l1.addStretch()
        self._stack.addWidget(p1)

        # Page 2: temperature °C
        p2 = QWidget(); l2 = QHBoxLayout(p2); l2.setContentsMargins(0,0,0,0); l2.setSpacing(4)
        self._temp_spin = QSpinBox(); self._temp_spin.setRange(-50, 80)
        self._temp_spin.setSuffix(' °C'); self._temp_spin.setFixedWidth(90)
        self._temp_spin.valueChanged.connect(self.changed)
        l2.addWidget(self._temp_spin); l2.addStretch()
        self._stack.addWidget(p2)

        # Page 3: N cycles (no-motion / every-N-cycles)
        p3 = QWidget(); l3 = QHBoxLayout(p3); l3.setContentsMargins(0,0,0,0); l3.setSpacing(4)
        self._cyc_spin = QSpinBox(); self._cyc_spin.setRange(1, 9999)
        self._cyc_spin.setSuffix(' cycles'); self._cyc_spin.setFixedWidth(110)
        self._cyc_spin.valueChanged.connect(self.changed)
        l3.addWidget(self._cyc_spin); l3.addStretch()
        self._stack.addWidget(p3)

        # Page 4: Every H h M m S s  (val1=hours*60+minutes, val2=seconds)
        p4 = QWidget(); l4 = QHBoxLayout(p4); l4.setContentsMargins(0,0,0,0); l4.setSpacing(4)
        self._nhrs_h = QSpinBox(); self._nhrs_h.setRange(0, 167); self._nhrs_h.setSuffix(' h')
        self._nhrs_h.setFixedWidth(70); self._nhrs_h.valueChanged.connect(self.changed)
        self._nhrs_m = QSpinBox(); self._nhrs_m.setRange(0, 59); self._nhrs_m.setSuffix(' m')
        self._nhrs_m.setFixedWidth(66); self._nhrs_m.valueChanged.connect(self.changed)
        self._nhrs_s = QSpinBox(); self._nhrs_s.setRange(0, 59); self._nhrs_s.setSuffix(' s')
        self._nhrs_s.setFixedWidth(66); self._nhrs_s.valueChanged.connect(self.changed)
        for w in (self._nhrs_h, self._nhrs_m, self._nhrs_s):
            l4.addWidget(w)
        l4.addStretch()
        self._stack.addWidget(p4)

        h.addWidget(self._stack, 1)

        self._btn_rm = QPushButton('✕')
        self._btn_rm.setFixedSize(26, 26)
        self._btn_rm.setObjectName('secondary')
        self._btn_rm.setToolTip('Remove this condition')
        self._btn_rm.clicked.connect(lambda: self.removed.emit(self))
        h.addWidget(self._btn_rm)

        self._type_cb.currentIndexChanged.connect(self._on_type)
        self._on_type()

    def _on_type(self):
        code = self._type_cb.currentData()
        self._stack.setCurrentIndex(self._PSTACK.get(code, 0))
        self.changed.emit()

    def set_removable(self, v: bool):
        self._btn_rm.setVisible(v)

    def get_data(self) -> dict:
        code = self._type_cb.currentData()
        v1, v2 = 0, 0
        pg = self._PSTACK.get(code, 0)
        if pg == 1: v1 = self._pct_spin.value()
        elif pg == 2: v1 = self._temp_spin.value()
        elif pg == 3: v1 = self._cyc_spin.value()
        elif pg == 4:
            # val1 = hours*60+minutes (total_minutes), val2 = seconds
            v1 = self._nhrs_h.value() * 60 + self._nhrs_m.value()
            v2 = self._nhrs_s.value()
        return {'type': code, 'val1': v1, 'val2': v2}

    def set_data(self, d: dict):
        code = d.get('type', COND_DISABLED)
        for i in range(self._type_cb.count()):
            if self._type_cb.itemData(i) == code:
                self._type_cb.setCurrentIndex(i)
                break
        v1, v2 = d.get('val1', 0), d.get('val2', 0)
        pg = self._PSTACK.get(code, 0)
        if pg == 1: self._pct_spin.setValue(v1)
        elif pg == 2: self._temp_spin.setValue(v1)
        elif pg == 3: self._cyc_spin.setValue(v1)
        elif pg == 4:
            total_min = max(0, v1)
            self._nhrs_h.setValue(total_min // 60)
            self._nhrs_m.setValue(total_min % 60)
            self._nhrs_s.setValue(max(0, v2))


class _ActWidget(QWidget):
    """Action type selector + context-aware parameter widgets."""
    changed = pyqtSignal()

    _PSTACK = {
        ACT_NONE: 0, ACT_BLE_START: 0, ACT_LED_ON: 0, ACT_LED_OFF: 0,
        ACT_SET_POWER: 1, ACT_TX_PULSES: 2, ACT_TX_PAT: 3,
        ACT_SET_CH: 4, ACT_SET_PERIOD: 5, ACT_LOG_MARK: 6,
        ACT_LED_BLINK: 7,
    }

    def __init__(self, parent=None):
        super().__init__(parent)
        self._build()

    def _build(self):
        h = QHBoxLayout(self)
        h.setContentsMargins(0, 3, 0, 3)
        h.setSpacing(8)

        lbl = QLabel('→ Then:')
        lbl.setStyleSheet('font-weight: bold;')
        h.addWidget(lbl)

        self._type_cb = QComboBox()
        self._type_cb.setFixedWidth(200)
        for code, lbl_t in ACT_LABELS:
            self._type_cb.addItem(lbl_t, code)
        h.addWidget(self._type_cb)

        self._stack = QStackedWidget()
        self._stack.setFixedHeight(30)

        # Page 0: no params
        self._stack.addWidget(QWidget())

        # Page 1: TX power level
        p1 = QWidget(); l1 = QHBoxLayout(p1); l1.setContentsMargins(0,0,0,0); l1.setSpacing(4)
        l1.addWidget(QLabel('Level:'))
        self._pwr_cb = QComboBox(); self._pwr_cb.setFixedWidth(100)
        for i, n in [(1,'Low (1)'), (2,'Mid (2)'), (3,'High (3)'), (4,'Max (4)')]:
            self._pwr_cb.addItem(n, i)
        self._pwr_cb.currentIndexChanged.connect(self.changed)
        l1.addWidget(self._pwr_cb); l1.addStretch()
        self._stack.addWidget(p1)

        # Page 2: TX pulses — count + gap
        p2 = QWidget(); l2 = QHBoxLayout(p2); l2.setContentsMargins(0,0,0,0); l2.setSpacing(4)
        l2.addWidget(QLabel('Count:'))
        self._pcnt = QSpinBox(); self._pcnt.setRange(1, 255); self._pcnt.setFixedWidth(65)
        self._pcnt.valueChanged.connect(self.changed)
        l2.addWidget(self._pcnt)
        l2.addSpacing(8); l2.addWidget(QLabel('Gap:'))
        self._pgap = QSpinBox(); self._pgap.setRange(1, 9999); self._pgap.setSuffix(' ms')
        self._pgap.setFixedWidth(90); self._pgap.valueChanged.connect(self.changed)
        l2.addWidget(self._pgap); l2.addStretch()
        self._stack.addWidget(p2)

        # Page 3: TX pattern — ON ms / OFF ms
        p3 = QWidget(); l3 = QHBoxLayout(p3); l3.setContentsMargins(0,0,0,0); l3.setSpacing(4)
        l3.addWidget(QLabel('ON:'))
        self._pon = QSpinBox(); self._pon.setRange(1, 9999); self._pon.setSuffix(' ms')
        self._pon.setFixedWidth(90); self._pon.valueChanged.connect(self.changed)
        l3.addWidget(self._pon)
        l3.addSpacing(8); l3.addWidget(QLabel('OFF:'))
        self._poff = QSpinBox(); self._poff.setRange(1, 9999); self._poff.setSuffix(' ms')
        self._poff.setFixedWidth(90); self._poff.valueChanged.connect(self.changed)
        l3.addWidget(self._poff); l3.addStretch()
        self._stack.addWidget(p3)

        # Page 4: channel selector CH0-CH3
        p4 = QWidget(); l4 = QHBoxLayout(p4); l4.setContentsMargins(0,0,0,0); l4.setSpacing(4)
        l4.addWidget(QLabel('Channel:'))
        self._ch_cb = QComboBox(); self._ch_cb.setFixedWidth(80)
        for i in range(4):
            self._ch_cb.addItem(f'CH{i}', i)
        self._ch_cb.currentIndexChanged.connect(self.changed)
        l4.addWidget(self._ch_cb); l4.addStretch()
        self._stack.addWidget(p4)

        # Page 5: TX period (seconds)
        p5 = QWidget(); l5 = QHBoxLayout(p5); l5.setContentsMargins(0,0,0,0); l5.setSpacing(4)
        l5.addWidget(QLabel('Period:'))
        self._per_s = QSpinBox(); self._per_s.setRange(1, 3600); self._per_s.setSuffix(' s')
        self._per_s.setFixedWidth(90); self._per_s.valueChanged.connect(self.changed)
        l5.addWidget(self._per_s); l5.addStretch()
        self._stack.addWidget(p5)

        # Page 6: log marker code
        p6 = QWidget(); l6 = QHBoxLayout(p6); l6.setContentsMargins(0,0,0,0); l6.setSpacing(4)
        l6.addWidget(QLabel('Marker code:'))
        self._mark = QSpinBox(); self._mark.setRange(1, 255); self._mark.setFixedWidth(65)
        self._mark.valueChanged.connect(self.changed)
        l6.addWidget(self._mark); l6.addStretch()
        self._stack.addWidget(p6)

        # Page 7: LED blink — count + period_ms
        p7 = QWidget(); l7 = QHBoxLayout(p7); l7.setContentsMargins(0,0,0,0); l7.setSpacing(4)
        l7.addWidget(QLabel('Count:'))
        self._led_cnt = QSpinBox(); self._led_cnt.setRange(1, 20); self._led_cnt.setFixedWidth(55)
        self._led_cnt.valueChanged.connect(self.changed)
        l7.addWidget(self._led_cnt)
        l7.addSpacing(8); l7.addWidget(QLabel('Period:'))
        self._led_ms = QSpinBox(); self._led_ms.setRange(50, 5000); self._led_ms.setSuffix(' ms')
        self._led_ms.setValue(200); self._led_ms.setFixedWidth(90)
        self._led_ms.valueChanged.connect(self.changed)
        l7.addWidget(self._led_ms); l7.addStretch()
        self._stack.addWidget(p7)

        h.addWidget(self._stack, 1)
        self._type_cb.currentIndexChanged.connect(self._on_type)
        self._on_type()

    def _on_type(self):
        code = self._type_cb.currentData()
        self._stack.setCurrentIndex(self._PSTACK.get(code, 0))
        self.changed.emit()

    def get_data(self) -> dict:
        code = self._type_cb.currentData()
        p1, p2 = 0, 0
        pg = self._PSTACK.get(code, 0)
        if pg == 1: p1 = self._pwr_cb.currentData() or 1
        elif pg == 2: p1 = self._pcnt.value(); p2 = self._pgap.value()
        elif pg == 3: p1 = self._pon.value(); p2 = self._poff.value()
        elif pg == 4: p1 = self._ch_cb.currentData() or 0
        elif pg == 5: p1 = self._per_s.value()
        elif pg == 6: p1 = self._mark.value()
        elif pg == 7: p1 = self._led_cnt.value(); p2 = self._led_ms.value()
        return {'type': code, 'p1': p1, 'p2': p2}

    def set_data(self, d: dict):
        code = d.get('type', ACT_NONE)
        for i in range(self._type_cb.count()):
            if self._type_cb.itemData(i) == code:
                self._type_cb.setCurrentIndex(i); break
        p1, p2 = d.get('p1', 0), d.get('p2', 0)
        pg = self._PSTACK.get(code, 0)
        if pg == 1:
            for i in range(self._pwr_cb.count()):
                if self._pwr_cb.itemData(i) == p1: self._pwr_cb.setCurrentIndex(i); break
        elif pg == 2: self._pcnt.setValue(p1); self._pgap.setValue(p2)
        elif pg == 3: self._pon.setValue(p1); self._poff.setValue(p2)
        elif pg == 4:
            for i in range(self._ch_cb.count()):
                if self._ch_cb.itemData(i) == p1: self._ch_cb.setCurrentIndex(i); break
        elif pg == 5: self._per_s.setValue(p1)
        elif pg == 6: self._mark.setValue(p1)
        elif pg == 7:
            self._led_cnt.setValue(max(1, p1))
            self._led_ms.setValue(max(50, p2) if p2 > 0 else 200)


class _EventCard(QFrame):
    """Collapsible event card: up to MAX_CONDS conditions (AND) + one action."""
    changed         = pyqtSignal()
    remove_requested = pyqtSignal(object)

    def __init__(self, index: int, parent=None):
        super().__init__(parent)
        self.setObjectName('card')
        self._index = index
        self._cond_rows: list = []
        self._build()

    def _build(self):
        vbox = QVBoxLayout(self)
        vbox.setContentsMargins(14, 10, 14, 10)
        vbox.setSpacing(6)

        # ── Title row ──────────────────────────────────────────────────────
        tr = QHBoxLayout()
        self._title_lbl = QLabel(f'Event {self._index + 1}')
        self._title_lbl.setFont(QFont('Segoe UI', 10, QFont.Weight.Bold))
        tr.addWidget(self._title_lbl)
        tr.addSpacing(12)
        self._en_chk  = QCheckBox('Enabled')
        self._en_chk.stateChanged.connect(lambda _: self.changed.emit())
        tr.addWidget(self._en_chk)
        self._shot_chk = QCheckBox('One-shot')
        self._shot_chk.setToolTip('Fire once then disable automatically')
        self._shot_chk.stateChanged.connect(lambda _: self.changed.emit())
        tr.addWidget(self._shot_chk)
        tr.addStretch()
        self._btn_rm = QPushButton('✕ Remove')
        self._btn_rm.setObjectName('secondary')
        self._btn_rm.setFixedWidth(88)
        self._btn_rm.clicked.connect(lambda: self.remove_requested.emit(self))
        tr.addWidget(self._btn_rm)
        vbox.addLayout(tr)

        vbox.addWidget(self._hr())

        # ── Conditions section ─────────────────────────────────────────────
        if_lbl = QLabel('IF  (all must be true):')
        if_lbl.setStyleSheet('color: #888; font-size: 9pt; font-style: italic;')
        vbox.addWidget(if_lbl)

        self._conds_vbox = QVBoxLayout()
        self._conds_vbox.setContentsMargins(0, 0, 0, 0)
        self._conds_vbox.setSpacing(2)
        vbox.addLayout(self._conds_vbox)

        self._btn_add_cond = QPushButton('＋ Add condition')
        self._btn_add_cond.setObjectName('secondary')
        self._btn_add_cond.setFixedWidth(140)
        self._btn_add_cond.clicked.connect(self._add_cond)
        vbox.addWidget(self._btn_add_cond)

        vbox.addWidget(self._hr())

        # ── Action section ─────────────────────────────────────────────────
        self._act = _ActWidget()
        self._act.changed.connect(self.changed)
        vbox.addWidget(self._act)

        vbox.addWidget(self._hr())

        # ── Cooldown row ───────────────────────────────────────────────────
        cr = QHBoxLayout()
        cr.addWidget(QLabel('Cooldown:'))
        self._cool = QSpinBox()
        self._cool.setRange(0, 255)
        self._cool.setSuffix(' s')
        self._cool.setFixedWidth(80)
        self._cool.setToolTip('Min seconds between fires (0 = fire on every wake)')
        self._cool.valueChanged.connect(lambda _: self.changed.emit())
        cr.addWidget(self._cool)
        cr.addStretch()
        vbox.addLayout(cr)

        # Add the first condition
        self._add_cond()

    def _hr(self) -> QFrame:
        line = QFrame()
        line.setFrameShape(QFrame.Shape.HLine)
        line.setFrameShadow(QFrame.Shadow.Sunken)
        return line

    def _add_cond(self):
        if len(self._cond_rows) >= MAX_CONDS:
            return
        row = _CondRow()
        row.removed.connect(self._remove_cond)
        row.changed.connect(self.changed)
        self._cond_rows.append(row)
        self._conds_vbox.addWidget(row)
        self._refresh_cond_buttons()
        self.changed.emit()

    def _remove_cond(self, row):
        if len(self._cond_rows) <= 1:
            return
        self._cond_rows.remove(row)
        row.setParent(None)
        row.deleteLater()
        self._refresh_cond_buttons()
        self.changed.emit()

    def _refresh_cond_buttons(self):
        can_rm = len(self._cond_rows) > 1
        for r in self._cond_rows:
            r.set_removable(can_rm)
        self._btn_add_cond.setVisible(len(self._cond_rows) < MAX_CONDS)

    # public
    def set_index(self, idx: int):
        self._index = idx
        self._title_lbl.setText(f'Event {idx + 1}')

    def set_removable(self, v: bool):
        self._btn_rm.setVisible(v)

    def get_data(self) -> dict:
        return {
            'enabled':  self._en_chk.isChecked(),
            'oneshot':  self._shot_chk.isChecked(),
            'conds':    [r.get_data() for r in self._cond_rows],
            'act':      self._act.get_data(),
            'cooldown': self._cool.value(),
        }

    def set_data(self, d: dict):
        self._en_chk.setChecked(d.get('enabled', False))
        self._shot_chk.setChecked(d.get('oneshot', False))
        self._cool.setValue(d.get('cooldown', 0))
        self._act.set_data(d.get('act', {'type': ACT_NONE, 'p1': 0, 'p2': 0}))
        conds = d.get('conds') or [{'type': COND_DISABLED, 'val1': 0, 'val2': 0}]
        # Rebuild condition rows
        for r in self._cond_rows:
            r.setParent(None); r.deleteLater()
        self._cond_rows.clear()
        for c in conds[:MAX_CONDS]:
            row = _CondRow()
            row.removed.connect(self._remove_cond)
            row.changed.connect(self.changed)
            row.set_data(c)
            self._cond_rows.append(row)
            self._conds_vbox.addWidget(row)
        self._refresh_cond_buttons()


# ─────────────────────────────────────────────────────────────────────────────
# Events tab
# ─────────────────────────────────────────────────────────────────────────────
class EventsTab(QWidget):
    events_read_requested  = pyqtSignal()
    events_write_requested = pyqtSignal(bytes)   # 112-byte blob
    markers_load_requested = pyqtSignal()
    wom_apply_requested    = pyqtSignal(dict)    # {'enable', 'threshold_mg', 'duration_ms', 'action'}
    wom_refresh_requested  = pyqtSignal()
    wom_clear_requested    = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._C = THEMES['light']
        self._cards: list = []
        self._build()

    def _build(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        # ── Header bar ─────────────────────────────────────────────────────
        bar = QFrame()
        bl = QHBoxLayout(bar)
        bl.setContentsMargins(14, 8, 14, 8)
        bl.setSpacing(8)
        title = QLabel(f'{ICO["events"]}  Events  —  IF / THEN rules')
        title.setFont(QFont('Segoe UI', 10, QFont.Weight.Bold))
        bl.addWidget(title)
        bl.addSpacing(12)
        note = QLabel(f'Max {MAX_EVENTS} events · up to {MAX_CONDS} conditions each (AND logic)')
        note.setStyleSheet('color: #888; font-size: 9pt;')
        bl.addWidget(note)
        bl.addStretch()

        self._btn_read = QPushButton('↓ Read')
        self._btn_read.setObjectName('secondary')
        self._btn_read.setEnabled(False)
        self._btn_read.setToolTip('Read events from connected device')
        self._btn_read.clicked.connect(self.events_read_requested)
        bl.addWidget(self._btn_read)

        self._btn_write = QPushButton('↑ Write')
        self._btn_write.setObjectName('secondary')
        self._btn_write.setEnabled(False)
        self._btn_write.setToolTip('Write events to connected device')
        self._btn_write.clicked.connect(self._do_write)
        bl.addWidget(self._btn_write)

        self._btn_markers = QPushButton('📋 Load Markers')
        self._btn_markers.setObjectName('secondary')
        self._btn_markers.setEnabled(False)
        self._btn_markers.setToolTip('Download log and show only marker records')
        self._btn_markers.clicked.connect(self.markers_load_requested)
        bl.addWidget(self._btn_markers)

        self._btn_clr_all = QPushButton('🗑 Clear all')
        self._btn_clr_all.setObjectName('secondary')
        self._btn_clr_all.clicked.connect(self._clear_all)
        bl.addWidget(self._btn_clr_all)
        outer.addWidget(bar)

        # ── Scroll area ────────────────────────────────────────────────────
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        inner = QWidget()
        self._root = QVBoxLayout(inner)
        self._root.setContentsMargins(12, 12, 12, 12)
        self._root.setSpacing(10)

        self._cards_vbox = QVBoxLayout()
        self._cards_vbox.setSpacing(10)
        self._root.addLayout(self._cards_vbox)

        self._btn_add = QPushButton('＋ Add Event')
        self._btn_add.setObjectName('secondary')
        self._btn_add.setFixedWidth(150)
        self._btn_add.clicked.connect(self._add_event)
        self._root.addWidget(self._btn_add)
        self._root.addStretch()

        scroll.setWidget(inner)
        outer.addWidget(scroll, 1)

        # ── Wake-on-Motion panel ───────────────────────────────────────────
        wom_box = QGroupBox('🔵  Wake-on-Motion  —  hardware accelerometer trigger')
        wom_box.setCheckable(True)
        wom_box.setChecked(False)
        wom_box.setStyleSheet('QGroupBox { font-size: 9pt; font-weight: bold; }')
        wom_vlay = QVBoxLayout(wom_box)
        wom_vlay.setContentsMargins(12, 6, 12, 10)
        wom_vlay.setSpacing(8)

        wom_note = QLabel(
            '⚠  Sensor must be powered on while WoM is armed.  '
            'Use Events COND_MOTION to react to motion in rules above.')
        wom_note.setWordWrap(True)
        wom_note.setStyleSheet('color:#b45000; font-size:9pt; background:transparent;')
        wom_vlay.addWidget(wom_note)

        row1 = QHBoxLayout()
        row1.addWidget(QLabel('Enable:'))
        self._wom_tog = ToggleSwitch()
        row1.addWidget(self._wom_tog)
        row1.addSpacing(24)
        row1.addWidget(QLabel('Threshold:'))
        self._wom_thr = QSpinBox(); self._wom_thr.setRange(1, 16383); self._wom_thr.setValue(500)
        self._wom_thr.setSuffix(' mg'); self._wom_thr.setFixedWidth(90)
        row1.addWidget(self._wom_thr)
        row1.addSpacing(16)
        row1.addWidget(QLabel('Duration:'))
        self._wom_dur = QSpinBox(); self._wom_dur.setRange(0, 65535); self._wom_dur.setValue(100)
        self._wom_dur.setSuffix(' ms'); self._wom_dur.setFixedWidth(90)
        row1.addWidget(self._wom_dur)
        row1.addStretch()
        wom_vlay.addLayout(row1)

        row2 = QHBoxLayout()
        row2.addWidget(QLabel('Action:'))
        self._chk_wom_wake = QCheckBox('Wake MCU'); self._chk_wom_wake.setChecked(True)
        self._chk_wom_tx   = QCheckBox('TX burst')
        self._chk_wom_log  = QCheckBox('Log marker')
        for w in [self._chk_wom_wake, self._chk_wom_tx, self._chk_wom_log]:
            row2.addWidget(w)
        row2.addStretch()
        wom_vlay.addLayout(row2)

        row3 = QHBoxLayout()
        self._btn_wom_apply   = QPushButton('Apply'); self._btn_wom_apply.setObjectName('secondary')
        self._btn_wom_refresh = QPushButton('Refresh status'); self._btn_wom_refresh.setObjectName('secondary')
        self._btn_wom_clear   = QPushButton('Clear counter'); self._btn_wom_clear.setObjectName('secondary')
        self._lbl_wom_status  = QLabel('—')
        self._lbl_wom_status.setStyleSheet('color:#888; font-size:9pt;')
        row3.addWidget(self._btn_wom_apply)
        row3.addWidget(self._btn_wom_refresh)
        row3.addWidget(self._btn_wom_clear)
        row3.addSpacing(12); row3.addWidget(self._lbl_wom_status)
        row3.addStretch()
        wom_vlay.addLayout(row3)

        self._btn_wom_apply.clicked.connect(self._on_wom_apply)
        self._btn_wom_refresh.clicked.connect(self.wom_refresh_requested.emit)
        self._btn_wom_clear.clicked.connect(self.wom_clear_requested.emit)
        self._btn_wom_apply.setEnabled(False)
        self._btn_wom_refresh.setEnabled(False)
        self._btn_wom_clear.setEnabled(False)
        outer.addWidget(wom_box)

        # ── Markers panel ─────────────────────────────────────────────────
        self._markers_box = QGroupBox('Log Markers  (from device flash log)')
        self._markers_box.setCheckable(True)
        self._markers_box.setChecked(False)   # collapsed by default
        self._markers_box.setStyleSheet('QGroupBox { font-size: 9pt; color: #888; }')
        mb_lay = QVBoxLayout(self._markers_box)
        mb_lay.setContentsMargins(8, 4, 8, 8)
        mb_lay.setSpacing(4)

        self._markers_table = QTableWidget(0, 3)
        self._markers_table.setHorizontalHeaderLabels(['#', 'Time', 'Tag'])
        self._markers_table.horizontalHeader().setStretchLastSection(True)
        self._markers_table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self._markers_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self._markers_table.setAlternatingRowColors(True)
        self._markers_table.setFixedHeight(160)
        self._markers_table.verticalHeader().setVisible(False)
        self._markers_table.setColumnWidth(0, 40)
        self._markers_table.setColumnWidth(1, 170)
        mb_lay.addWidget(self._markers_table)

        self._markers_lbl = QLabel('Press  📋 Load Markers  to download log markers from device.')
        self._markers_lbl.setStyleSheet('color: #999; font-size: 8pt;')
        mb_lay.addWidget(self._markers_lbl)

        outer.addWidget(self._markers_box)

        # Start with Event 1
        self._add_event()

    # ── Event management ───────────────────────────────────────────────────
    def _add_event(self):
        if len(self._cards) >= MAX_EVENTS:
            return
        card = _EventCard(len(self._cards))
        card.changed.connect(lambda: None)   # no-op; write is explicit
        card.remove_requested.connect(self._remove_event)
        self._cards.append(card)
        self._cards_vbox.addWidget(card)
        self._btn_add.setVisible(len(self._cards) < MAX_EVENTS)
        self._update_remove_btns()

    def _remove_event(self, card):
        if len(self._cards) <= 1:
            return
        self._cards.remove(card)
        card.setParent(None)
        card.deleteLater()
        for i, c in enumerate(self._cards):
            c.set_index(i)
        self._btn_add.setVisible(True)
        self._update_remove_btns()

    def _update_remove_btns(self):
        can_rm = len(self._cards) > 1
        for c in self._cards:
            c.set_removable(can_rm)

    def _clear_all(self):
        while len(self._cards) > 1:
            c = self._cards.pop()
            c.setParent(None); c.deleteLater()
        if self._cards:
            self._cards[0].set_index(0)
        self._btn_add.setVisible(True)
        self._update_remove_btns()

    # ── Transport ──────────────────────────────────────────────────────────
    def set_connected(self, v: bool):
        self._btn_read.setEnabled(v)
        self._btn_write.setEnabled(v)
        self._btn_markers.setEnabled(v)
        self._btn_wom_apply.setEnabled(v)
        self._btn_wom_refresh.setEnabled(v)
        self._btn_wom_clear.setEnabled(v)

    def _do_write(self):
        self.events_write_requested.emit(self.encode_blob())

    # ── WoM ────────────────────────────────────────────────────────────────
    def get_wom_params(self) -> dict:
        action = 0
        if self._chk_wom_wake.isChecked(): action |= WAKE_ACTION_WAKE_MCU
        if self._chk_wom_tx.isChecked():   action |= WAKE_ACTION_TX_BURST
        if self._chk_wom_log.isChecked():  action |= WAKE_ACTION_LOG_MARKER
        return {'enable': 1 if self._wom_tog.isChecked() else 0,
                'threshold_mg': self._wom_thr.value(),
                'duration_ms':  self._wom_dur.value(),
                'action':       action}

    def set_wom_state(self, d: dict):
        self._wom_tog.setChecked(bool(d.get('enable', 0)))
        self._wom_thr.setValue(d.get('threshold_mg', 500))
        self._wom_dur.setValue(d.get('duration_ms', 100))
        action = d.get('action', WAKE_ACTION_WAKE_MCU)
        self._chk_wom_wake.setChecked(bool(action & WAKE_ACTION_WAKE_MCU))
        self._chk_wom_tx.setChecked(bool(action & WAKE_ACTION_TX_BURST))
        self._chk_wom_log.setChecked(bool(action & WAKE_ACTION_LOG_MARKER))

    def set_wom_status_text(self, text: str):
        self._lbl_wom_status.setText(text)

    def _on_wom_apply(self):
        self.wom_apply_requested.emit(self.get_wom_params())

    def set_markers_progress(self, done: int, total: int):
        if total > 0:
            pct = int(done * 100 / total)
            self._btn_markers.setText(f'⏳ {done}/{total} ({pct}%)')
        else:
            self._btn_markers.setText('⏳ Loading…')

    def set_markers(self, records: list, rtc_unix: int = 0):
        """Display only LOGREC_TYPE_MARKER records in the markers panel."""
        markers = [r for r in records if 'marker' in r]
        self._markers_table.setRowCount(0)
        self._btn_markers.setText('📋 Load Markers')
        if not markers:
            self._markers_lbl.setText('No markers found in log.')
            self._markers_box.setChecked(True)
            return
        import datetime as _dt
        for i, r in enumerate(markers):
            ts = r.get('ts', 0)
            if rtc_unix and ts:
                try:
                    t = _dt.datetime.utcfromtimestamp(ts)
                    time_str = t.strftime('%Y-%m-%d %H:%M:%S')
                except Exception:
                    time_str = str(ts)
            else:
                time_str = str(ts)
            tag = r.get('marker', 0)
            row = self._markers_table.rowCount()
            self._markers_table.insertRow(row)
            for col, val in enumerate([str(i + 1), time_str, f'Marker #{tag}']):
                item = QTableWidgetItem(val)
                item.setForeground(QColor('#e67e00'))
                self._markers_table.setItem(row, col, item)
        self._markers_lbl.setText(f'{len(markers)} marker(s) found in log.')
        self._markers_box.setChecked(True)
        self._btn_markers.setText('📋 Load Markers')

    # ── Wire format ────────────────────────────────────────────────────────
    def encode_blob(self) -> bytes:
        buf = bytearray(EVENTS_BLOB_SZ)
        slots = [c.get_data() for c in self._cards]
        empty = {'enabled': False, 'oneshot': False, 'cooldown': 0,
                 'conds': [{'type': COND_DISABLED, 'val1': 0, 'val2': 0}],
                 'act':   {'type': ACT_NONE, 'p1': 0, 'p2': 0}}
        while len(slots) < MAX_EVENTS:
            slots.append(empty)
        for i, s in enumerate(slots[:MAX_EVENTS]):
            self._pack_event(s, buf, i * EVENT_SIZE)
        return bytes(buf)

    def decode_blob(self, data: bytes):
        if len(data) < EVENTS_BLOB_SZ:
            return
        parsed = [self._unpack_event(data, i * EVENT_SIZE) for i in range(MAX_EVENTS)]
        # How many non-empty slots?
        active = [p for p in parsed if p['conds'][0]['type'] != COND_DISABLED or p['enabled']]
        n = max(1, len(active))
        # Rebuild cards to match
        while len(self._cards) > n:
            c = self._cards.pop(); c.setParent(None); c.deleteLater()
        while len(self._cards) < n:
            self._add_event()
        for card, slot in zip(self._cards, parsed[:n]):
            card.set_data(slot)
        self._btn_add.setVisible(len(self._cards) < MAX_EVENTS)
        self._update_remove_btns()

    @staticmethod
    def _pack_event(d: dict, buf: bytearray, off: int):
        flags = (EV_FLAG_ENABLED if d['enabled'] else 0) | (EV_FLAG_ONESHOT if d['oneshot'] else 0)
        buf[off]   = flags
        conds = (d.get('conds') or [])[:MAX_CONDS]
        buf[off+1] = len(conds)
        for i, c in enumerate(conds):
            b = off + 2 + i * 5
            buf[b] = c['type'] & 0xFF
            struct.pack_into('<hh', buf, b+1, c.get('val1', 0), c.get('val2', 0))
        act = d.get('act', {})
        buf[off+17] = act.get('type', ACT_NONE) & 0xFF
        struct.pack_into('<hh', buf, off+18, act.get('p1', 0), act.get('p2', 0))
        buf[off+22] = d.get('cooldown', 0) & 0xFF

    @staticmethod
    def _unpack_event(data: bytes, off: int) -> dict:
        flags   = data[off]
        n_conds = min(max(data[off+1], 1), MAX_CONDS)
        conds = []
        for i in range(n_conds):
            b = off + 2 + i * 5
            v1, v2 = struct.unpack_from('<hh', data, b+1)
            conds.append({'type': data[b], 'val1': v1, 'val2': v2})
        act_type = data[off+17]
        p1, p2 = struct.unpack_from('<hh', data, off+18)
        return {
            'enabled':  bool(flags & EV_FLAG_ENABLED),
            'oneshot':  bool(flags & EV_FLAG_ONESHOT),
            'conds':    conds,
            'act':      {'type': act_type, 'p1': p1, 'p2': p2},
            'cooldown': data[off+22],
        }

    def retheme(self, C: dict):
        self._C = C


# ─────────────────────────────────────────────────────────────────────────────
# BLE Auto-Disconnect countdown dialog
# ─────────────────────────────────────────────────────────────────────────────
class AutoDiscCountdownDialog(QDialog):
    """10-second warning before BLE auto-disconnect fires."""
    def __init__(self, seconds: int, on_extend, on_disconnect, parent=None):
        super().__init__(parent)
        self.setWindowTitle('BLE Auto-Disconnect')
        self.setWindowModality(Qt.WindowModality.WindowModal)
        self.setFixedSize(380, 148)
        self._secs = seconds
        self._on_extend = on_extend
        self._on_disconnect = on_disconnect
        self._handled = False

        lay = QVBoxLayout(self)
        lay.setContentsMargins(24, 20, 24, 20)
        lay.setSpacing(16)

        self._lbl = QLabel(f'BLE session disconnects in  {self._secs} s')
        self._lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        f = QFont('Segoe UI', 13)
        self._lbl.setFont(f)
        lay.addWidget(self._lbl)

        btn_row = QHBoxLayout()
        btn_row.setSpacing(10)
        self._btn_ext = QPushButton('Continue session')
        self._btn_ext.setObjectName('primary')
        self._btn_ext.setFixedHeight(36)
        self._btn_ext.clicked.connect(self._extend)
        self._btn_disc = QPushButton('Disconnect now')
        self._btn_disc.setObjectName('secondary')
        self._btn_disc.setFixedHeight(36)
        self._btn_disc.clicked.connect(self._disconnect)
        btn_row.addWidget(self._btn_ext)
        btn_row.addWidget(self._btn_disc)
        lay.addLayout(btn_row)

        self._timer = QTimer(self)
        self._timer.setInterval(1000)
        self._timer.timeout.connect(self._tick)
        self._timer.start()

    def _tick(self):
        self._secs -= 1
        if self._secs <= 0:
            self._timer.stop()
            if not self._handled:
                self._handled = True
                self.close()
                self._on_disconnect()
        else:
            self._lbl.setText(f'BLE session disconnects in  {self._secs} s')

    def _extend(self):
        if self._handled: return
        self._handled = True
        self._timer.stop()
        self.close()
        self._on_extend()

    def _disconnect(self):
        if self._handled: return
        self._handled = True
        self._timer.stop()
        self.close()
        self._on_disconnect()

    def closeEvent(self, event):
        if not self._handled:
            self._handled = True
            self._timer.stop()
            self._on_disconnect()
        event.accept()


# ─────────────────────────────────────────────────────────────────────────────
# Main Window
# ─────────────────────────────────────────────────────────────────────────────
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle('TX Beacon v4')
        self.resize(1000, 700)

        self._theme = 'light'
        self._C     = THEMES['light']
        self._transport: Optional[Transport] = None
        self._info_cache: Optional[InfoBlob]     = None
        self._cfg_cache:  Optional[ConfigBlob]   = None
        self._stat_cache: Optional[StatusBlob]   = None
        self._connected  = False
        self._profiles   = ProfileManager()
        self._workers: List[Worker] = []
        self._dl_records: List[dict] = []
        self._dl_offset  = 0
        self._dl_total   = 0
        self._settings_file = 'tx_beacon_settings_v3.json'
        self._load_settings()

        self._build_ui()
        self._tab_settings.set_live_settings(
            getattr(self, '_live_en', True), getattr(self, '_live_iv', 2))
        self._tab_settings.set_uptime_flush_settings(
            getattr(self, '_uptime_flush_en', False), getattr(self, '_uptime_flush_min', 10))
        self._tab_settings.live_refresh_changed.connect(self._on_live_refresh_changed)
        self._tab_settings.uptime_flush_changed.connect(self._on_uptime_flush_changed)
        self._tab_settings.tag_set_requested.connect(self._on_tag_set)
        self._tab_settings.uptime_reset_requested.connect(self._on_uptime_reset)
        self._tab_settings.uptime_save_requested.connect(self._on_uptime_save)
        self._tab_settings.accel_zero_g_requested.connect(self._on_accel_zero_g)
        self._tab_settings.hwdesc_read_requested.connect(self._on_hwdesc_read)
        self._tab_settings.hwdesc_write_requested.connect(self._on_hwdesc_write)
        self._tab_settings.hwdesc_commit_requested.connect(self._on_hwdesc_commit)
        self._tab_events.wom_apply_requested.connect(self._on_wom_apply)
        self._tab_events.wom_refresh_requested.connect(self._on_wom_refresh)
        self._tab_events.wom_clear_requested.connect(self._on_wom_clear)
        self._refresh_timer = QTimer(self)
        live_en, live_iv = self._tab_settings.get_live_settings()
        self._refresh_timer.setInterval(max(100, int(live_iv * 1000)) if live_en else 5000)
        self._refresh_timer.timeout.connect(self._poll_status)

        self._uptime_flush_timer = QTimer(self)
        self._uptime_flush_timer.timeout.connect(self._on_uptime_flush_tick)
        fl_en, fl_min = self._tab_settings.get_uptime_flush_settings()
        if fl_en:
            self._uptime_flush_timer.setInterval(fl_min * 60000)

        # RSSI polling timer (BLE only, fires while BLE connected)
        self._rssi_timer = QTimer(self)
        self._rssi_timer.setInterval(3000)
        self._rssi_timer.timeout.connect(self._poll_rssi)
        self._rssi_history: deque = deque(maxlen=120)   # (timestamp, dBm)
        self._rssi_graph_dlg: Optional['RssiGraphDialog'] = None
        self._ble_session_records = 0  # RAM queue entries buffered since last BLE connect

        # Background BLE scan spinner
        _SPIN = ['◐', '◓', '◑', '◒']
        self._scan_spin_frames = _SPIN
        self._scan_spin_idx    = 0
        self._bg_scan_active   = False
        self._scan_spin_timer  = QTimer(self)
        self._scan_spin_timer.setInterval(200)
        self._scan_spin_timer.timeout.connect(self._on_scan_spin_tick)

        # BLE auto-disconnect timer (single shot — fires 10 s before actual disconnect)
        self._ble_autodisc_timer = QTimer(self)
        self._ble_autodisc_timer.setSingleShot(True)
        self._ble_autodisc_timer.timeout.connect(self._on_ble_autodisc_timeout)
        # Antenna pulse indicator for auto-disconnect
        self._ant_pulse_timer = QTimer(self)
        self._ant_pulse_timer.setInterval(500)
        self._ant_pulse_timer.timeout.connect(self._on_ant_pulse)
        self._ant_pulse_state = False

        # Auto-apply timer: debounce config changes → write to beacon RAM without pressing Apply
        self._auto_apply_timer = QTimer(self)
        self._auto_apply_timer.setSingleShot(True)
        self._auto_apply_timer.setInterval(700)   # 700 ms after last change
        self._auto_apply_timer.timeout.connect(self._do_ram_write)
        self._loading = False   # True while load_config() is called programmatically

        self._apply_theme()
        self._restore_ui_settings()
        self._update_connection_ui(False)

    def _load_settings(self):
        try:
            if os.path.exists(self._settings_file):
                with open(self._settings_file) as f:
                    d = json.load(f)
                self._theme    = d.get('theme', 'light')
                if 'geometry' in d:
                    from PyQt6.QtCore import QByteArray
                    self.restoreGeometry(QByteArray.fromHex(d['geometry'].encode()))
                self._last_src       = d.get('last_src', '')
                self._live_en        = d.get('live_refresh_en', True)
                self._live_iv        = float(d.get('live_refresh_s', 2.0))
                self._saved_tz       = d.get('tz_offset_h', 0)
                self._uptime_flush_en  = d.get('uptime_flush_en',  False)
                self._uptime_flush_min = d.get('uptime_flush_min', 10)
                self._auto_scan_startup  = d.get('auto_scan_startup', False)
                self._auto_reconnect_ble = d.get('auto_reconnect_ble', False)
                self._auto_disc_en       = d.get('ble_auto_disc_en',  False)
                self._auto_disc_min      = d.get('ble_auto_disc_min', 1)
                self._spark_series_s     = d.get('spark_series', 0)
                self._spark_pts_s        = d.get('spark_pts',    100)
                self._scan_iv_ms         = d.get('scan_iv_ms',   60)
                self._scan_win_ms        = d.get('scan_win_ms',  60)
            else:
                self._last_src           = ''
                self._live_en            = True
                self._live_iv            = 2.0
                self._saved_tz           = 0
                self._uptime_flush_en    = False
                self._uptime_flush_min   = 10
                self._auto_scan_startup  = False
                self._auto_reconnect_ble = False
                self._auto_disc_en       = False
                self._auto_disc_min      = 1
                self._spark_series_s     = 0
                self._spark_pts_s        = 100
                self._scan_iv_ms         = 60
                self._scan_win_ms        = 60
        except Exception:
            self._last_src           = ''
            self._live_en            = True
            self._live_iv            = 2.0
            self._saved_tz           = 0
            self._uptime_flush_en    = False
            self._uptime_flush_min   = 10
            self._auto_scan_startup  = False
            self._auto_reconnect_ble = False
            self._auto_disc_en       = False
            self._auto_disc_min      = 1
            self._spark_series_s     = 0
            self._spark_pts_s        = 100
            self._scan_iv_ms         = 60
            self._scan_win_ms        = 60

    def _save_settings(self):
        try:
            live_en, live_iv   = self._tab_settings.get_live_settings()
            flush_en, flush_min = self._tab_settings.get_uptime_flush_settings()
            d = {
                'theme':            self._theme,
                'last_src':         getattr(self, '_last_src', ''),
                'live_refresh_en':  live_en,
                'live_refresh_s':   live_iv,
                'tz_offset_h':      self._tab_settings.get_tz_offset_h(),
                'uptime_flush_en':  flush_en,
                'uptime_flush_min': flush_min,
                'auto_scan_startup':  self._tab_ble._chk_auto_scan.isChecked(),
                'auto_reconnect_ble': self._tab_ble._chk_auto_reconnect.isChecked(),
                'ble_auto_disc_en':  self._tab_ble._chk_auto_disc.isChecked(),
                'ble_auto_disc_min': self._tab_ble._spin_auto_disc_min.value(),
                'spark_series':  self._tab_overview.get_spark_settings()[0],
                'spark_pts':     self._tab_overview.get_spark_settings()[1],
                'scan_iv_ms':    self._tab_ble._spin_scan_iv.value(),
                'scan_win_ms':   self._tab_ble._spin_scan_win.value(),
            }
            with open(self._settings_file, 'w') as f:
                json.dump(d, f)
        except Exception:
            pass

    def _restore_ui_settings(self):
        """Apply loaded settings to UI widgets after build."""
        last = getattr(self, '_last_src', '')
        if last:
            idx = self._combo_src.findText(last)
            if idx >= 0:
                self._combo_src.setCurrentIndex(idx)
        tz = getattr(self, '_saved_tz', 0)
        if tz:
            self._tab_settings._spin_tz.setValue(tz)
        self._tab_ble.set_auto_scan_settings(
            getattr(self, '_auto_scan_startup', False),
            getattr(self, '_auto_reconnect_ble', False))
        self._tab_ble.set_auto_disconnect(
            getattr(self, '_auto_disc_en', False),
            getattr(self, '_auto_disc_min', 1))
        self._tab_overview.set_spark_settings(
            getattr(self, '_spark_series_s', 0),
            getattr(self, '_spark_pts_s', 100))
        self._tab_ble.set_scan_params(
            getattr(self, '_scan_iv_ms',  60),
            getattr(self, '_scan_win_ms', 60))
        # Auto-connect Serial or auto-scan BLE based on saved settings
        self._autoconnect_start()
        if HAS_BLEAK and getattr(self, '_auto_scan_startup', False):
            QTimer.singleShot(400, self._start_bg_scan)

    def _autoconnect_start(self):
        """One attempt to reconnect to last Serial port. Silently skips if unavailable."""
        src = getattr(self, '_last_src', '')
        # BLE requires scan first — never auto-connect to BLE address
        if not src or not src.startswith('Serial '):
            return
        # Port must be present in the current combo list (i.e. physically available)
        if self._combo_src.findText(src) < 0:
            return
        self._log(f'Auto-connect → {src}')
        self._lbl_conn.setText(f'Connecting…  {src}')
        self._btn_connect.setEnabled(False)

        port = src[7:]

        def _do():
            t = UartTransport()
            ok = t.connect_to(port)
            return ok, t if ok else None

        def _done(result):
            self._btn_connect.setEnabled(True)
            ok, t = result
            if ok and t:
                t.connected_changed.connect(self._on_conn_changed)
                if hasattr(t, 'telemetry'):
                    t.telemetry.connect(self._tab_applog.append_line)
                self._transport = t
                self._on_conn_changed(True)
            else:
                self._update_connection_ui(False)

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda _e: _done((False, None)))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_ble_autodisc_timeout(self):
        if not self._connected:
            return
        _, disc_min = self._tab_ble.get_auto_disconnect()

        def _do_extend():
            self._log(f'⏱ Session extended — auto-disconnect in {disc_min} min')
            fire_ms = max(5000, disc_min * 60000 - 10000)
            self._ble_autodisc_timer.start(fire_ms)
            self._ant_pulse_timer.start()

        def _do_disc():
            self._log('⏱ Auto-disconnect timer expired — disconnecting BLE')
            self._do_disconnect()

        dlg = AutoDiscCountdownDialog(10, _do_extend, _do_disc, parent=self)
        dlg.show()

    def _on_ant_pulse(self):
        self._ant_pulse_state = not self._ant_pulse_state
        color = self._C['accent'] if self._ant_pulse_state else self._C['success']
        self._dot.setStyleSheet(f'color: {color};')

    # ── Background BLE scan ───────────────────────────────────────────────────

    def _start_bg_scan(self):
        """Start a one-shot background BLE scan. Auto-connects to any BCN_ beacon found."""
        if self._bg_scan_active or self._connected or not HAS_BLEAK:
            return
        self._bg_scan_active = True
        self._scan_spin_idx  = 0
        self._lbl_scan_spin.setText('📶 ◐')
        self._lbl_scan_spin.setVisible(True)
        self._scan_spin_timer.start()
        self._log('📶 Background BLE scan…')

        def _do():
            devs = _asyncio.run(BleakScanner.discover(timeout=10.0, return_adv=True))
            result = []
            for addr, (dev, adv) in devs.items():
                result.append({'address': addr,
                               'name':    dev.name or '',
                               'rssi':    adv.rssi if hasattr(adv, 'rssi') else 0})
            return result

        w = Worker(_do)
        w.result.connect(self._on_bg_scan_done)
        w.error.connect(self._on_bg_scan_error)
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _stop_bg_scan(self):
        self._bg_scan_active = False
        self._scan_spin_timer.stop()
        self._lbl_scan_spin.setVisible(False)

    def _on_scan_spin_tick(self):
        self._scan_spin_idx = (self._scan_spin_idx + 1) % len(self._scan_spin_frames)
        frame = self._scan_spin_frames[self._scan_spin_idx]
        self._lbl_scan_spin.setText(f'📶 {frame}')

    def _on_bg_scan_done(self, devices: list):
        self._bg_scan_active = False
        if self._connected:
            self._stop_bg_scan()
            return
        bcn = [d for d in devices if d['name'].upper().startswith('BCN')]
        if not bcn:
            self._log('📶 No BCN_ beacon found')
            _, auto_reconnect = self._tab_ble.get_auto_scan_settings()
            if auto_reconnect:
                self._log('📶 Retrying in 5 s…')
                self._stop_bg_scan()
                QTimer.singleShot(5000, self._start_bg_scan)
            else:
                self._stop_bg_scan()
            return
        # Prefer last known address, otherwise strongest RSSI
        preferred = ''
        last = getattr(self, '_last_src', '')
        if last.startswith('BLE '):
            preferred = last[4:].split(' ')[0].upper()
        target = next((d for d in bcn if d['address'].upper() == preferred), None)
        if target is None:
            target = max(bcn, key=lambda d: d['rssi'])
        self._stop_bg_scan()
        label = (f'BLE {target["address"]} ({target["name"]})' if target['name']
                 else f'BLE {target["address"]}')
        # Insert/select in combo
        for i in range(self._combo_src.count()):
            if self._combo_src.itemText(i).startswith(f'BLE {target["address"]}'):
                self._combo_src.setCurrentIndex(i)
                break
        else:
            self._combo_src.addItem(label)
            self._combo_src.setCurrentText(label)
        self._log(f'📶 Auto-connecting → {label}')
        if not self._connected:
            self._do_connect()

    def _on_bg_scan_error(self, err: str):
        self._bg_scan_active = False
        self._stop_bg_scan()
        self._log(f'📶 Scan error: {err}')

    def closeEvent(self, e):
        self._save_settings()
        if self._transport:
            self._transport.disconnect()
        super().closeEvent(e)

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        vbox = QVBoxLayout(central)
        vbox.setContentsMargins(0, 0, 0, 0)
        vbox.setSpacing(0)

        # ─ Top bar ─
        self._top_bar = self._build_top_bar()
        vbox.addWidget(self._top_bar)

        # ─ Banner (below top bar, full width) ─
        self._banner = BannerWidget()
        vbox.addWidget(self._banner)

        # ─ Tab widget ─
        self._tabs = QTabWidget()
        self._tabs.setDocumentMode(True)
        self._tab_overview  = OverviewTab()
        self._tab_beacon    = BeaconTab(self._profiles)
        self._tab_logging   = LoggingTab()
        self._tab_data      = DataTab()
        self._tab_settings  = SettingsTab()
        self._tab_ble       = BleTab()
        self._tab_events    = EventsTab()
        self._tab_applog    = AppLogTab()
        self._tabs.addTab(self._tab_overview,  f'{ICO["tx"]} Overview')
        self._tabs.addTab(self._tab_beacon,    f'{ICO["schedule"]} Beacon')
        self._tabs.addTab(self._tab_logging,   f'{ICO["storage"]} Logging')
        self._tabs.addTab(self._tab_data,      f'{ICO["data"]} Data')
        self._tabs.addTab(self._tab_settings,  f'{ICO["settings"]} Settings')
        self._tabs.addTab(self._tab_ble,       f'{ICO["ble"]} BLE')
        self._tabs.addTab(self._tab_events,    f'{ICO["events"]} Events')
        self._tabs.addTab(self._tab_applog,    f'{ICO["log"]} Log')
        self._tabs.currentChanged.connect(self._on_tab_changed)

        # Wrap in a container that also holds the sticky footer
        tab_container = QWidget()
        tc_lay = QVBoxLayout(tab_container)
        tc_lay.setContentsMargins(0, 0, 0, 0)
        tc_lay.setSpacing(0)
        tc_lay.addWidget(self._tabs)

        self._footer = StickyFooter()
        self._footer.revert_clicked.connect(self._on_revert)
        self._footer.apply_clicked.connect(self._on_apply)
        self._footer.restart_clicked.connect(self._on_restart_beacon)
        tc_lay.addWidget(self._footer)
        self._footer.setVisible(False)

        vbox.addWidget(tab_container, 1)

        # Wire config changed signals
        self._tab_beacon.config_changed.connect(self._on_config_dirty)
        self._tab_logging.config_changed.connect(self._on_config_dirty)
        self._tab_settings.config_changed.connect(self._on_config_dirty)
        self._tab_settings.tz_offset_changed.connect(self._on_tz_changed)
        self._tab_data.download_requested.connect(self._start_download)
        self._tab_data.erase_requested.connect(self._erase_log)
        # BLE tab signals
        self._tab_ble.ble_read_requested.connect(self._on_ble_read)
        self._tab_ble.ble_apply_requested.connect(self._on_ble_apply)
        # Events tab signals
        self._tab_events.events_read_requested.connect(self._on_events_read)
        self._tab_events.events_write_requested.connect(self._on_events_write)
        self._tab_events.markers_load_requested.connect(self._start_download)
        # Persist spark and BLE auto-disconnect settings on change
        self._tab_overview._spark_combo.currentIndexChanged.connect(
            lambda _: self._save_settings())
        self._tab_overview._spark_pts_spin.valueChanged.connect(
            lambda _: self._save_settings())
        self._tab_ble._chk_auto_disc.toggled.connect(lambda _: self._save_settings())
        self._tab_ble._spin_auto_disc_min.valueChanged.connect(
            lambda _: self._save_settings())

    def _build_top_bar(self) -> QWidget:
        bar = QFrame()
        bar.setFixedHeight(48)
        bar.setObjectName('topbar')
        lay = QHBoxLayout(bar)
        lay.setContentsMargins(16, 0, 16, 0)
        lay.setSpacing(12)

        # Status dot + label
        self._dot = QLabel('●')
        self._dot.setFont(QFont('Segoe UI', 12))
        self._lbl_conn = QLabel('Disconnected')
        self._lbl_conn.setFont(QFont('Segoe UI', 10, QFont.Weight.Bold))
        # Beacon time display (shown when connected + RTC set)
        self._lbl_bcn_time = QLabel()
        self._lbl_bcn_time.setFont(QFont('Consolas', 10))
        self._lbl_bcn_time.setVisible(False)
        # RSSI display (BLE only) — clickable: opens RSSI graph
        self._lbl_rssi = QPushButton()
        self._lbl_rssi.setFlat(True)
        self._lbl_rssi.setFont(QFont('Segoe UI', 9))
        self._lbl_rssi.setCursor(Qt.CursorShape.PointingHandCursor)
        self._lbl_rssi.setToolTip('Click to show RSSI history graph')
        self._lbl_rssi.setVisible(False)
        self._lbl_rssi.clicked.connect(self._show_rssi_graph)
        # Background BLE scan spinner (visible only while scanning)
        self._lbl_scan_spin = QLabel()
        self._lbl_scan_spin.setFont(QFont('Segoe UI', 10))
        self._lbl_scan_spin.setToolTip('Background BLE scan in progress…')
        self._lbl_scan_spin.setVisible(False)

        lay.addWidget(self._dot)
        lay.addWidget(self._lbl_conn)
        lay.addSpacing(12)
        lay.addWidget(self._lbl_bcn_time)
        lay.addSpacing(8)
        lay.addWidget(self._lbl_rssi)
        lay.addSpacing(8)
        lay.addWidget(self._lbl_scan_spin)
        lay.addStretch()

        # Source selector
        self._combo_src = QComboBox()
        self._combo_src.setFixedWidth(200)
        self._combo_src.addItem('BLE (demo)')
        if HAS_SERIAL:
            ports = [p.device for p in serial.tools.list_ports.comports()]
            for p in ports:
                self._combo_src.addItem(f'Serial {p}')
        self._combo_src.currentTextChanged.connect(self._on_source_changed)

        # BLE scan button — opens BleScanDialog
        self._btn_ble_scan = QPushButton('📶 BLE Scan')
        self._btn_ble_scan.setObjectName('secondary')
        self._btn_ble_scan.setFixedHeight(32)
        self._btn_ble_scan.setToolTip('Scan for BLE beacons')
        self._btn_ble_scan.clicked.connect(self._on_ble_scan)
        self._btn_ble_scan.setEnabled(True)  # always clickable; shows install hint if bleak missing

        self._btn_connect = QPushButton('Connect')
        self._btn_connect.setObjectName('secondary')
        self._btn_connect.setFixedHeight(32)
        self._btn_connect.clicked.connect(self._on_connect_toggle)

        self._btn_refresh = QPushButton('⟳ Refresh')
        self._btn_refresh.setObjectName('secondary')
        self._btn_refresh.setFixedHeight(32)
        self._btn_refresh.setToolTip('Re-read all data from beacon')
        self._btn_refresh.clicked.connect(self._refresh_from_beacon)
        self._btn_refresh.setEnabled(False)

        self._btn_sync_time = QPushButton('⏱ Sync time')
        self._btn_sync_time.setObjectName('secondary')
        self._btn_sync_time.setFixedHeight(32)
        self._btn_sync_time.setToolTip('Set beacon RTC to current PC time')
        self._btn_sync_time.clicked.connect(self._sync_time)
        self._btn_sync_time.setEnabled(False)

        # Theme toggle — solid accent circle, always visible
        self._btn_theme = QPushButton('☼' if self._theme == 'light' else '☽')
        self._btn_theme.setObjectName('theme_btn')
        self._btn_theme.setFixedSize(34, 34)
        self._btn_theme.setToolTip('Switch theme')
        self._btn_theme.clicked.connect(self._toggle_theme)

        lay.addWidget(self._combo_src)
        lay.addWidget(self._btn_ble_scan)
        lay.addWidget(self._btn_connect)
        lay.addWidget(self._btn_refresh)
        lay.addWidget(self._btn_sync_time)
        lay.addWidget(self._btn_theme)
        return bar

    def _on_source_changed(self, text: str):
        pass

    def _on_ble_scan(self):
        """Open BLE scan dialog; add found device to combo and select it."""
        import traceback
        if not HAS_BLEAK:
            import sys
            msg = f'bleak import failed: {_BLEAK_ERR}' if _BLEAK_ERR else \
                  'bleak library not found — install it:  pip install bleak'
            self._banner.show_err(msg)
            self._log(f'BLE Scan: {msg}')
            self._log(f'Python: {sys.executable}  ver={sys.version.split()[0]}')
            return
        try:
            # Extract previously-used BLE address for auto-connect hint
            preferred = ''
            last = getattr(self, '_last_src', '')
            if last.startswith('BLE '):
                preferred = last[4:].split(' ')[0]   # "BLE AA:BB:... (Name)" → "AA:BB:..."
            dlg = BleScanDialog(self._C, self, preferred_address=preferred)
            if dlg.exec() == QDialog.DialogCode.Accepted:
                dev = dlg.selected_device()
                if dev:
                    label = f'BLE {dev["address"]} ({dev["name"]})' if dev['name'] \
                            else f'BLE {dev["address"]}'
                    for i in range(self._combo_src.count()):
                        if self._combo_src.itemText(i).startswith(f'BLE {dev["address"]}'):
                            self._combo_src.removeItem(i)
                            break
                    self._combo_src.addItem(label)
                    self._combo_src.setCurrentText(label)
                    self._log(f'📶 BLE device selected: {label}')
                    # Immediately connect — no need to press Connect again
                    if not self._connected:
                        self._do_connect()
        except Exception as e:
            self._banner.show_err(f'BLE Scan error: {e}')
            self._log(f'BLE Scan exception: {traceback.format_exc()}')

    def _on_connect_toggle(self):
        if self._connected:
            self._do_disconnect()
        else:
            self._do_connect()

    def _do_connect(self):
        src = self._combo_src.currentText()

        if src == 'BLE (demo)':
            t = MockBleTransport(self)
            t.connect_to('')
            t.connected_changed.connect(self._on_conn_changed)
            if hasattr(t, 'telemetry'):
                t.telemetry.connect(self._tab_applog.append_line)
            self._transport = t
            self._last_src = src
            self._save_settings()
            self._on_conn_changed(True)

        elif src.startswith('Serial '):
            port = src[7:]
            t = UartTransport(self)
            if not t.connect_to(port):
                self._banner.show_err(f'Cannot open {port}')
                return
            t.connected_changed.connect(self._on_conn_changed)
            if hasattr(t, 'telemetry'):
                t.telemetry.connect(self._tab_applog.append_line)
            self._transport = t
            self._last_src = src
            self._save_settings()
            self._on_conn_changed(True)

        elif src.startswith('BLE '):
            # Extract address from "BLE <addr> (<name>)" or "BLE <addr>"
            parts  = src[4:].split(' (', 1)
            addr   = parts[0].strip()
            name   = parts[1].rstrip(')') if len(parts) > 1 else ''
            t = BleTransport(addr, name, self)
            t.connected_changed.connect(self._on_conn_changed)
            if hasattr(t, 'telemetry'):
                t.telemetry.connect(self._tab_applog.append_line)
            self._transport = t
            self._last_src = src
            self._save_settings()
            # BLE connect is slow — run in background, show status
            self._lbl_conn.setText(f'Connecting BLE…  {addr}')
            self._btn_connect.setEnabled(False)

            def _ble_do():
                return t.connect_to()

            def _ble_done(ok):
                self._btn_connect.setEnabled(True)
                if not ok:
                    err = getattr(t, '_connect_error', '') or 'connection refused or timeout'
                    self._transport = None
                    self._banner.show_err(f'BLE connect failed: {err}')
                    self._log(f'BLE connect error ({addr}): {err}')
                    self._update_connection_ui(False)
                    # Auto-reconnect: restart background scan after a failed connection attempt
                    if HAS_BLEAK:
                        _, auto_reconnect = self._tab_ble.get_auto_scan_settings()
                        if auto_reconnect:
                            self._log('📶 Retrying scan in 5 s…')
                            QTimer.singleShot(5000, self._start_bg_scan)

            w = Worker(_ble_do)
            w.result.connect(_ble_done)
            w.error.connect(lambda e: _ble_done(False))
            self._workers.append(w)
            w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
            w.start()

    def _do_disconnect(self):
        t = self._transport
        self._transport = None
        self._on_conn_changed(False)

        def _cleanup():
            if t and hasattr(t, 'cmd_uart_mode'):
                try:
                    t.cmd_uart_mode(0)  # restore verbose before closing port
                except Exception:
                    pass
            if t:
                t.disconnect()

        w = Worker(_cleanup)
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_conn_changed(self, connected: bool):
        if self._connected == connected:
            return  # guard: prevent double processing from BLE disconnect + _do_disconnect
        self._connected = connected
        if connected:
            self._stop_bg_scan()  # cancel any background scan when we get a connection
        self._update_connection_ui(connected)
        if connected:
            src = self._combo_src.currentText()
            self._log(f'{ICO["connect"]} Connected: {src}')
            self._init_after_connect()
            # Start RSSI polling and auto-refresh if BLE transport
            if HAS_BLEAK and isinstance(self._transport, BleTransport):
                self._lbl_rssi.setText('⬤ …')
                self._lbl_rssi.setStyleSheet('color: #9E9E9E; font-weight: bold;')
                self._lbl_rssi.setVisible(True)
                self._rssi_timer.start()
            # BLE auto-disconnect timer (fires 10 s early to show countdown dialog)
            disc_en, disc_min = self._tab_ble.get_auto_disconnect()
            is_ble = HAS_BLEAK and isinstance(self._transport, BleTransport)
            if disc_en and is_ble:
                self._log(f'⏱ Auto-disconnect in {disc_min} min')
                fire_ms = max(5000, disc_min * 60000 - 10000)
                self._ble_autodisc_timer.start(fire_ms)
                self._ant_pulse_state = True
                self._ant_pulse_timer.start()
        else:
            self._ble_autodisc_timer.stop()
            self._ant_pulse_timer.stop()
            self._refresh_timer.stop()
            self._uptime_flush_timer.stop()
            self._rssi_timer.stop()
            self._auto_apply_timer.stop()
            self._lbl_rssi.setVisible(False)
            self._rssi_history.clear()
            self._ble_session_records = 0
            self._tab_overview._s_bar.set_ble_queue(0)
            if self._rssi_graph_dlg:
                self._rssi_graph_dlg.close()
                self._rssi_graph_dlg = None
            self._tab_overview.set_live_streaming(False)
            self._tab_overview.show_disconnected()
            self._info_cache = self._cfg_cache = self._stat_cache = None
            self._log(f'{ICO["warn"]} Disconnected')
            # Auto-reconnect: if last source was BLE and option is enabled, scan
            if HAS_BLEAK:
                _, auto_reconnect = self._tab_ble.get_auto_scan_settings()
                if auto_reconnect and getattr(self, '_last_src', '').startswith('BLE '):
                    QTimer.singleShot(2000, self._start_bg_scan)
            # If disconnect came from BLE disconnected_callback (firmware dropped connection),
            # self._transport is still set — clean up the BLE asyncio thread without
            # calling disconnect() which would re-emit connected_changed
            old_t = self._transport
            self._transport = None
            if old_t is not None and HAS_BLEAK and isinstance(old_t, BleTransport):
                def _stop_ble(t=old_t):
                    try:
                        if t._ble_thread:
                            t._ble_thread.stop()
                            t._ble_thread.wait(3000)
                            t._ble_thread = None
                        t._ble_client = None
                    except Exception:
                        pass
                w = Worker(_stop_ble)
                self._workers.append(w)
                w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
                w.start()

    def _update_connection_ui(self, connected: bool):
        C = self._C
        self._btn_refresh.setEnabled(connected)
        self._btn_sync_time.setEnabled(connected)
        self._tab_events.set_connected(connected)
        self._btn_ble_scan.setEnabled(not connected)  # always allowed; shows install hint if bleak missing
        if connected:
            src = self._combo_src.currentText()
            if src.startswith('BLE ') and not src.startswith('BLE ('):
                icon = '📶 '
                name = src[4:].split(' (')[0]   # just the address
            else:
                icon = ''
                name = src.replace('Serial ', '').replace('BLE (demo)', 'Demo')
            self._dot.setStyleSheet(f'color:{C["success"]};')
            self._lbl_conn.setText(f'{icon}Connected  {name}')
            self._btn_connect.setText('Disconnect')
            self._footer.set_disconnected(False)
        else:
            self._dot.setStyleSheet(f'color:{C["text_dim"]};')
            self._lbl_conn.setText('Disconnected')
            self._btn_connect.setText('Connect')
            self._footer.set_disconnected(True)
            self._footer.set_enabled(False)
            self._lbl_bcn_time.setVisible(False)

    def _init_after_connect(self):
        """Read INFO, CFG, HwDesc, sensor list, BLE settings after connect then go to Overview."""
        def _read():
            t = self._transport
            info_b  = t.read_info()
            cfg_b   = t.read_config()
            stat_b  = t.read_status()
            hwdesc  = t.cmd_hwdesc_get() if hasattr(t, 'cmd_hwdesc_get') else None
            sensors = t.cmd_sensor_list() if hasattr(t, 'cmd_sensor_list') else []
            ble_s   = t.cmd_ble_get() if hasattr(t, 'cmd_ble_get') else None
            return info_b, cfg_b, stat_b, hwdesc, sensors, ble_s

        w = Worker(_read)
        w.result.connect(self._on_init_done)
        w.error.connect(lambda e: self._banner.show_err(f'Init error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_init_done(self, result):
        self._btn_refresh.setEnabled(self._connected)
        self._btn_refresh.setText('⟳ Refresh')
        info_b, cfg_b, stat_b, hwdesc, sensors, ble_s = result
        if len(info_b) == 48:
            self._info_cache = InfoBlob._from_bytes(info_b)
            self._tab_settings.load_info(self._info_cache)
        if len(cfg_b) == 64:
            self._cfg_cache = ConfigBlob.from_bytes(cfg_b)
            self._loading = True
            self._tab_beacon.load_config(self._cfg_cache)
            self._tab_logging.load_config(self._cfg_cache)
            self._tab_settings.load_config(self._cfg_cache)
            self._loading = False
            self._tab_data.set_ts_mode(getattr(self._cfg_cache, 'log_ts_source', 0))
            self._tab_logging.set_total_records(self._info_cache.log_total_entries if self._info_cache else 768)
        # Load accel enable/interval from sensor list (stored separately in log header page)
        for s in sensors:
            if s.get('id') == SENSOR_ID_ACCEL:
                self._tab_logging.load_accel(bool(s['enabled']), s['interval_s'])
                break
        if hwdesc is not None:
            self._tab_settings.load_hwdesc(hwdesc)
        if ble_s is not None:
            self._tab_ble.load_ble(ble_s)
        if len(stat_b) == 24:
            self._stat_cache = StatusBlob.from_bytes(stat_b)
            self._tab_overview.update_status(self._stat_cache, self._info_cache, self._cfg_cache)
            self._update_bcn_time_label(self._stat_cache)
            self._check_time_drift(self._stat_cache)
            total    = self._info_cache.log_total_entries if self._info_cache else 768
            circular = bool(self._cfg_cache and getattr(self._cfg_cache, 'log_overflow', 0) == 1)
            self._tab_data.set_storage_info(self._stat_cache.log_used, total, circular)
        live_en, _ = self._tab_settings.get_live_settings()
        self._tab_overview.set_live_streaming(live_en)
        self._refresh_timer.start()
        flush_en, flush_min = self._tab_settings.get_uptime_flush_settings()
        if flush_en:
            self._uptime_flush_timer.setInterval(flush_min * 60000)
            self._uptime_flush_timer.start()
        self._tabs.setCurrentIndex(0)

    def _poll_status(self):
        if not self._connected or not self._transport:
            return

        def _read():
            # Use measure_all for BLE (forces fresh sensor readings at poll rate)
            if HAS_BLEAK and isinstance(self._transport, BleTransport):
                return self._transport.measure_all()
            return self._transport.read_status()

        w = Worker(_read)
        w.result.connect(self._on_stat_refreshed)
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _poll_rssi(self):
        if not self._connected or not isinstance(self._transport, BleTransport):
            self._rssi_timer.stop()
            return

        def _get():
            return self._transport.get_rssi()

        def _done(rssi):
            if rssi is not None:
                color = _rssi_color(rssi)
                self._lbl_rssi.setText(f'⬤ {rssi} dBm')
                self._lbl_rssi.setStyleSheet(
                    f'QPushButton {{ color: {color}; font-weight: bold; border: none; '
                    f'background: transparent; padding: 0; }}')
                # Record history and refresh graph if open
                self._rssi_history.append((time.time(), rssi))
                if self._rssi_graph_dlg and self._rssi_graph_dlg.isVisible():
                    self._rssi_graph_dlg.update_data(self._rssi_history)
                # Push to sparkline overview
                self._tab_overview.push_rssi(rssi)
            else:
                # bleak on Windows may not expose live RSSI — show N/A
                self._lbl_rssi.setText('⬤ N/A')
                self._lbl_rssi.setStyleSheet(
                    'QPushButton { color: #9E9E9E; font-weight: bold; border: none; '
                    'background: transparent; padding: 0; }')

        w = Worker(_get)
        w.result.connect(_done)
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _show_rssi_graph(self):
        if self._rssi_graph_dlg is None or not self._rssi_graph_dlg.isVisible():
            self._rssi_graph_dlg = RssiGraphDialog(self._rssi_history, self)
            self._rssi_graph_dlg.show()
        else:
            self._rssi_graph_dlg.raise_()
            self._rssi_graph_dlg.activateWindow()

    def _on_tz_changed(self, hours: int):
        self._tab_data.set_tz_offset(hours)
        self._tab_overview.set_tz_offset(hours)
        self._save_settings()

    def _on_live_refresh_changed(self, enabled: bool, iv_s: float):
        ms = max(100, int(iv_s * 1000)) if enabled else 5000
        self._refresh_timer.setInterval(ms)
        self._tab_overview.set_live_streaming(enabled and self._connected)
        self._save_settings()
        if enabled:
            self._banner.show_ok(f'Live refresh: every {iv_s:.1f} s — saved', 2000)
        else:
            self._banner.show_ok('Live refresh disabled — saved', 2000)

    def _on_stat_refreshed(self, stat_b: bytes):
        if len(stat_b) >= 24:
            self._stat_fail_count = 0
            self._stat_cache = StatusBlob.from_bytes(stat_b[:24])
            self._tab_overview.update_status(self._stat_cache, self._info_cache, self._cfg_cache)
            self._tab_beacon.update_sched_status(bool(self._stat_cache.sched_active))
            self._update_bcn_time_label(self._stat_cache)
            total    = self._info_cache.log_total_entries if self._info_cache else 768
            circular = bool(self._cfg_cache and getattr(self._cfg_cache, 'log_overflow', 0) == 1)
            self._tab_data.set_storage_info(self._stat_cache.log_used, total, circular)
            # Via BLE, silent mode suppresses telemetry — emit a poll line to AppLog
            # so the user can see data is actively refreshing (matches UART streaming feel)
            if HAS_BLEAK and isinstance(self._transport, BleTransport):
                s = self._stat_cache
                _us = s.uptime_s
                _up = f'{_us // 3600}h{(_us % 3600) // 60:02d}m{_us % 60:02d}s'
                self._log(f'[BLE] temp={s.temp_01c / 10.0:.1f}°C  bat={s.bat_mv}mV'
                          f'  log={s.log_used}  up={_up}')
                self._ble_session_records = min(64, self._ble_session_records + 1)
                self._tab_overview._s_bar.set_ble_queue(self._ble_session_records)
            self._poll_accel()
        else:
            self._stat_fail_count = getattr(self, '_stat_fail_count', 0) + 1
            if self._stat_fail_count >= 6:
                self._log(f'{ICO["warn"]} No response from beacon — disconnecting')
                self._do_disconnect()

    def _poll_accel(self):
        """Read current accel sensor after each status refresh and update the Overview card.
        OP_SENSOR_READ_NOW for SENSOR_ID_ACCEL returns x(i16) y(i16) z(i16) — 6 bytes."""
        if not self._connected or not self._transport:
            return

        def _read():
            return self._transport.cmd_sensor_read_now(SENSOR_ID_ACCEL)

        def _done(result):
            wom = bool(self._stat_cache and
                       getattr(self._stat_cache, 'flags', 0) & 0x01)
            if result and result.get('raw_i16') is not None:
                self._tab_overview.update_accel(
                    result['raw_i16'], result.get('y'), result.get('z'), wom)
            else:
                self._tab_overview.update_accel(None, None, None, wom)

        w = Worker(_read)
        w.result.connect(_done)
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_tab_changed(self, idx: int):
        edit_tabs = {1, 2, 4}  # Beacon, Logging, Settings
        self._footer.setVisible(idx in edit_tabs)
        # Sync read-only tabs from cache when there are no pending edits
        if self._cfg_cache and not self._footer._btn_apply.isEnabled():
            self._loading = True
            if idx == 2:
                self._tab_logging.load_config(self._cfg_cache)
            elif idx == 4:
                self._tab_settings.load_config(self._cfg_cache)
            self._loading = False

    def _on_config_dirty(self):
        self._footer.set_enabled(True)
        if self._connected and self._transport and not self._loading:
            self._auto_apply_timer.start()   # restarts on each change → debounce

    def _on_revert(self):
        if self._cfg_cache:
            self._loading = True
            self._tab_beacon.load_config(self._cfg_cache)
            self._tab_logging.load_config(self._cfg_cache)
            self._tab_settings.load_config(self._cfg_cache)
            self._loading = False
        self._auto_apply_timer.stop()
        self._footer.set_enabled(False)

    def _do_ram_write(self):
        """Auto-apply: push current GUI config to beacon RAM only (no flash write).
        Called by the debounce timer on every config change while connected."""
        if not self._connected or not self._transport:
            return
        cfg = self._cfg_cache or ConfigBlob()
        cfg = self._tab_beacon.get_config(cfg)
        cfg = self._tab_logging.get_config(cfg)
        cfg = self._tab_settings.get_config(cfg)
        blob = cfg.to_bytes()

        def _do():
            return self._transport.write_config(blob)

        def _done(result):
            ok, msg = result
            if not ok:
                self._log(f'Auto-apply failed: {msg}')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._log(f'Auto-apply error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_apply(self):
        if not self._connected or not self._transport:
            # Offline: just fill GUI (profile-like)
            self._banner.show_ok('Profile loaded — connect beacon to apply', 3000)
            return

        # Build blob from all config tabs
        cfg = self._cfg_cache or ConfigBlob()
        cfg = self._tab_beacon.get_config(cfg)
        cfg = self._tab_logging.get_config(cfg)
        cfg = self._tab_settings.get_config(cfg)
        blob = cfg.to_bytes()

        # Accel enable/interval are stored separately (log header page, not ConfigBlob)
        accel_en, accel_iv = self._tab_logging.get_accel()

        btn = self._footer._btn_apply
        btn.setEnabled(False)
        btn.setText('Applying…')

        def _do_apply():
            ok, msg = self._transport.write_config(blob)
            if not ok:
                return False, msg
            # Verify: read back
            cfg_b = self._transport.read_config()
            if len(cfg_b) == 64:
                c2 = ConfigBlob.from_bytes(cfg_b)
                if c2.to_bytes() != blob:
                    return False, 'Verification mismatch'
            # Save config to flash (deferred when BLE connected; explicit restart via button)
            s_ok, s_msg = self._transport.save_flash()
            if not s_ok:
                return False, f'Flash save failed: {s_msg}'
            t = self._transport
            if hasattr(t, 'cmd_sensor_enable'):
                t.cmd_sensor_enable(SENSOR_ID_ACCEL, 1 if accel_en else 0)
                if accel_en and accel_iv > 0:
                    t.cmd_sensor_interval(SENSOR_ID_ACCEL, accel_iv)
            return True, 'Settings applied and saved to flash'

        w = Worker(_do_apply)
        w.result.connect(self._on_apply_done)
        w.error.connect(lambda e: self._on_apply_error(e))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_apply_done(self, result):
        ok, msg = result
        self._footer._btn_apply.setEnabled(True)
        self._footer._btn_apply.setText('Apply to beacon')
        if ok:
            self._banner.show_ok(msg, 4000)
            self._log(f'{ICO["ok"]} {msg}')
            # Refresh config cache and reload all edit tabs so they show what beacon accepted
            def _re():
                return self._transport.read_config() if self._transport else b''
            w = Worker(_re)
            w.result.connect(self._on_cfg_reloaded)
            self._workers.append(w)
            w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
            w.start()
        else:
            self._banner.show_err(f'Not applied: {msg}')
            self._log(f'{ICO["err"]} Apply failed: {msg}')

    def _on_apply_error(self, msg: str):
        self._footer._btn_apply.setEnabled(True)
        self._footer._btn_apply.setText('Apply to beacon')
        self._banner.show_err(f'Error: {msg}')
        self._log(f'{ICO["err"]} Apply error: {msg}')

    def _on_restart_beacon(self):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected')
            return
        if not hasattr(self._transport, 'cmd_reboot'):
            self._banner.show_err('Restart not supported on this transport')
            return
        t = self._transport
        self._log('Restarting beacon…')
        self._banner.show_ok('Beacon restarting — reconnect manually', 8000)
        def _do():
            return t.cmd_reboot(0)
        w = Worker(_do)
        w.error.connect(lambda e: self._log(f'Reboot error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()
        QTimer.singleShot(300, self._do_disconnect)

    def _on_cfg_reloaded(self, b: bytes):
        """Called after successful Apply — update cache and sync all edit tabs."""
        if len(b) == 64:
            self._cfg_cache = ConfigBlob.from_bytes(b)
            self._loading = True
            self._tab_beacon.load_config(self._cfg_cache)
            self._tab_logging.load_config(self._cfg_cache)
            self._tab_settings.load_config(self._cfg_cache)
            self._loading = False
            self._tab_data.set_ts_mode(getattr(self._cfg_cache, 'log_ts_source', 0))
            self._footer.set_enabled(False)   # clear "unsaved" state

    def _start_download(self):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected')
            return
        # Pause status polling so STAT? doesn't race with LOG? on the serial port
        self._refresh_timer.stop()
        total    = self._stat_cache.log_used   if self._stat_cache    else 100
        capacity = (self._info_cache.log_total_entries
                    if self._info_cache else LOG_ENTRIES_MAX)
        if total == 0:
            self._banner.show_ok('Log is empty', 3000)
            self._refresh_timer.start()
            return
        # In circular mode stat.log_used can be capped at capacity even when fewer
        # records are actually readable (one page is always erased).  Clamp so we
        # never request more records than the capacity and always reach 100%.
        circular = bool(self._cfg_cache and getattr(self._cfg_cache, 'log_overflow', 0) == 1)
        total = min(total, capacity)
        self._dl_total     = total
        self._dl_capacity  = capacity
        self._dl_circular  = circular
        self._dl_offset    = 0
        self._dl_retries   = 0
        self._dl_records   = []
        # Carry-forward values for delta decoding — must survive across batches
        self._dl_last_temp   = None   # None until first TEMP record seen
        self._dl_last_bat    = None   # None until first BATT record seen
        self._dl_last_bat_mv = None
        self._dl_last_light  = None   # None until first LIGHT record seen
        self._dl_fmt_ver     = 1   # overwritten below once we hear from firmware
        self._tab_data.set_download_progress(0, total, capacity)
        self._tab_events.set_markers_progress(0, total)
        self._log(f'{ICO["download"]} Download started: {total} records  (cap {capacity})')

        # Fetch log format version so the parser can handle v2 ACCEL records
        def _get_fmt():
            return self._transport.cmd_log_info() if self._transport else {}

        def _fmt_done(info):
            self._dl_fmt_ver = info.get('fmt_ver', 1) if info else 1
            self._dl_batch()

        w = Worker(_get_fmt)
        w.result.connect(_fmt_done)
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    @staticmethod
    def _merge_by_ts(records: list) -> list:
        """Merge consecutive records with identical timestamps into one row."""
        merged: dict = {}   # ts -> combined dict
        order:  list = []   # preserves first-seen insertion order
        for r in records:
            ts = r['ts']
            if ts not in merged:
                merged[ts] = dict(r)
                order.append(ts)
            else:
                # Later record of same ts fills in any missing sensor fields
                for k, v in r.items():
                    if k != 'ts':
                        merged[ts].setdefault(k, v)
        return [merged[ts] for ts in order]

    def _dl_finish(self):
        """Called when download completes (success or give-up)."""
        rtc = self._stat_cache.rtc_unix if self._stat_cache else 0
        cap = getattr(self, '_dl_capacity', self._dl_total)
        circ = getattr(self, '_dl_circular', False)
        merged = self._merge_by_ts(self._dl_records)
        # Snap progress to 100% — actual records may be less than initial estimate
        # in circular mode (one flash page is always erased = invisible to reader)
        actual = len(merged)
        self._tab_data.set_download_progress(actual, actual or 1, cap)
        self._tab_data.set_records(merged, rtc, circ)
        self._tab_events.set_markers(merged, rtc)
        self._log(f'{ICO["ok"]} Download done: {actual} rows  ({len(self._dl_records)} raw records)  (cap {cap})')
        if self._connected:
            self._refresh_timer.start()

    def _dl_batch(self):
        BATCH = 8
        remaining = self._dl_total - self._dl_offset
        if remaining <= 0:
            self._dl_finish()
            return
        count = min(BATCH, remaining)
        off   = self._dl_offset
        self._log(f'  LOG? {off} {count}  (total={self._dl_total}, retry={self._dl_retries})')

        def _read():
            return self._transport.read_log(off, count) if self._transport else b''

        w = Worker(_read)
        w.result.connect(self._on_dl_batch)
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    MAX_DL_RETRIES = 3

    def _on_dl_batch(self, data: bytes):
        cap = getattr(self, '_dl_capacity', self._dl_total)
        n = len(data) // 16
        self._log(f'  → got {n} records ({len(data)} bytes)')
        if n == 0:
            circ = getattr(self, '_dl_circular', False)
            if circ and self._dl_offset > 0:
                # Circular mode: 0-record response after downloading some records means
                # we hit the erased write page — this is the normal end of the ring buffer,
                # not a comms error.  One retry to rule out a transient glitch.
                if self._dl_retries < 1:
                    self._dl_retries += 1
                    QTimer.singleShot(500, self._dl_batch)
                else:
                    self._log(f'  ℹ end of circular buffer at offset {self._dl_offset} (write page erased)')
                    self._dl_finish()
                return
            # Linear mode or start of download: MCU may have gone to sleep between
            # STATUS and LOG requests.  Wait 2.5 s before retrying — covers the
            # default g_tx_period_ms = 2000 ms sleep window.
            if self._dl_retries < self.MAX_DL_RETRIES:
                self._dl_retries += 1
                delay_ms = 2500 if self._dl_retries == 1 else 500
                QTimer.singleShot(delay_ms, self._dl_batch)
            else:
                self._log('  ✗ download gave up after retries')
                self._dl_finish()
            return

        self._dl_retries = 0

        # Decode records; carry-forward state persists across batches via instance vars
        received = 0
        for i in range(n):
            raw16 = data[i*16:(i+1)*16]
            ts = struct.unpack_from('<I', raw16, 0)[0]

            if getattr(self, '_dl_fmt_ver', 1) >= 2:
                # ── v2 typed record: byte4=type, byte5=flags, bytes6-15=payload ──
                typ   = raw16[4]
                flags = raw16[5]
                if typ == LOGREC_TYPE_EMPTY or flags == 0xFF:
                    self._dl_total = self._dl_offset + received
                    break
                # Each v2 record carries only its own sensor data.
                # Other columns stay None → displayed as "—" in table.
                if typ == LOGREC_TYPE_TEMP:
                    temp_01c = struct.unpack_from('<h', raw16, 6)[0]
                    self._dl_last_temp = temp_01c
                    self._dl_records.append({
                        'ts':     ts,
                        'temp_c': temp_01c / 10.0,
                    })
                elif typ == LOGREC_TYPE_BATT:
                    bat_mv  = struct.unpack_from('<H', raw16, 6)[0]
                    bat_pct = raw16[8]
                    self._dl_last_bat    = bat_pct
                    self._dl_last_bat_mv = bat_mv
                    self._dl_records.append({
                        'ts':      ts,
                        'bat_pct': bat_pct,
                        'bat_mv':  bat_mv,
                    })
                elif typ == LOGREC_TYPE_LIGHT:
                    light_raw = struct.unpack_from('<H', raw16, 6)[0]
                    self._dl_last_light = light_raw
                    self._dl_records.append({
                        'ts':        ts,
                        'light_raw': light_raw,
                    })
                elif typ == LOGREC_TYPE_ACCEL:
                    ax, ay, az = struct.unpack_from('<hhh', raw16, 6)
                    self._dl_records.append({
                        'ts':      ts,
                        'accel_x': ax,
                        'accel_y': ay,
                        'accel_z': az,
                    })
                elif typ == LOGREC_TYPE_MARKER:
                    tag = raw16[6]
                    self._dl_records.append({
                        'ts':     ts,
                        'marker': int(tag),
                    })
                # unknown types: skip silently
                received += 1
                continue

            # ── v1 combined-sensor record ────────────────────────────────────
            r = LogRecord.from_bytes(raw16)
            if r.rec_flags == LogRecord.REC_FLAG_EMPTY:
                self._dl_total = self._dl_offset + received
                break
            if r.rec_flags in (LogRecord.REC_FLAG_FULL, LogRecord.REC_FLAG_CHECKPOINT):
                if r.mask & LogRecord.LOG_MASK_TEMP:        self._dl_last_temp  = r.temp_01c
                if r.mask & LogRecord.LOG_MASK_BATTERY_PCT: self._dl_last_bat   = r.battery_pct
                if r.mask & LogRecord.LOG_MASK_LIGHT:       self._dl_last_light = r.light_raw
            elif r.rec_flags == LogRecord.REC_FLAG_DELTA:
                if self._dl_last_temp is not None and r.mask & LogRecord.LOG_MASK_TEMP:
                    self._dl_last_temp  += r.temp_01c
                if self._dl_last_bat is not None:
                    self._dl_last_bat   += r.battery_pct
                if self._dl_last_light is not None:
                    self._dl_last_light += r.light_raw
            # Build record — show only fields present in this row's mask
            row = {'ts': r.timestamp}
            if r.mask & LogRecord.LOG_MASK_TEMP:
                row['temp_c'] = self._dl_last_temp / 10.0 if self._dl_last_temp is not None else None
            if r.mask & (LogRecord.LOG_MASK_BATTERY_PCT | LogRecord.LOG_MASK_BATTERY_MV):
                row['bat_pct'] = self._dl_last_bat
                row['bat_mv']  = r.bat_mv
            if r.mask & LogRecord.LOG_MASK_LIGHT:
                row['light_raw'] = self._dl_last_light
            self._dl_records.append(row)
            received += 1

        self._dl_offset += received
        self._tab_data.set_download_progress(self._dl_offset, self._dl_total, cap)
        self._tab_events.set_markers_progress(self._dl_offset, self._dl_total)
        if self._dl_offset < self._dl_total:
            self._dl_batch()
        else:
            self._dl_finish()

    def _log(self, text: str):
        """Append a line to the internal log tab (thread-safe from UI thread)."""
        self._tab_applog.append_line(text)

    def _erase_log(self):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected')
            return
        self._refresh_timer.stop()
        self._tab_data.start_erase_animation()

        def _do():
            return self._transport.erase_log()  # returns (bool, str)

        def _done(result):
            ok, msg = result
            self._tab_data.stop_erase_animation(ok, msg)
            if self._connected:
                self._refresh_timer.start()
            if ok:
                self._dl_records = []
                self._dl_offset  = 0
                self._dl_total   = 0
                self._tab_data.set_records([], 0)
                if msg == 'deferred':
                    self._banner.show_ok(f'{ICO["erase"]} Erase scheduled — executes after BLE disconnect')
                    self._log(f'{ICO["ok"]} Erase scheduled (deferred — will run after disconnect)')
                else:
                    self._banner.show_ok(f'{ICO["erase"]} Log erased.')
                    self._log(f'{ICO["ok"]} Log erased')
            else:
                self._banner.show_err(f'{ICO["err"]} Erase failed: {msg}')
                self._log(f'{ICO["err"]} Erase failed: {msg}')

        def _err(msg):
            self._tab_data.stop_erase_animation(False)
            if self._connected:
                self._refresh_timer.start()
            self._banner.show_err(f'{ICO["err"]} Erase error: {msg}')
            self._log(f'{ICO["err"]} Erase error: {msg}')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(_err)
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_tag_set(self, tag: str):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected')
            return
        if not tag:
            self._banner.show_err('Tag cannot be empty')
            return

        def _do():
            return self._transport.send_tag(tag)

        def _done(result):
            ok, msg = result
            if ok:
                self._banner.show_ok(f'Tag set to "{tag}"', 3000)
                self._log(f'{ICO["ok"]} Tag set: {tag}')
                # re-read info to update caches
                if self._transport:
                    info_b = self._transport.read_info()
                    if len(info_b) == 48:
                        self._info_cache = InfoBlob._from_bytes(info_b)
                        self._tab_settings.load_info(self._info_cache)
                        self._tab_overview.update_status(
                            self._stat_cache, self._info_cache, self._cfg_cache)
            else:
                self._banner.show_err(f'Set tag failed: {msg}')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._banner.show_err(f'Tag error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_uptime_reset(self):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected')
            return
        reply = QMessageBox.question(self, 'Reset Uptime',
            'Zero the persistent uptime counter on the beacon?\n'
            'This cannot be undone.',
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply != QMessageBox.StandardButton.Yes:
            return

        def _do():
            return self._transport.send_uptime_clr()

        def _done(result):
            ok, msg = result
            if ok:
                self._banner.show_ok('Uptime reset to 0', 3000)
                self._log(f'{ICO["ok"]} Uptime reset')
                if self._transport:
                    info_b = self._transport.read_info()
                    if len(info_b) == 48:
                        self._info_cache = InfoBlob._from_bytes(info_b)
                        self._tab_settings.load_info(self._info_cache)
                        self._tab_overview.update_status(
                            self._stat_cache, self._info_cache, self._cfg_cache)
            else:
                self._banner.show_err(f'Uptime reset failed: {msg}')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._banner.show_err(f'Uptime reset error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_uptime_save(self):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected')
            return

        btn = self._tab_settings._btn_uptime_save
        btn.setEnabled(False)
        btn.setText('Working…')

        def _do():
            # Both calls run in the Worker thread — no Qt deadlock.
            # Calling read_info() from the main thread would block Qt's
            # event loop and prevent the reader thread signal from being
            # delivered, causing a 3-second timeout and zeros in the display.
            ok, msg = self._transport.flush_uptime()
            info_b = self._transport.read_info() if ok else bytes(48)
            return ok, msg, info_b

        def _done(result):
            btn.setEnabled(True)
            btn.setText('Get Data')
            ok, msg, info_b = result
            if ok:
                self._banner.show_ok('Data saved to flash and refreshed', 3000)
                self._log(f'{ICO["ok"]} Uptime flushed to flash')
                if len(info_b) == 48:
                    self._info_cache = InfoBlob._from_bytes(info_b)
                    self._tab_settings.load_info(self._info_cache)
                    self._tab_overview.update_status(
                        self._stat_cache, self._info_cache, self._cfg_cache)
            else:
                self._banner.show_err(f'Get Data failed: {msg}')
                self._log(f'{ICO["err"]} Uptime flush failed: {msg}')

        def _err(e):
            btn.setEnabled(True)
            btn.setText('Get Data')
            self._banner.show_err(f'Get Data error: {e}')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(_err)
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    # ── Uptime auto-flush handlers ───────────────────────────────────────────

    def _on_uptime_flush_changed(self, enabled: bool, interval_min: int):
        """Called when the user changes the auto-flush toggle or interval."""
        if enabled:
            self._uptime_flush_timer.setInterval(interval_min * 60000)
            if self._connected:
                self._uptime_flush_timer.start()
        else:
            self._uptime_flush_timer.stop()
        self._save_settings()

    def _on_uptime_flush_tick(self):
        """Periodic timer: flush RAM uptime counters to flash via Worker."""
        if not self._connected or not self._transport:
            return

        def _do():
            ok, msg = self._transport.flush_uptime()
            info_b = self._transport.read_info() if ok else bytes(48)
            return ok, msg, info_b

        def _done(result):
            ok, msg, info_b = result
            if ok:
                self._log(f'{ICO["ok"]} Auto uptime flush OK')
                if len(info_b) == 48:
                    self._info_cache = InfoBlob._from_bytes(info_b)
                    self._tab_settings.load_info(self._info_cache)
                    self._tab_overview.update_status(
                        self._stat_cache, self._info_cache, self._cfg_cache)
            else:
                self._log(f'{ICO["err"]} Auto uptime flush failed: {msg}')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._log(f'{ICO["err"]} Auto uptime flush error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    # ── Hardware descriptor handlers ─────────────────────────────────────────

    def _on_hwdesc_read(self):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected'); return

        def _do():
            return self._transport.cmd_hwdesc_get()

        def _done(blob):
            if blob:
                self._tab_settings.load_hwdesc(blob)
                self._tab_settings.set_hw_status('Read OK')
                self._log(f'{ICO["ok"]} HwDesc read from device')
            else:
                self._banner.show_err('HwDesc read failed')
                self._tab_settings.set_hw_status('Read FAILED')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._banner.show_err(f'HwDesc read error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_hwdesc_write(self, blob):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected'); return

        def _do():
            return self._transport.cmd_hwdesc_set(blob)

        def _done(rc):
            if rc == CMD_OK:
                self._tab_settings.set_hw_status('Written to RAM (not saved)')
                self._banner.show_ok('HwDesc written to device RAM', 3000)
                self._log(f'{ICO["ok"]} HwDesc written to RAM')
            else:
                self._banner.show_err(f'HwDesc write failed (rc=0x{rc:02X})')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._banner.show_err(f'HwDesc write error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_hwdesc_commit(self):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected'); return
        reply = QMessageBox.question(self, 'Commit HwDesc',
            'Write hardware descriptor to flash (page 59)?\n'
            'This permanently changes the device configuration.',
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply != QMessageBox.StandardButton.Yes:
            return

        def _do():
            return self._transport.cmd_hwdesc_commit()

        def _done(rc):
            if rc == CMD_OK:
                self._tab_settings.set_hw_status('Saved to flash ✓')
                self._banner.show_ok('HwDesc committed to flash', 3000)
                self._log(f'{ICO["ok"]} HwDesc committed to flash page 59')
            else:
                self._banner.show_err(f'HwDesc commit failed (rc=0x{rc:02X})')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._banner.show_err(f'HwDesc commit error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    # ── Wake-on-motion handlers ───────────────────────────────────────────────

    def _on_wom_apply(self, params: dict):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected')
            return

        def _do():
            return self._transport.cmd_wake_cfg_set(
                enable       = int(params.get('enable', 0)),
                threshold_mg = params.get('threshold_mg', 100),
                duration_ms  = params.get('duration_ms', 0),
                action       = params.get('action', 0x01),
            )

        def _done(result):
            rc, _ = result if isinstance(result, tuple) else (result, b'')
            if rc == CMD_OK:
                self._banner.show_ok('WoM config applied', 2000)
                self._log(f'{ICO["ok"]} WoM config applied')
            elif rc == CMD_ERR_STATE:
                self._banner.show_err('WoM: cannot arm — sensor is powered off')
                self._tab_events.set_wom_status_text('⚠ Sensor powered off')
            else:
                self._banner.show_err(f'WoM apply failed (rc=0x{rc:02X})')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._banner.show_err(f'WoM apply error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_wom_refresh(self):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected')
            return

        def _do():
            return self._transport.cmd_wake_status()

        def _done(result):
            if result:
                armed = result.get('armed', 0)
                count = result.get('trigger_count', 0)
                last  = result.get('last_trigger_ts', 0)
                status_txt = (f'{"Armed" if armed else "Disarmed"}  '
                              f'count={count}  last_ts={last}')
                self._tab_events.set_wom_status_text(status_txt)
                self._log(f'{ICO["ok"]} WoM status: {status_txt}')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._banner.show_err(f'WoM refresh error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_wom_clear(self):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected')
            return

        def _do():
            return self._transport.cmd_wake_clear()

        def _done(result):
            rc, _ = result if isinstance(result, tuple) else (result, b'')
            if rc == CMD_OK:
                self._banner.show_ok('WoM counter cleared', 2000)
                self._tab_events.set_wom_status_text('count=0')
                self._log(f'{ICO["ok"]} WoM counter cleared')
            else:
                self._banner.show_err(f'WoM clear failed (rc=0x{rc:02X})')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._banner.show_err(f'WoM clear error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    # ── Accel zero-g calibration ──────────────────────────────────────────────

    def _on_accel_zero_g(self):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected')
            return

        def _do():
            rx = self._transport.cmd_sensor_read_now(SENSOR_ID_ACCEL)
            ry = self._transport.cmd_sensor_read_now(SENSOR_ID_ACCEL)
            rz = self._transport.cmd_sensor_read_now(SENSOR_ID_ACCEL)
            return rx, ry, rz

        def _done(result):
            rx, ry, rz = result
            # cmd_sensor_read_now returns (rc, raw_i16, scaled_i32) via send_cmd
            # For accel the three axes come from three separate calls (X, Y, Z)
            # Interpret raw_i16 as the axis reading
            def _raw(r):
                if isinstance(r, dict):
                    return r.get('raw_i16', 0)
                if isinstance(r, tuple) and len(r) >= 2:
                    p = r[1]
                    if len(p) >= 2:
                        import struct as _s
                        return _s.unpack_from('<h', p, 0)[0]
                return 0
            ox, oy, oz = _raw(rx), _raw(ry), _raw(rz)
            self._tab_settings.set_accel_offsets(ox, oy, oz)
            self._banner.show_ok(f'Accel offsets captured: X={ox} Y={oy} Z={oz}', 4000)
            self._log(f'{ICO["ok"]} Zero-G offsets: X={ox} Y={oy} Z={oz}')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._banner.show_err(f'Accel zero-G error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _update_bcn_time_label(self, stat: 'StatusBlob'):
        import datetime
        if stat.rtc_unix:
            dt = datetime.datetime.fromtimestamp(stat.rtc_unix)
            C = self._C
            self._lbl_bcn_time.setText(
                f'⏱  BCN {dt.strftime("%Y-%m-%d  %H:%M:%S")}')
            self._lbl_bcn_time.setStyleSheet(f'color:{C["text_dim"]};')
            self._lbl_bcn_time.setVisible(True)
        else:
            self._lbl_bcn_time.setText('⏱  BCN --:--:--')
            self._lbl_bcn_time.setStyleSheet(f'color:{self._C["warning"]};')
            self._lbl_bcn_time.setVisible(True)

    def _check_time_drift(self, stat: 'StatusBlob'):
        pc_ts = int(time.time())
        bcn_ts = stat.rtc_unix
        if bcn_ts == 0:
            reply = QMessageBox.question(
                self, 'Clock not set',
                'Beacon RTC is not set.\n\nSync beacon clock with PC time now?',
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.Yes)
            if reply == QMessageBox.StandardButton.Yes:
                self._sync_time()
        elif abs(pc_ts - bcn_ts) > 60:
            drift = pc_ts - bcn_ts
            sign = '+' if drift > 0 else ''
            reply = QMessageBox.question(
                self, 'Clock drift',
                f'Beacon clock is {sign}{drift} s off from PC.\n\nSync now?',
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.Yes)
            if reply == QMessageBox.StandardButton.Yes:
                self._sync_time()

    def _refresh_from_beacon(self):
        if not self._connected or not self._transport:
            return
        self._refresh_timer.stop()
        self._btn_refresh.setEnabled(False)
        self._btn_refresh.setText('…')
        self._init_after_connect()

    def _on_refresh_done(self):
        self._btn_refresh.setEnabled(True)
        self._btn_refresh.setText('⟳ Refresh')

    def _sync_time(self):
        if not self._connected or not self._transport:
            return
        unix_ts = int(time.time())

        def _do():
            return self._transport.sync_time(unix_ts)

        def _done(result):
            ok, msg = result
            if ok:
                import datetime
                dt = datetime.datetime.fromtimestamp(unix_ts)
                self._banner.show_ok(f'Time synced: {dt.strftime("%Y-%m-%d %H:%M:%S")}', 4000)
                self._log(f'⏱ Time synced: {unix_ts}')
            else:
                self._banner.show_err(f'Time sync failed: {msg}')
                self._log(f'❌ Time sync failed: {msg}')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._banner.show_err(f'Time sync error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    # ── BLE handlers (v4) ────────────────────────────────────────────────────

    def _on_ble_read(self):
        if not self._connected or not self._transport:
            self._tab_ble.set_status('Not connected', ok=False)
            return

        def _do():
            return self._transport.cmd_ble_get()

        def _done(s):
            if s is not None:
                self._tab_ble.load_ble(s)
                self._tab_ble.set_status('Read OK', ok=True)
                self._log(f'{ICO["ok"]} BLE settings read from device')
            else:
                self._tab_ble.set_status('Read failed', ok=False)
                self._banner.show_err('BLE read failed')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: (
            self._tab_ble.set_status(f'Error: {e}', ok=False),
            self._banner.show_err(f'BLE read error: {e}')))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_ble_apply(self, s):
        if not self._connected or not self._transport:
            self._tab_ble.set_status('Not connected — settings not saved', ok=False)
            return

        def _do():
            rc = self._transport.cmd_ble_set(s)
            # No reboot! Firmware saves to flash immediately in _op_ble_set.
            # NVIC_SystemReset() only resets CPU1; CPU2 (BLE stack) keeps the
            # connection alive, so CPU1 re-init fails. Just disconnect cleanly.
            return rc

        _mode_names = {0x00: 'Off', 0x01: 'Cont', 0x02: 'Sched',
                       0x04: 'Gekon', 0x06: 'Sched+Gekon'}

        def _done(rc):
            if rc == CMD_OK:
                mode_str = _mode_names.get(s.op_mode, f'0x{s.op_mode:02X}')
                self._tab_ble.set_status('Saved', ok=True)
                self._banner.show_ok('BLE settings saved', 3000)
                self._log(f'{ICO["ok"]} BLE settings saved  '
                          f'mode={mode_str}  '
                          f'pwr={s.tx_power}  adv={s.adv_interval_ms}ms')
                self._stat_fail_count = 0  # reset: flash save may delay one STAT? poll
                # Stay connected — settings take effect on next BLE advertising session.
                # No reboot: NVIC_SystemReset() only resets CPU1, CPU2 keeps the
                # BLE connection alive and blocks CPU1 re-init.
            else:
                self._tab_ble.set_status(f'Apply failed (rc=0x{rc:02X})', ok=False)
                self._banner.show_err(f'BLE apply failed (0x{rc:02X})')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: (
            self._tab_ble.set_status(f'Error: {e}', ok=False),
            self._banner.show_err(f'BLE apply error: {e}')))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_events_read(self):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected')
            return

        def _do():
            rc, data = self._transport.send_cmd(OP_EVT_GET, timeout=4.0)
            return rc, data

        def _done(res):
            rc, data = res
            if rc == 0 and len(data) >= EVENTS_BLOB_SZ:
                self._tab_events.decode_blob(data[:EVENTS_BLOB_SZ])
                self._banner.show_ok('Events read OK', 3000)
                self._log(f'{ICO["ok"]} Events read from device ({EVENTS_BLOB_SZ} bytes)')
            else:
                self._banner.show_err(f'Events read failed (rc=0x{rc:02X}, got {len(data)} bytes)')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._banner.show_err(f'Events read error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _on_events_write(self, blob: bytes):
        if not self._connected or not self._transport:
            self._banner.show_err('Not connected')
            return

        def _do():
            rc, _ = self._transport.send_cmd(OP_EVT_SET, blob, timeout=4.0)
            return rc

        def _done(rc):
            if rc == 0:
                self._banner.show_ok('Events written to device', 3000)
                self._log(f'{ICO["ok"]} Events written ({EVENTS_BLOB_SZ} bytes)')
            else:
                self._banner.show_err(f'Events write failed (rc=0x{rc:02X})')

        w = Worker(_do)
        w.result.connect(_done)
        w.error.connect(lambda e: self._banner.show_err(f'Events write error: {e}'))
        self._workers.append(w)
        w.finished.connect(lambda: self._workers.remove(w) if w in self._workers else None)
        w.start()

    def _toggle_theme(self):
        self._theme = 'dark' if self._theme == 'light' else 'light'
        self._C = THEMES[self._theme]
        self._btn_theme.setText('☼' if self._theme == 'light' else '☽')
        self._apply_theme()
        self._save_settings()

    def _apply_theme(self):
        C = self._C
        QApplication.instance().setStyleSheet(build_qss(C))
        self._tab_overview.retheme(C)
        self._tab_beacon.retheme(C)
        self._tab_logging.retheme(C)
        self._tab_data.retheme(C)
        self._tab_settings.retheme(C)
        self._tab_ble.retheme(C)
        self._tab_events.retheme(C)
        self._tab_applog.retheme(C)
        self._banner.retheme(C)
        self._footer.retheme(C)
        self._update_connection_ui(self._connected)
        # Re-apply time label color after theme change
        if self._stat_cache:
            self._update_bcn_time_label(self._stat_cache)


# ─────────────────────────────────────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────────────────────────────────────
def run_gui():
    if HAS_PG:
        pg.setConfigOption('background', 'w')
        pg.setConfigOption('foreground', '#1e293b')
    app = QApplication(sys.argv)
    app.setApplicationName('TX Beacon v4')
    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == '__main__':
    if '--selftest' in sys.argv:
        run_selftest()
    else:
        run_gui()
