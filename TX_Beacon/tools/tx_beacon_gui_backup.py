"""
TX Beacon 30 MHz — UART Control GUI
Run: python tx_beacon_gui.py  |  pip install pyserial
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, simpledialog, filedialog
import threading, queue, re, datetime, os, json

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    import sys, subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pyserial"])
    import serial
    import serial.tools.list_ports

try:
    import matplotlib
    matplotlib.use("TkAgg")
    from matplotlib.figure import Figure
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
    _HAS_MPL = True
except ImportError:
    _HAS_MPL = False

# ── Colours ───────────────────────────────────────────────────────────────────
BG      = "#f0f2f5"
BG2     = "#ffffff"
BG3     = "#d0d5dd"
FG      = "#111827"
FG_DIM  = "#6b7280"
GREEN   = "#166534"
BLUE    = "#1d4ed8"
RED     = "#991b1b"
ACCENT  = "#2563eb"

def _mpl_plot_series(ax, xs, ys, label, color):
    import datetime as _dt
    pts = [(x, y) for x, y in zip(xs, ys) if y is not None]
    if not pts:
        ax.set_ylabel(label, color=color, fontsize=8)
        ax.grid(True, alpha=0.3)
        return
    xs2, ys2 = zip(*pts)
    ax.plot(xs2, ys2, color=color, linewidth=1.2, marker=".", markersize=3)
    ax.set_ylabel(label, color=color, fontsize=8)
    is_dt = isinstance(xs2[0], _dt.datetime)
    ax.set_xlabel("Date/Time (UTC)" if is_dt else "ts (s since boot)", fontsize=8)
    if is_dt:
        try:
            import matplotlib.dates as mdates
            ax.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d %H:%M"))
            ax.figure.autofmt_xdate(rotation=30, ha="right")
        except Exception:
            pass
    ax.grid(True, alpha=0.3)
    ax.tick_params(labelsize=7)


_MONTH_NAMES = ["Jan","Feb","Mar","Apr","May","Jun",
                "Jul","Aug","Sep","Oct","Nov","Dec"]
_DAY_NAMES   = ["Mon","Tue","Wed","Thu","Fri","Sat","Sun"]

_DIR           = os.path.dirname(os.path.abspath(__file__))
PROFILES_FILE  = os.path.join(_DIR, "tx_beacon_profiles.json")
SETTINGS_FILE  = os.path.join(_DIR, "tx_beacon_settings.json")

PRESET_PROFILES = {
    "Always active": {
        "mode": "pulse", "ch": 0, "pwr": 3, "pulse_ms": 23, "period_ms": 2000,
        "led_mode": "tx", "sched_enabled": False, "hours": [], "days": [], "months": []
    },
    "Night (22–06h)": {
        "mode": "pulse", "ch": 0, "pwr": 3, "pulse_ms": 23, "period_ms": 2000,
        "led_mode": "tx", "sched_enabled": True,
        "hours": [22, 23, 0, 1, 2, 3, 4, 5, 6], "days": [], "months": []
    },
    "Day (08–20h)": {
        "mode": "pulse", "ch": 0, "pwr": 3, "pulse_ms": 23, "period_ms": 2000,
        "led_mode": "tx", "sched_enabled": True,
        "hours": list(range(8, 21)), "days": [], "months": []
    },
    "Eco night": {
        "mode": "eco", "ch": 0, "pwr": 2, "pulse_ms": 50, "period_ms": 5000,
        "led_mode": "off", "sched_enabled": True,
        "hours": [22, 23, 0, 1, 2, 3, 4, 5], "days": [], "months": []
    },
}

HELP_TEXT = """\
TX Beacon 30 MHz — User Manual & Command Reference
════════════════════════════════════════════════════════════════════

  Radio beacon 30 MHz for field rodent research (rat implant ~300g).
  MCU: STM32WB1MMCH6TR (Cortex-M4 @ 64 MHz, 256KB flash, BLE).
  Connection: USB-UART adapter → NRST/PA9(TX)/PA10(RX)/GND.
  Baud: 115200, 8N1.  Commands are case-sensitive, end with Enter.


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  ARCHITECTURE — How modules connect
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  ┌─────────────────────────────────────────────────────────────┐
  │                       main.c                                │
  │  loop: ChipTemp_Tick → BattAdc_Tick → LightAdc_Tick         │
  │        → UartCmd_Poll → FlashLog_Task                       │
  └──────┬──────────┬───────────┬─────────────┬────────────────┘
         │          │           │             │
  ┌──────▼──┐  ┌────▼────┐  ┌──▼──────┐  ┌───▼──────────────┐
  │drv_rf_tx│  │drv_chip │  │drv_batt │  │  svc_uart_cmd    │
  │ PA0 TX  │  │  _temp  │  │  _adc   │  │  parses terminal │
  │ PA1 PWR1│  │ ADC int.│  │ PA5 ADC │  │  commands →      │
  │ PA8 PWR2│  │ offset  │  │g_batt_mV│  │  calls drivers   │
  │ PA2 CH1 │  │g_temp   │  │g_batt_% │  └──────────────────┘
  │ PA6 CH2 │  │ _x10    │  │g_batt_  │
  └─────────┘  └─────────┘  │ raw/vref│  ┌──────────────────┐
                             └─────────┘  │   flash_config   │
  ┌──────────┐  ┌──────────┐             │   page 60        │
  │drv_light │  │ drv_led  │             │   TxConfig_t     │
  │  _adc    │  │  PB1 LED │             │   (settings)     │
  │ ADC light│  │heartbeat │             └──────────────────┘
  │g_light   │  │tx / on   │
  │_raw/lux  │  └──────────┘             ┌──────────────────┐
  └──────────┘                           │   flash_log      │
                                         │   pg61 header    │
  ┌──────────┐  ┌──────────┐             │   pg62-67 data   │
  │ hw_desc  │  │svc_power │             │   768 records    │
  │ hardware │  │ Shutdown │             │   16 bytes each  │
  │ metadata │  │ Stop1    │             └──────────────────┘
  └──────────┘  └──────────┘

  Data flow — temperature example:
    ADC raw → _calc_temp_x10() → +g_temp_offset_x10 → g_last_temp_x10
    g_last_temp_x10 → [TEMP] UART print   (corrected)
    g_last_temp_x10 → FlashLog_Task()     (corrected, same value)

  Flash page map (2 KB per page):
    pg 0–59   firmware code
    pg 60     TxConfig_t  (settings saved by 'save')
    pg 61     FlashLog header + field descriptors + LogConfig_t
    pg 62–67  FlashLog data  (768 × 16-byte records = 12 KB)
    pg 68+    BLE stack (CPU2, read-only)


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  QUICK START
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  First boot after flash:
    1. Connect UART, open GUI, select port → Connect
    2. Tab "HW Desc" → fill board details → hwdesc save
    3. Tab "Status & RTC" → ⟳ Sync PC Time  (sets RTC)
    4. Left panel: set Mode / Channel / Power / Pulse / Period
    5. Press "Save to flash" — settings survive power-off
    6. Tab "Flash Log" → Log Info to check log state
    7. Disconnect UART for field use (UART draws ~1 mA)

  Field calibration (temperature offset):
    • Connect UART, wait for [TEMP] lines
    • Compare chip reading vs reference thermometer
    • temp offset <value>  e.g.  temp offset -1.5
    • Offset applies to both UART display AND flash log
    • Save settings after changing offset


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  STATUS & CONTROL
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  status        print all current settings + schedule summary
  regs          print power/clock diagnostic registers (debug)
  reset         software reset — reloads config from flash
  sleep         enter Shutdown mode (~0.5 µA, wake: GEKON button)
  save          write current RAM settings to flash page 60


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  TRANSMITTER
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  Modes:
    mode off      TX completely disabled (GPIO stay LOW)
    mode pulse    brief TX burst → sleep (default, lowest power)
    mode cont     continuous TX, MCU stays awake
    mode eco      MCU enters Stop1 during TX (GPIO hold RF state)

  TX on/off:
    tx on         start TX (or resume paused cont session)
    tx off        stop TX

  Parameters:
    ch 0..3       channel:     CH0 (PA2+PA6), CH1 (PA2), CH2 (PA6), CH3 (none)
    pwr 1..4      power level: PWR1 (PA1=L,PA8=L) … PWR4 (PA1=H,PA8=H)
    pulse <ms>    TX burst duration  (1–60000 ms)
    period <ms>   gap between bursts (100–3600000 ms)

  Hardware:
    RF_TX  = PA0  → gates P-MOS Q1 (PJE8406), powers Colpitts oscillator
    RF_PWR1= PA1  → bias resistor network level 1
    RF_PWR2= PA8  → bias resistor network level 2
    CH1    = PA2  → adds capacitor to crystal via R3 (4Ω) — shifts freq down
    CH2    = PA6  → adds capacitor to crystal via R4 (4Ω) — shifts freq more


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  TEMPERATURE  (internal STM32 chip sensor)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  Algorithm (ST AN4800):
    1. Read VREFINT ADC code → compute actual VDDA
    2. Read TEMPSENSOR ADC (8 averages)
    3. Normalise to VDDA=3.0V, interpolate via TS_CAL1(30°C)/TS_CAL2(130°C)
    4. Add g_temp_offset_x10 (user calibration)
    Result stored in g_last_temp_x10 (×10, e.g. 365 = 36.5°C)

  Commands:
    temp                  print current mode + last corrected reading
    temp mode off         disable — never read temperature
    temp mode periodic    read every N seconds (saves battery vs 'tx')
    temp mode tx          read just before each TX pulse
    temp period <1-255>   set periodic interval in seconds
    temp offset <±N.N>    calibration offset in °C, 0.1 step  e.g. -2.5
                          ► applies to UART display AND flash log
    temp read             force one measurement now

  Output: [TEMP] chip=36.5C  VDDA=3280mV


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  BATTERY ADC  (PA5 resistor divider)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  Algorithm:
    1. Read VREFINT → compute VDDA (same as temp driver)
    2. Read PA5 ADC code
    3. V_PA5 = raw × VDDA / 4095
    4. V_batt = V_PA5 × scale_factor  (accounts for divider ratio)
    5. % = (V_batt − empty_mV) / (full_mV − empty_mV) × 100

  Commands:
    batt              print current mode + last reading
    batt mode off     disable
    batt mode periodic  read every N seconds
    batt period <s>   set interval  (1–255 s)
    batt scale <x.x>  voltage divider multiplier  e.g. 2.0 for 1:1 divider
    batt show         verbose: scale, full/empty thresholds
    batt read         force one measurement now

  Output: [BATT] Battery: 3850mV 72%  raw=2041 vref=1510
    raw=  — raw ADC code of PA5 (0-4095)  useful for divider calibration
    vref= — raw VREFINT code              useful for VDDA verification


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  LIGHT ADC
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  Commands:
    light               print current mode + last reading
    light mode off      disable
    light mode periodic read every N seconds
    light period <s>    set interval  (1–255 s)
    light read          force one measurement now

  Output: [LIGHT] Light: 1024 (raw)  ~350 lux


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  LED  (PB1, active HIGH)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  led off        always off  (recommended in field — saves ~1.5 mA)
  led on         always on
  led heartbeat  slow blink every ~2 s — confirms MCU is running
  led tx         mirrors TX state ON/OFF  (most useful during tuning)


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  SCHEDULE
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  When schedule is enabled, TX only fires during the allowed
  hours / days / months. Outside the window: Shutdown sleep.

  sched show              print current masks
  sched off               disable schedule (TX always active)
  sched hours H H…        active hours 0-23, space-sep  (empty = all 24h)
                          e.g.  sched hours 22 23 0 1 2 3 4 5
  sched days  D D…        weekdays: Mon=1 … Sun=7       (empty = all 7d)
                          e.g.  sched days 1 2 3 4 5  (Mon-Fri)
  sched months M M…       months: Jan=1 … Dec=12        (empty = all 12)
                          e.g.  sched months 4 5 6 7 8 9  (Apr-Sep)

  GUI "Schedule" tab has checkboxes for hours/days/months.
  Changes are applied immediately to RAM; use 'save' to persist.


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  RTC
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  rtc get                          print current date + time
  rtc set YYYY-MM-DD HH:MM:SS      set RTC (weekday calculated auto)
  rtc live on|off                  stream RTC every 1 s via UART
                                   (saved to flash; disable in field!)

  GUI "⟳ Sync PC Time" sends the PC's current time in one click.
  "Live RTC" checkbox enables/disables streaming.

  Note: RTC is backed by the MCU's LSE (32.768 kHz crystal) and
  runs on VBAT when main power is off (if coin cell fitted).


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  FLASH LOG  (sensor data recorder)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  Storage:
    Header page (pg 61): magic, format version, field descriptors,
                         LogConfig_t (mask, intervals, write mode…)
    Data pages (pg 62-67): 768 records × 16 bytes = 12 KB total

  Record format (16 bytes):
    timestamp(4B)  mask(1B)  flags(1B)  temp×10(2B)
    light_raw(2B)  bat_pct(1B)  bat_mv_hi(1B)  bat_mv_lo(1B)  pad(3B)

  Sensor mask bits:
    0x01 TEMP    — internal chip temperature (corrected)
    0x02 LIGHT   — light ADC raw value
    0x04 BAT_PCT — battery percentage
    0x08 BAT_MV  — battery millivolts

  Write modes:
    ALWAYS     — write every time an interval fires (most data)
    ON_CHANGE  — write only if value changed by ≥ delta threshold
    ADAPTIVE   — checkpoint every N records, deltas in between
                 (×3 capacity vs ALWAYS for slowly-changing sensors)

  Overflow modes:
    STOP       — stops logging when full (preserves first data)
    CIRCULAR   — overwrites oldest page when full (keeps latest data)

  Record flags:
    FULL        — all fields present, absolute values
    CHECKPOINT  — absolute values (ADAPTIVE mode anchor point)
    DELTA       — values are differences from previous CHECKPOINT
                  (must decode with FlashLog_DecodeAbsolute)

  UART commands:
    log info              print full status (used/free/mode/mask…)
    log get               print current config (mask, intervals, deltas)
    log calc              capacity calculator (writes/hr, hours remaining)
    log write             force one record now (all sensors)
    log pages             print flash page addresses
    log dump              dump all records as CSV to terminal
    log read <N>          print single record #N (decoded)
    log read <N> <count>  dump range as CSV
    log clear yes         erase all data pages (irreversible!)

  Config commands:
    log mask <hex>        set active sensor mask  e.g. log mask 0F
                          bit0=TEMP  bit1=LIGHT  bit2=BAT_PCT  bit3=BAT_MV
    log temp <s>          temperature sampling interval (seconds)
    log light <s>         light sampling interval (seconds)
    log bat <s>           battery sampling interval (seconds)
    log mode <0|1|2>      write mode: 0=ALWAYS  1=ON_CHANGE  2=ADAPTIVE
    log overflow <0|1>    overflow: 0=STOP  1=CIRCULAR
    log dt <n>            temp change threshold (0.1°C units, e.g. 5 = 0.5°C)
    log dl <n>            light change threshold (%)
    log db <n>            battery change threshold (%)

  GUI "Flash Log" tab workflow:
    1. "Log Info" — check current state
    2. "Get Config" — sync GUI controls with MCU config
    3. Set mask checkboxes + intervals → "Set mask" / "Set" each
    4. "Write Now" — manual test record
    5. "Dump All" — read all data → table fills automatically
    6. "Save CSV to PC…" — save to file for analysis
    7. "Plot Temp/Battery/Light/All" — matplotlib graphs
       (requires:  pip install matplotlib)

  CSV column order: idx, ts_s, flags, temp_c, light_raw, bat_pct, bat_mv
  Empty cell = that sensor was not in this record's mask.


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  HW DESCRIPTOR  (one-time setup after board assembly)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  Describes the physical hardware so firmware behaves correctly.
  Stored separately from TxConfig so firmware updates don't erase it.

  hwdesc show                          print all fields
  hwdesc save                          burn RAM → flash (one-time)
  hwdesc clear                         reset to defaults (RAM only)
  hwdesc ver <1-255>                   hardware revision number
  hwdesc temp  none|crystal|ntc|stts22h|lis2dw12
  hwdesc light none|<model>            e.g. BH1750
  hwdesc batt  none|adc <full_mV> <empty_mV>|fuel
                                       e.g. hwdesc batt adc 4200 3200
  hwdesc accel none|ism330|lis2dw12|<model>
  hwdesc led   none|led <model>|rgb <model>
  hwdesc tx    <freq_hz> <channels> <pwr_levels> <type>
                                       e.g. hwdesc tx 30000000 4 4 colpitts
  hwdesc comment <text>                free text, max 183 chars


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  FLASH DEBUG COMMANDS
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  frd <page>            read 128 bytes from page (hex dump)
  ferase <page>         erase one flash page  (CAREFUL: pg60=config!)
  fwrite <page> <hex>   write 8 bytes at page start
  fmon [ms]             monitor flash SR for BSY/CFGBSY (default 5 s)

  Page reference:  pg60=TxConfig  pg61=LogHeader  pg62-67=LogData
  BLE stack starts at pg68 — never erase pg68+.


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  GEKON BUTTON  (hardware button, not UART)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  Short press  20–500 ms   → 2 LED blinks + extend wake window 5 s
                             (useful: press to get UART access window)
  Long press   ≥3 s        → enter Shutdown (~0.5 µA sleep)
                             wake: GEKON press again or power cycle

  After reset/power-on there is a ~10 s window where UART commands
  are accepted before MCU enters its first TX/sleep cycle.


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  GUI PANELS OVERVIEW
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  Left column:
    Profiles        — save/load named sets of TX settings
    Transmitter     — mode, channel, power, pulse/period
    LED             — indicator behaviour
    Temperature     — mode, interval, offset, live reading
    Battery ADC     — mode, period, scale, live reading + raw ADC
    Light ADC       — mode, period, live reading
    Actions         — Save / Reset / Sleep / Help

  Right column tabs:
    Status & RTC    — live values for all sensors + RTC set
    Schedule        — hours/days/months enable checkboxes
    HW Desc         — hardware descriptor edit & save
    Flash Log       — log config, dump, CSV export, graphs

  Terminal (bottom):
    All raw UART traffic shown colour-coded by source:
      green  = RTC lines          purple = Flash Log lines
      blue   = TX ON              orange = schedule
      gray   = TX OFF             magenta= diagnostics
      yellow = GEKON button       red    = shutdown/sleep

  Profiles are saved locally to: tx_beacon_profiles.json
"""


class TxBeaconGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("TX Beacon 30 MHz")
        self.configure(bg=BG)
        self.resizable(True, True)
        self.geometry("940x720")
        self.minsize(820, 600)

        self.serial    = None
        self.rx_queue  = queue.Queue()
        self._stop_rx  = threading.Event()
        self.rx_thread = None

        self.sv = {k: tk.StringVar(value="—") for k in
                   ["mode", "ch", "pwr", "pulse_ms", "period_ms",
                    "led_mode", "temp_c", "vdda_mv",
                    "batt_mv", "batt_pct", "batt_raw", "batt_vref", "light_raw", "light_lux",
                    "rtc_time", "rtc_date", "sched_status"]}

        self._sched_enabled  = tk.BooleanVar(value=False)
        self._sched_widgets  = []
        self._hour_vars      = [tk.BooleanVar() for _ in range(24)]
        self._day_vars       = [tk.BooleanVar() for _ in range(7)]
        self._month_vars     = [tk.BooleanVar() for _ in range(12)]
        self._sv_hours_disp  = tk.StringVar(value="—")
        self._sv_days_disp   = tk.StringVar(value="—")
        self._sv_months_disp = tk.StringVar(value="—")
        self._sv_active_now  = tk.StringVar(value="—")
        self._active_lbl     = None

        # Flash log tab
        self._log_in_dump     = False
        self._log_csv_buf     = []
        self._log_dump_tree   = None
        self._log_sv_used     = tk.StringVar(value="0")
        self._log_sv_free     = tk.StringVar(value="768")
        self._log_sv_mode     = tk.StringVar(value="—")
        self._log_sv_oflow    = tk.StringVar(value="—")
        self._log_mask_temp   = tk.BooleanVar(value=True)
        self._log_mask_light  = tk.BooleanVar(value=False)
        self._log_mask_batp   = tk.BooleanVar(value=True)
        self._log_mask_batmv  = tk.BooleanVar(value=False)
        self._log_dt_var      = tk.StringVar(value="60")
        self._log_dl_var      = tk.StringVar(value="300")
        self._log_db_var      = tk.StringVar(value="3600")
        self._log_mode_var    = tk.StringVar(value="always")
        self._log_oflow_var   = tk.StringVar(value="circular")
        self._log_ckp_var     = tk.StringVar(value="16")
        self._log_from_var    = tk.StringVar(value="0")
        self._log_count_var   = tk.StringVar(value="0")
        self._log_get_var     = tk.StringVar(value="0")
        self._log_prog_var    = tk.DoubleVar(value=0.0)
        self._log_ts_var      = tk.StringVar(value="boot")
        self._log_ts_as_date  = tk.BooleanVar(value=False)
        # Row graying / sync / verify
        self._log_row_widgets  = {}
        self._log_sync_vars    = {
            'temp':  tk.BooleanVar(value=False),
            'light': tk.BooleanVar(value=False),
            'bat':   tk.BooleanVar(value=False),
        }
        self._flash_ind_vars   = {
            'temp':  tk.StringVar(value=""),
            'light': tk.StringVar(value=""),
            'bat':   tk.StringVar(value=""),
        }
        self._verify_mask_cbs  = []
        self._verify_active    = False
        self._verify_snapshot  = {}
        self._verify_received  = {}
        # Flash memory calculator
        self._calc_total_var   = tk.StringVar(value="768")
        self._calc_free_var    = tk.StringVar(value="768")
        self._calc_temp_en     = tk.BooleanVar(value=True)
        self._calc_temp_iv     = tk.StringVar(value="60")
        self._calc_light_en    = tk.BooleanVar(value=False)
        self._calc_light_iv    = tk.StringVar(value="300")
        self._calc_bat_en      = tk.BooleanVar(value=True)
        self._calc_bat_iv      = tk.StringVar(value="3600")
        self._calc_mode_var    = tk.StringVar(value="always")
        self._calc_eff_var     = tk.StringVar(value="100")
        self._calc_target_days = tk.StringVar(value="7")
        self._calc_target_pct  = tk.StringVar(value="90")
        self._calc_rph_var     = tk.StringVar(value="—")
        self._calc_hrs_var     = tk.StringVar(value="—")
        self._calc_days_var    = tk.StringVar(value="—")
        self._calc_budget_var  = tk.StringVar(value="—")
        self._calc_sugg_iv_var = tk.StringVar(value="—")

        self._settings        = self._load_settings()
        self._custom_profiles = self._load_profiles_file()
        self._profile_var     = tk.StringVar(value=list(PRESET_PROFILES)[0])
        self._profile_om      = None

        self._apply_style()
        self._build_ui()
        self._refresh_ports()
        self.after(80, self._poll_queue)

    # ── Style ─────────────────────────────────────────────────────────────────
    def _apply_style(self):
        s = ttk.Style(); s.theme_use("clam")
        s.configure(".",           background=BG, foreground=FG, font=("Segoe UI", 9))
        s.configure("TFrame",      background=BG)
        s.configure("TLabelframe", background=BG, foreground=BLUE,
                    bordercolor=ACCENT, lightcolor=BG3, darkcolor=BG3, borderwidth=1)
        s.configure("TLabelframe.Label", background=BG, foreground=BLUE,
                    font=("Segoe UI", 9, "bold"))
        s.configure("TLabel",      background=BG, foreground=FG)
        s.configure("TNotebook",   background=BG3, tabmargins=[2, 2, 2, 0])
        s.configure("TNotebook.Tab", background=BG3, foreground=FG, padding=[8, 3])
        s.map("TNotebook.Tab",     background=[("selected", BG), ("active", BG2)])
        s.configure("TButton",     background=BG3, foreground=FG, padding=(6, 3), relief="flat")
        s.map("TButton",           background=[("active", "#b8c0cc")])
        s.configure("Green.TButton",  background="#166534", foreground="#fff", padding=(6, 3))
        s.map("Green.TButton",        background=[("active", "#14532d")])
        s.configure("Red.TButton",    background="#b91c1c", foreground="#fff", padding=(6, 3))
        s.map("Red.TButton",          background=[("active", "#991b1b")])
        s.configure("Danger.TButton", background="#7c2d12", foreground="#fff", padding=(6, 3))
        s.map("Danger.TButton",       background=[("active", "#6b2110")])
        s.configure("Accent.TButton", background="#1d4ed8", foreground="#fff", padding=(6, 3))
        s.map("Accent.TButton",       background=[("active", "#1e40af")])
        s.configure("Save.TButton",   background="#15803d", foreground="#fff",
                    padding=(6, 6), font=("Segoe UI", 10, "bold"))
        s.map("Save.TButton",         background=[("active", "#166534")])
        s.configure("Warn.TButton",   background="#d97706", foreground="#fff", padding=(6, 3))
        s.map("Warn.TButton",         background=[("active", "#b45309")])

    # ── UI skeleton ───────────────────────────────────────────────────────────
    def _build_ui(self):
        self.columnconfigure(0, weight=1, minsize=390)
        self.columnconfigure(1, weight=1, minsize=390)
        self.rowconfigure(1, weight=1)

        self._build_conn_bar()
        self._build_left(row=1)
        self._build_right(row=1)
        self._build_log(row=2)
        self._setup_traces()

    # ── Connection bar ────────────────────────────────────────────────────────
    def _build_conn_bar(self):
        f = tk.Frame(self, bg=BG3, pady=4)
        f.grid(row=0, column=0, columnspan=2, sticky="ew")

        cb_opts = dict(bg=BG2, fg=FG, activebackground=ACCENT, activeforeground="#fff",
                       relief="groove", font=("Segoe UI", 9), borderwidth=1,
                       highlightthickness=1, highlightbackground=BG3)

        tk.Label(f, text="  Port:", bg=BG3, fg=FG).pack(side="left")
        self.port_var = tk.StringVar()
        self.port_cb  = tk.OptionMenu(f, self.port_var, "")
        self.port_cb.config(**cb_opts)
        self.port_cb["menu"].config(bg=BG2, fg=FG, activebackground=ACCENT,
                                    activeforeground="#fff", font=("Segoe UI", 9))
        self.port_cb.pack(side="left", padx=(2, 6))

        ttk.Button(f, text="Refresh", command=self._refresh_ports, width=7).pack(side="left", padx=2)
        self.btn_conn = ttk.Button(f, text="Connect", style="Accent.TButton",
                                   command=self._toggle_connect, width=10)
        self.btn_conn.pack(side="left", padx=(4, 8))
        self.conn_lbl = tk.Label(f, text="● Disconnected", fg=RED, bg=BG3,
                                 font=("Segoe UI", 9, "bold"))
        self.conn_lbl.pack(side="left")

    # ── Left column ───────────────────────────────────────────────────────────
    def _build_left(self, row):
        outer = tk.Frame(self, bg=BG)
        outer.grid(row=row, column=0, sticky="nsew", padx=(6, 3), pady=6)
        outer.columnconfigure(0, weight=1)

        self._build_profiles_panel(outer, row=0)
        self._build_transmitter_panel(outer, row=1)
        self._build_led_panel(outer, row=2)
        self._build_temp_panel(outer, row=3)
        self._build_batt_panel(outer, row=4)
        self._build_light_panel(outer, row=5)
        self._build_actions_panel(outer, row=6)

    # ── Profiles ──────────────────────────────────────────────────────────────
    def _build_profiles_panel(self, outer, row):
        pf = ttk.LabelFrame(outer, text="  Profiles  ", padding=(8, 5))
        pf.grid(row=row, column=0, sticky="ew", pady=(0, 5))
        pf.columnconfigure(0, weight=1)

        rf = tk.Frame(pf, bg=BG); rf.grid(row=0, column=0, sticky="ew")
        rf.columnconfigure(0, weight=1)

        cb_opts = dict(bg=BG2, fg=FG, activebackground=ACCENT, activeforeground="#fff",
                       relief="groove", font=("Segoe UI", 9), borderwidth=1,
                       highlightthickness=1, highlightbackground=BG3, anchor="w")

        all_names = list(self._get_all_profiles().keys())
        self._profile_var.set(all_names[0])
        self._profile_om = tk.OptionMenu(rf, self._profile_var, *all_names)
        self._profile_om.config(**cb_opts)
        self._profile_om["menu"].config(bg=BG2, fg=FG, activebackground=ACCENT,
                                        activeforeground="#fff", font=("Segoe UI", 9))
        self._profile_om.grid(row=0, column=0, sticky="ew", padx=(0, 4), ipady=1)

        ttk.Button(rf, text="▶ Load", style="Accent.TButton",
                   command=self._load_selected_profile).grid(row=0, column=1, padx=2)
        ttk.Button(rf, text="💾 Save as…",
                   command=self._save_as_profile).grid(row=0, column=2, padx=2)
        ttk.Button(rf, text="✕", style="Red.TButton", width=3,
                   command=self._delete_profile).grid(row=0, column=3, padx=(2, 0))

    # ── Transmitter (compact inline) ──────────────────────────────────────────
    def _build_transmitter_panel(self, outer, row):
        sp_opts = dict(bg=BG2, fg=FG, insertbackground=FG,
                       buttonbackground=BG3, relief="groove",
                       highlightthickness=1, highlightbackground=BG3,
                       font=("Consolas", 9))

        tf = ttk.LabelFrame(outer, text="  Transmitter  ", padding=(8, 5))
        tf.grid(row=row, column=0, sticky="ew", pady=(0, 5))
        tf.columnconfigure(1, weight=1)

        # Mode inline
        tk.Label(tf, text="Mode:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8, "bold"), width=8, anchor="w"
                 ).grid(row=0, column=0, sticky="w", pady=2)
        mf = tk.Frame(tf, bg=BG); mf.grid(row=0, column=1, columnspan=3, sticky="w")
        self._mode_var = tk.StringVar(value="pulse")
        for val, lbl in [("off","OFF"), ("pulse","Pulse"), ("cont","Cont"), ("eco","Eco")]:
            ttk.Radiobutton(mf, text=lbl, variable=self._mode_var, value=val,
                            command=lambda v=val: self._cmd(f"mode {v}")
                            ).pack(side="left", padx=6)

        # Channel inline
        tk.Label(tf, text="Channel:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8, "bold"), width=8, anchor="w"
                 ).grid(row=1, column=0, sticky="w", pady=2)
        cf = tk.Frame(tf, bg=BG); cf.grid(row=1, column=1, columnspan=3, sticky="w")
        self._ch_var = tk.IntVar(value=0)
        for i in range(4):
            ttk.Radiobutton(cf, text=f"CH{i}", variable=self._ch_var, value=i,
                            command=lambda v=i: self._cmd(f"ch {v}")
                            ).pack(side="left", padx=5)

        # Power inline
        tk.Label(tf, text="Power:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8, "bold"), width=8, anchor="w"
                 ).grid(row=2, column=0, sticky="w", pady=2)
        pf2 = tk.Frame(tf, bg=BG); pf2.grid(row=2, column=1, columnspan=3, sticky="w")
        self._pwr_var = tk.IntVar(value=3)
        for i, lbl in enumerate(["PWR1","PWR2","PWR3","PWR4"], start=1):
            ttk.Radiobutton(pf2, text=lbl, variable=self._pwr_var, value=i,
                            command=lambda v=i: self._cmd(f"pwr {v}")
                            ).pack(side="left", padx=4)

        ttk.Separator(tf, orient="horizontal").grid(row=3, column=0, columnspan=4,
                                                     sticky="ew", pady=4)

        # Timing
        self._pulse_var  = tk.StringVar(value="23")
        self._period_var = tk.StringVar(value="2000")
        for r, (lbl, var, lo, hi, cmd_fn) in enumerate([
            ("Pulse (ms):",  self._pulse_var,  1,       60000,
             lambda: self._cmd(f"pulse {self._pulse_var.get()}")),
            ("Period (ms):", self._period_var, 100, 3600000,
             lambda: self._cmd(f"period {self._period_var.get()}")),
        ], start=4):
            tk.Label(tf, text=lbl, bg=BG, fg=FG_DIM,
                     font=("Segoe UI", 8, "bold"), width=8, anchor="w"
                     ).grid(row=r, column=0, sticky="w", pady=2)
            tk.Spinbox(tf, textvariable=var, from_=lo, to=hi, width=10, **sp_opts
                       ).grid(row=r, column=1, sticky="w", padx=4, pady=2)
            ttk.Button(tf, text="Set", width=4, command=cmd_fn
                       ).grid(row=r, column=2, padx=2)


    # ── LED ───────────────────────────────────────────────────────────────────
    def _build_led_panel(self, outer, row):
        lf = ttk.LabelFrame(outer, text="  LED  ", padding=(8, 5))
        lf.grid(row=row, column=0, sticky="ew", pady=(0, 5))
        for c in range(4): lf.columnconfigure(c, weight=1)
        self._led_mode_var = tk.StringVar(value="heartbeat")
        for c, (val, lbl) in enumerate([("off","OFF"),("on","ON"),
                                         ("heartbeat","Heartbeat"),("tx","TX sync")]):
            ttk.Radiobutton(lf, text=lbl, variable=self._led_mode_var, value=val,
                            command=lambda v=val: self._cmd(f"led {v}")
                            ).grid(row=0, column=c, sticky="w", padx=4, pady=3)

    # ── Temperature ───────────────────────────────────────────────────────────
    def _build_temp_panel(self, outer, row):
        sp_opts = dict(bg=BG2, fg=FG, insertbackground=FG,
                       buttonbackground=BG3, relief="groove",
                       highlightthickness=1, highlightbackground=BG3,
                       font=("Consolas", 9))

        tf = ttk.LabelFrame(outer, text="  Temperature  ", padding=(8, 5))
        tf.grid(row=row, column=0, sticky="ew", pady=(0, 5))
        tf.columnconfigure(0, weight=1)

        self._temp_mode_var   = tk.StringVar(value="off")
        self._temp_period_var = tk.StringVar(value="1")
        self._temp_offset_var = tk.StringVar(value="0.0")

        # Row 0: three mutually exclusive mode radiobuttons
        mf = tk.Frame(tf, bg=BG); mf.grid(row=0, column=0, sticky="ew", pady=(0, 2))
        for val, lbl in [("off", "Off"), ("periodic", "Periodic"), ("tx", "Before TX")]:
            ttk.Radiobutton(mf, text=lbl, variable=self._temp_mode_var, value=val,
                            command=self._on_temp_mode_change
                            ).pack(side="left", padx=6)

        # Row 1: period + calibration offset
        po_f = tk.Frame(tf, bg=BG); po_f.grid(row=1, column=0, sticky="ew", pady=(2, 4))
        tk.Label(po_f, text="Period:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Spinbox(po_f, textvariable=self._temp_period_var, from_=1, to=255, width=4, **sp_opts
                   ).pack(side="left", padx=(2, 1))
        tk.Label(po_f, text="s", bg=BG, fg=FG_DIM, font=("Segoe UI", 8)).pack(side="left")
        ttk.Button(po_f, text="Set", width=4,
                   command=lambda: self._cmd(f"temp period {self._temp_period_var.get()}")
                   ).pack(side="left", padx=(2, 12))

        tk.Label(po_f, text="Offset:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Spinbox(po_f, textvariable=self._temp_offset_var,
                   from_=-9.9, to=9.9, increment=0.1, format="%.1f",
                   width=5, **sp_opts
                   ).pack(side="left", padx=(2, 1))
        tk.Label(po_f, text="°C", bg=BG, fg=FG_DIM, font=("Segoe UI", 8)).pack(side="left")
        ttk.Button(po_f, text="Set", width=4,
                   command=lambda: self._cmd(f"temp offset {float(self._temp_offset_var.get()):.1f}")
                   ).pack(side="left", padx=(2, 0))

        # Row 2: live readings + Read now button
        rf = tk.Frame(tf, bg=BG); rf.grid(row=2, column=0, sticky="ew", pady=(0, 0))
        rf.columnconfigure(0, weight=1)
        lv = tk.Frame(rf, bg=BG); lv.grid(row=0, column=0, sticky="w")
        tk.Label(lv, text="chip:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Label(lv, textvariable=self.sv["temp_c"],
                 bg=BG2, fg=BLUE, font=("Consolas", 9, "bold"),
                 padx=4, relief="groove", borderwidth=1
                 ).pack(side="left", padx=(2, 8))
        tk.Label(lv, text="VDDA:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Label(lv, textvariable=self.sv["vdda_mv"],
                 bg=BG2, fg=FG_DIM, font=("Consolas", 9),
                 padx=4, relief="groove", borderwidth=1
                 ).pack(side="left", padx=(2, 0))
        ttk.Button(rf, text="Read now",
                   command=lambda: self._cmd("temp read")
                   ).grid(row=0, column=1, padx=(6, 0))
        fi = tk.Frame(tf, bg=BG); fi.grid(row=3, column=0, sticky="w", pady=(1, 0))
        tk.Label(fi, textvariable=self._flash_ind_vars['temp'],
                 bg=BG, fg="#a78bfa", font=("Segoe UI", 7, "italic")).pack(side="left")

    # ── Battery ───────────────────────────────────────────────────────────────
    def _build_batt_panel(self, outer, row):
        sp_opts = dict(bg=BG2, fg=FG, insertbackground=FG,
                       buttonbackground=BG3, relief="groove",
                       highlightthickness=1, highlightbackground=BG3,
                       font=("Consolas", 9))

        bf = ttk.LabelFrame(outer, text="  Battery ADC  ", padding=(8, 5))
        bf.grid(row=row, column=0, sticky="ew", pady=(0, 5))
        bf.columnconfigure(0, weight=1)

        self._batt_mode_var   = tk.StringVar(value="off")
        self._batt_period_var = tk.StringVar(value="5")
        self._batt_scale_var  = tk.StringVar(value="2.0")

        # Row 0: mode radiobuttons
        mf = tk.Frame(bf, bg=BG); mf.grid(row=0, column=0, sticky="ew", pady=(0, 2))
        for val, lbl in [("off", "Off"), ("periodic", "Periodic")]:
            ttk.Radiobutton(mf, text=lbl, variable=self._batt_mode_var, value=val,
                            command=self._on_batt_mode_change
                            ).pack(side="left", padx=6)

        # Row 1: period + scale
        pf = tk.Frame(bf, bg=BG); pf.grid(row=1, column=0, sticky="ew", pady=(2, 4))
        tk.Label(pf, text="Period:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Spinbox(pf, textvariable=self._batt_period_var, from_=1, to=255, width=4, **sp_opts
                   ).pack(side="left", padx=(2, 1))
        tk.Label(pf, text="s", bg=BG, fg=FG_DIM, font=("Segoe UI", 8)).pack(side="left")
        ttk.Button(pf, text="Set", width=4,
                   command=lambda: self._cmd(f"batt period {self._batt_period_var.get()}")
                   ).pack(side="left", padx=(2, 12))
        tk.Label(pf, text="Scale×:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Spinbox(pf, textvariable=self._batt_scale_var,
                   from_=0.1, to=25.0, increment=0.1, format="%.1f",
                   width=5, **sp_opts
                   ).pack(side="left", padx=(2, 1))
        ttk.Button(pf, text="Set", width=4,
                   command=lambda: self._cmd(f"batt scale {float(self._batt_scale_var.get()):.1f}")
                   ).pack(side="left", padx=(2, 0))

        # Row 2: live reading + Read now
        rf = tk.Frame(bf, bg=BG); rf.grid(row=2, column=0, sticky="ew")
        rf.columnconfigure(0, weight=1)
        lv = tk.Frame(rf, bg=BG); lv.grid(row=0, column=0, sticky="w")
        tk.Label(lv, text="mV:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Label(lv, textvariable=self.sv["batt_mv"],
                 bg=BG2, fg=BLUE, font=("Consolas", 9, "bold"),
                 padx=4, relief="groove", borderwidth=1
                 ).pack(side="left", padx=(2, 8))
        tk.Label(lv, text="%:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Label(lv, textvariable=self.sv["batt_pct"],
                 bg=BG2, fg=FG_DIM, font=("Consolas", 9),
                 padx=4, relief="groove", borderwidth=1
                 ).pack(side="left", padx=(2, 0))
        ttk.Button(rf, text="Read now",
                   command=lambda: self._cmd("batt read")
                   ).grid(row=0, column=1, padx=(6, 0))

        # Row 3: raw ADC values
        rw = tk.Frame(bf, bg=BG); rw.grid(row=3, column=0, sticky="w", pady=(2, 0))
        tk.Label(rw, text="raw:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Label(rw, textvariable=self.sv["batt_raw"],
                 bg=BG2, fg=FG_DIM, font=("Consolas", 9),
                 padx=4, relief="groove", borderwidth=1
                 ).pack(side="left", padx=(2, 10))
        tk.Label(rw, text="vref:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Label(rw, textvariable=self.sv["batt_vref"],
                 bg=BG2, fg=FG_DIM, font=("Consolas", 9),
                 padx=4, relief="groove", borderwidth=1
                 ).pack(side="left", padx=(2, 0))
        fi = tk.Frame(bf, bg=BG); fi.grid(row=4, column=0, sticky="w", pady=(1, 0))
        tk.Label(fi, textvariable=self._flash_ind_vars['bat'],
                 bg=BG, fg="#a78bfa", font=("Segoe UI", 7, "italic")).pack(side="left")

    def _on_batt_mode_change(self):
        self._cmd(f"batt mode {self._batt_mode_var.get()}")

    # ── Light Sensor ──────────────────────────────────────────────────────────
    def _build_light_panel(self, outer, row):
        sp_opts = dict(bg=BG2, fg=FG, insertbackground=FG,
                       buttonbackground=BG3, relief="groove",
                       highlightthickness=1, highlightbackground=BG3,
                       font=("Consolas", 9))

        lf = ttk.LabelFrame(outer, text="  Light Sensor ADC  ", padding=(8, 5))
        lf.grid(row=row, column=0, sticky="ew", pady=(0, 5))
        lf.columnconfigure(0, weight=1)

        self._light_mode_var   = tk.StringVar(value="off")
        self._light_period_var = tk.StringVar(value="5")

        # Row 0: mode radiobuttons
        mf = tk.Frame(lf, bg=BG); mf.grid(row=0, column=0, sticky="ew", pady=(0, 2))
        for val, lbl in [("off", "Off"), ("periodic", "Periodic")]:
            ttk.Radiobutton(mf, text=lbl, variable=self._light_mode_var, value=val,
                            command=self._on_light_mode_change
                            ).pack(side="left", padx=6)

        # Row 1: period
        pf = tk.Frame(lf, bg=BG); pf.grid(row=1, column=0, sticky="ew", pady=(2, 4))
        tk.Label(pf, text="Period:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Spinbox(pf, textvariable=self._light_period_var, from_=1, to=255, width=4, **sp_opts
                   ).pack(side="left", padx=(2, 1))
        tk.Label(pf, text="s", bg=BG, fg=FG_DIM, font=("Segoe UI", 8)).pack(side="left")
        ttk.Button(pf, text="Set", width=4,
                   command=lambda: self._cmd(f"light period {self._light_period_var.get()}")
                   ).pack(side="left", padx=(2, 0))

        # Row 2: live reading + Read now
        rf = tk.Frame(lf, bg=BG); rf.grid(row=2, column=0, sticky="ew")
        rf.columnconfigure(0, weight=1)
        lv = tk.Frame(rf, bg=BG); lv.grid(row=0, column=0, sticky="w")
        tk.Label(lv, text="raw:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Label(lv, textvariable=self.sv["light_raw"],
                 bg=BG2, fg=BLUE, font=("Consolas", 9, "bold"),
                 padx=4, relief="groove", borderwidth=1
                 ).pack(side="left", padx=(2, 8))
        tk.Label(lv, text="lux~:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Label(lv, textvariable=self.sv["light_lux"],
                 bg=BG2, fg=FG_DIM, font=("Consolas", 9),
                 padx=4, relief="groove", borderwidth=1
                 ).pack(side="left", padx=(2, 0))
        ttk.Button(rf, text="Read now",
                   command=lambda: self._cmd("light read")
                   ).grid(row=0, column=1, padx=(6, 0))
        fi = tk.Frame(lf, bg=BG); fi.grid(row=3, column=0, sticky="w", pady=(1, 0))
        tk.Label(fi, textvariable=self._flash_ind_vars['light'],
                 bg=BG, fg="#a78bfa", font=("Segoe UI", 7, "italic")).pack(side="left")

    def _on_light_mode_change(self):
        self._cmd(f"light mode {self._light_mode_var.get()}")

    def _on_temp_mode_change(self):
        self._cmd(f"temp mode {self._temp_mode_var.get()}")

    def _on_rtc_live_change(self):
        self._cmd(f"rtc live {'on' if self._rtc_live_var.get() else 'off'}")

    # ── Actions ───────────────────────────────────────────────────────────────
    def _build_actions_panel(self, outer, row):
        af = ttk.LabelFrame(outer, text="  Actions  ", padding=(8, 5))
        af.grid(row=row, column=0, sticky="ew")
        for c in range(4): af.columnconfigure(c, weight=1)

        ttk.Button(af, text="Status",
                   command=lambda: self._cmd("status")
                   ).grid(row=0, column=0, padx=3, pady=3, sticky="ew")
        ttk.Button(af, text="Regs",
                   command=lambda: self._cmd("regs")
                   ).grid(row=0, column=1, padx=3, pady=3, sticky="ew")
        ttk.Button(af, text="Help",
                   command=self._show_help
                   ).grid(row=0, column=2, padx=3, pady=3, sticky="ew")
        ttk.Button(af, text="Reset MCU", style="Warn.TButton",
                   command=self._do_reset
                   ).grid(row=0, column=3, padx=3, pady=3, sticky="ew")
        ttk.Button(af, text="SLEEP (Shutdown)", style="Danger.TButton",
                   command=self._do_sleep
                   ).grid(row=1, column=0, columnspan=4, padx=3, pady=3, sticky="ew")

        ttk.Separator(af, orient="horizontal").grid(row=2, column=0, columnspan=4,
                                                     sticky="ew", pady=(4, 0))
        ttk.Button(af, text="💾   SAVE TO FLASH  &  RESTART MCU", style="Save.TButton",
                   command=self._do_save
                   ).grid(row=3, column=0, columnspan=4, padx=3, pady=(4, 3), sticky="ew")

    # ── Right column (Notebook) ───────────────────────────────────────────────
    def _build_right(self, row):
        outer = tk.Frame(self, bg=BG)
        outer.grid(row=row, column=1, sticky="nsew", padx=(3, 6), pady=6)
        outer.columnconfigure(0, weight=1)
        outer.rowconfigure(0, weight=1)

        nb = ttk.Notebook(outer)
        nb.grid(row=0, column=0, sticky="nsew")

        t_status = tk.Frame(nb, bg=BG)
        t_sched  = tk.Frame(nb, bg=BG)
        t_hw     = tk.Frame(nb, bg=BG)
        t_log    = tk.Frame(nb, bg=BG)
        nb.add(t_status, text="  Status & RTC  ")
        nb.add(t_sched,  text="  Schedule  ")
        nb.add(t_hw,     text="  HW Desc  ")
        nb.add(t_log,    text="  Flash Log  ")

        t_status.columnconfigure(0, weight=1)
        self._build_status_panel(t_status)
        self._build_rtc_panel(t_status)

        t_sched.columnconfigure(0, weight=1)
        self._build_sched_panel(t_sched)

        t_hw.columnconfigure(0, weight=1)
        self._build_hwdesc_panel(t_hw)

        t_log.columnconfigure(0, weight=1)
        t_log.rowconfigure(2, weight=1)
        self._build_flash_log_panel(t_log)

    # ── Flash Log panel ───────────────────────────────────────────────────────
    def _build_flash_log_panel(self, parent):
        sp = dict(bg=BG2, fg=FG, insertbackground=FG, buttonbackground=BG3,
                  relief="groove", highlightthickness=1, highlightbackground=BG3,
                  font=("Consolas", 9))
        cb_menu = dict(bg=BG2, fg=FG, activebackground=ACCENT, activeforeground="#fff",
                       relief="groove", font=("Segoe UI", 8), borderwidth=1,
                       highlightthickness=1, highlightbackground=BG3)

        # ── Config ────────────────────────────────────────────────────────────
        cf = ttk.LabelFrame(parent, text="  Config  ", padding=(8, 5))
        cf.grid(row=0, column=0, sticky="ew", padx=6, pady=(6, 3))
        cf.columnconfigure(0, weight=1)

        # Mask
        mf = tk.Frame(cf, bg=BG); mf.grid(row=0, column=0, sticky="w")
        tk.Label(mf, text="Mask:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        self._verify_mask_cbs = []
        for var, lbl in [(self._log_mask_temp, "Temp"),
                         (self._log_mask_light, "Light"),
                         (self._log_mask_batp,  "Bat%"),
                         (self._log_mask_batmv, "BatmV")]:
            _cb = tk.Checkbutton(mf, text=lbl, variable=var, bg=BG, fg=FG,
                                 selectcolor=BG2, activebackground=BG,
                                 font=("Segoe UI", 8))
            _cb.pack(side="left", padx=3)
            self._verify_mask_cbs.append(_cb)
        ttk.Button(mf, text="Set mask",
                   command=self._log_set_mask).pack(side="left", padx=(8, 0))

        # Intervals — Read (driver) vs Log write (flash)
        ivf = tk.Frame(cf, bg=BG); ivf.grid(row=1, column=0, sticky="w", pady=(4, 0))
        # header
        tk.Label(ivf, text="Sensor", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8, "bold"), width=5, anchor="w").grid(
                 row=0, column=0, padx=(0, 4))
        tk.Label(ivf, text="Read (driver), s", bg=BG, fg="#7ec8e3",
                 font=("Segoe UI", 8)).grid(row=0, column=1, columnspan=3, padx=(0, 6))
        tk.Label(ivf, text="→", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 9)).grid(row=0, column=4, padx=4)
        tk.Label(ivf, text="Write (flash), s", bg=BG, fg="#a78bfa",
                 font=("Segoe UI", 8)).grid(row=0, column=5, columnspan=3)
        tk.Label(ivf, text="Sync", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).grid(row=0, column=8, padx=(6, 0))
        # rows — store widget refs for state management
        _irows = [
            ("Temp",  "temp",  self._temp_period_var,  "temp period",  self._log_dt_var, "log temp"),
            ("Light", "light", self._light_period_var, "light period", self._log_dl_var, "log light"),
            ("Bat",   "bat",   self._batt_period_var,  "batt period",  self._log_db_var, "log bat"),
        ]
        for r, (lbl, key, rv, rc, wv, wc) in enumerate(_irows, start=1):
            row_lbl = tk.Label(ivf, text=lbl+":", bg=BG, fg=FG_DIM,
                               font=("Segoe UI", 8), width=5, anchor="w")
            row_lbl.grid(row=r, column=0, padx=(0, 4), pady=1)
            r_sp = tk.Spinbox(ivf, textvariable=rv, from_=1, to=65535, width=5, **sp)
            r_sp.grid(row=r, column=1, padx=(0, 2))
            tk.Label(ivf, text="s", bg=BG, fg=FG_DIM,
                     font=("Segoe UI", 8)).grid(row=r, column=2, padx=(0, 2))
            r_btn = ttk.Button(ivf, text="Set", width=3,
                               command=lambda v=rv, k=rc: self._cmd(f"{k} {v.get()}"))
            r_btn.grid(row=r, column=3, padx=(0, 6))
            arr = tk.Label(ivf, text="→", bg=BG, fg=FG_DIM, font=("Segoe UI", 9))
            arr.grid(row=r, column=4, padx=4)
            w_sp = tk.Spinbox(ivf, textvariable=wv, from_=1, to=65535, width=5, **sp)
            w_sp.grid(row=r, column=5, padx=(0, 2))
            tk.Label(ivf, text="s", bg=BG, fg=FG_DIM,
                     font=("Segoe UI", 8)).grid(row=r, column=6, padx=(0, 2))
            w_btn = ttk.Button(ivf, text="Set", width=3,
                               command=lambda v=wv, k=wc: self._cmd(f"{k} {v.get()}"))
            w_btn.grid(row=r, column=7, padx=(0, 4))
            sync_cb = tk.Checkbutton(ivf, text="⇌", variable=self._log_sync_vars[key],
                                     bg=BG, fg=FG_DIM, selectcolor=BG2, activebackground=BG,
                                     font=("Segoe UI", 9))
            sync_cb.grid(row=r, column=8, padx=(4, 0))
            self._log_row_widgets[key] = {
                'row_lbl': row_lbl, 'read_ws': [r_sp, r_btn],
                'write_ws': [w_sp, w_btn], 'arrow_w': arr,
                'sync_cb': sync_cb, 'write_sp': w_sp,
            }

        # Mode / Overflow / CKP
        mof = tk.Frame(cf, bg=BG); mof.grid(row=2, column=0, sticky="w", pady=(3, 0))
        tk.Label(mof, text="Write:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        om_w = tk.OptionMenu(mof, self._log_mode_var, "always", "onchange", "adaptive")
        om_w.config(**cb_menu, width=9)
        om_w["menu"].config(bg=BG2, fg=FG, activebackground=ACCENT,
                            activeforeground="#fff", font=("Segoe UI", 8))
        om_w.pack(side="left", padx=(2, 1))
        _MODE_NUM = {"always": "0", "onchange": "1", "adaptive": "2"}
        ttk.Button(mof, text="Set", width=3,
                   command=lambda: self._cmd(
                       f"log mode {_MODE_NUM.get(self._log_mode_var.get(), '0')}")
                   ).pack(side="left", padx=(0, 10))

        tk.Label(mof, text="Oflow:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        om_o = tk.OptionMenu(mof, self._log_oflow_var, "circular", "stop")
        om_o.config(**cb_menu, width=7)
        om_o["menu"].config(bg=BG2, fg=FG, activebackground=ACCENT,
                            activeforeground="#fff", font=("Segoe UI", 8))
        om_o.pack(side="left", padx=(2, 1))
        ttk.Button(mof, text="Set", width=3,
                   command=lambda: self._cmd(
                       f"log overflow {'1' if self._log_oflow_var.get() == 'circular' else '0'}")
                   ).pack(side="left", padx=(0, 10))

        tk.Label(mof, text="CKP/N:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Spinbox(mof, textvariable=self._log_ckp_var, from_=2, to=255, width=4, **sp
                   ).pack(side="left", padx=(2, 1))
        ttk.Button(mof, text="Set", width=3,
                   command=lambda: self._cmd(
                       f"log ckp {self._log_ckp_var.get()}")
                   ).pack(side="left", padx=(0, 0))

        # Timestamp source
        tsf = tk.Frame(cf, bg=BG); tsf.grid(row=3, column=0, sticky="w", pady=(3, 0))
        tk.Label(tsf, text="Timestamp:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        for val, lbl in [("boot", "Boot (s since reset)"), ("rtc", "RTC calendar")]:
            ttk.Radiobutton(tsf, text=lbl, variable=self._log_ts_var, value=val
                            ).pack(side="left", padx=4)
        ttk.Button(tsf, text="Set", width=3,
                   command=lambda: self._cmd(f"log ts {self._log_ts_var.get()}")
                   ).pack(side="left", padx=(4, 0))

        # ── Controls ──────────────────────────────────────────────────────────
        ctrl = ttk.LabelFrame(parent, text="  Controls  ", padding=(8, 5))
        ctrl.grid(row=1, column=0, sticky="ew", padx=6, pady=(0, 3))
        ctrl.columnconfigure(0, weight=1)

        bf = tk.Frame(ctrl, bg=BG); bf.grid(row=0, column=0, sticky="w")
        ttk.Button(bf, text="Log Info",
                   command=lambda: self._cmd("log info")).pack(side="left", padx=2)
        ttk.Button(bf, text="Get Config",
                   command=lambda: self._cmd("log get")).pack(side="left", padx=2)
        ttk.Button(bf, text="Calc",
                   command=lambda: self._cmd("log calc")).pack(side="left", padx=2)
        ttk.Button(bf, text="Write Now",
                   command=lambda: self._cmd("log write")).pack(side="left", padx=2)
        ttk.Button(bf, text="Pages",
                   command=lambda: self._cmd("log pages")).pack(side="left", padx=2)
        ttk.Button(bf, text="Clear Log", style="Warn.TButton",
                   command=self._log_clear_confirm).pack(side="left", padx=(12, 2))

        bf2 = tk.Frame(ctrl, bg=BG); bf2.grid(row=1, column=0, sticky="w", pady=(3, 0))
        ttk.Button(bf2, text="Write All & Verify", style="Green.TButton",
                   command=self._log_write_verify).pack(side="left", padx=2)
        tk.Label(bf2, text="— send all log config to MCU, read back and confirm",
                 bg=BG, fg=FG_DIM, font=("Segoe UI", 7, "italic")).pack(side="left", padx=(4, 0))

        sf2 = tk.Frame(ctrl, bg=BG); sf2.grid(row=2, column=0, sticky="w", pady=(5, 2))
        for lbl, var in [("used:", self._log_sv_used), ("free:", self._log_sv_free),
                          ("mode:", self._log_sv_mode), ("oflow:", self._log_sv_oflow)]:
            tk.Label(sf2, text=lbl, bg=BG, fg=FG_DIM,
                     font=("Segoe UI", 8)).pack(side="left")
            tk.Label(sf2, textvariable=var, bg=BG2, fg=BLUE,
                     font=("Consolas", 9, "bold"), padx=4,
                     relief="groove", borderwidth=1).pack(side="left", padx=(0, 8))

        ttk.Progressbar(ctrl, variable=self._log_prog_var,
                        maximum=100.0).grid(row=3, column=0, sticky="ew", pady=(2, 0))

        # ── Data ──────────────────────────────────────────────────────────────
        df = ttk.LabelFrame(parent, text="  Data  ", padding=(8, 5))
        df.grid(row=2, column=0, sticky="nsew", padx=6, pady=(0, 3))
        df.columnconfigure(0, weight=1)
        df.rowconfigure(1, weight=1)

        dc = tk.Frame(df, bg=BG); dc.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 3))
        ttk.Button(dc, text="Dump All",
                   command=self._log_dump_all).pack(side="left", padx=(0, 4))
        tk.Label(dc, text="From:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Spinbox(dc, textvariable=self._log_from_var, from_=0, to=767, width=4, **sp
                   ).pack(side="left", padx=(2, 2))
        tk.Label(dc, text="Count(0=all):", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Spinbox(dc, textvariable=self._log_count_var, from_=0, to=768, width=4, **sp
                   ).pack(side="left", padx=(2, 2))
        ttk.Button(dc, text="Dump",
                   command=self._log_dump_range).pack(side="left", padx=2)
        tk.Label(dc, text="  Rec#:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Spinbox(dc, textvariable=self._log_get_var, from_=0, to=767, width=4, **sp
                   ).pack(side="left", padx=(2, 2))
        ttk.Button(dc, text="Read",
                   command=lambda: self._cmd(f"log read {self._log_get_var.get()}")
                   ).pack(side="left", padx=2)

        cols = ("idx", "ts_s", "flags", "temp_c", "light", "bat_pct", "bat_mv")
        tv = ttk.Treeview(df, columns=cols, show="headings", height=7)
        self._log_dump_tree = tv
        for c, w, h in [("idx", 38, "idx"), ("ts_s", 65, "ts(s)"), ("flags", 58, "flags"),
                         ("temp_c", 62, "temp°C"), ("light", 55, "light"),
                         ("bat_pct", 50, "bat%"), ("bat_mv", 58, "batmV")]:
            tv.heading(c, text=h)
            tv.column(c, width=w, anchor="center", stretch=False)
        tv.grid(row=1, column=0, sticky="nsew")
        sb_tv = ttk.Scrollbar(df, orient="vertical", command=tv.yview)
        tv.configure(yscrollcommand=sb_tv.set)
        sb_tv.grid(row=1, column=1, sticky="ns")

        bc = tk.Frame(df, bg=BG); bc.grid(row=2, column=0, columnspan=2, sticky="w", pady=(4, 0))
        ttk.Button(bc, text="Save CSV to PC…",
                   command=self._log_save_csv).pack(side="left", padx=(0, 8))
        ttk.Button(bc, text="Clear table",
                   command=self._log_clear_tree).pack(side="left", padx=2)
        tk.Checkbutton(bc, text="ts → date (RTC mode)",
                       variable=self._log_ts_as_date,
                       command=self._log_refresh_ts_column,
                       bg=BG, fg=FG_DIM, selectcolor=BG2, activebackground=BG,
                       font=("Segoe UI", 8)).pack(side="left", padx=(16, 0))

        # ── Graphs ────────────────────────────────────────────────────────────
        gf = ttk.LabelFrame(parent, text="  Graphs  ", padding=(8, 5))
        gf.grid(row=3, column=0, sticky="ew", padx=6, pady=(0, 6))
        if _HAS_MPL:
            gb = tk.Frame(gf, bg=BG); gb.pack(fill="x")
            ttk.Button(gb, text="Plot Temp",
                       command=lambda: self._log_plot("temp")).pack(side="left", padx=2)
            ttk.Button(gb, text="Plot Battery",
                       command=lambda: self._log_plot("bat")).pack(side="left", padx=2)
            ttk.Button(gb, text="Plot Light",
                       command=lambda: self._log_plot("light")).pack(side="left", padx=2)
            ttk.Button(gb, text="Plot All",
                       command=lambda: self._log_plot("all")).pack(side="left", padx=2)
        else:
            tk.Label(gf, text="Install matplotlib for graphs:  pip install matplotlib",
                     bg=BG, fg=FG_DIM, font=("Segoe UI", 8)).pack(anchor="w")

        # ── Capacity Calculator ────────────────────────────────────────────────
        self._build_calc_panel(parent)

    # ── Flash capacity calculator ─────────────────────────────────────────────
    def _build_calc_panel(self, parent):
        sp = dict(bg=BG2, fg=FG, insertbackground=FG, buttonbackground=BG3,
                  relief="groove", highlightthickness=1, highlightbackground=BG3,
                  font=("Consolas", 9))
        cb_menu = dict(bg=BG2, fg=FG, activebackground=ACCENT, activeforeground="#fff",
                       relief="groove", font=("Segoe UI", 8), borderwidth=1,
                       highlightthickness=1, highlightbackground=BG3)

        cf = ttk.LabelFrame(parent, text="  Capacity Calculator  ", padding=(8, 6))
        cf.grid(row=4, column=0, sticky="ew", padx=6, pady=(0, 6))
        cf.columnconfigure(0, weight=1)

        # ── Row 0: import + capacity ──────────────────────────────────────────
        r0 = tk.Frame(cf, bg=BG); r0.grid(row=0, column=0, sticky="w")
        ttk.Button(r0, text="◀ Import config", style="Accent.TButton",
                   command=self._calc_import).pack(side="left", padx=(0, 12))
        tk.Label(r0, text="Total:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Spinbox(r0, textvariable=self._calc_total_var, from_=1, to=768, width=5, **sp
                   ).pack(side="left", padx=(2, 1))
        tk.Label(r0, text="rec", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left", padx=(0, 10))
        tk.Label(r0, text="Free:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Spinbox(r0, textvariable=self._calc_free_var, from_=0, to=768, width=5, **sp
                   ).pack(side="left", padx=(2, 1))
        tk.Label(r0, text="rec", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")

        # ── Row 1: sensors ────────────────────────────────────────────────────
        r1 = tk.Frame(cf, bg=BG); r1.grid(row=1, column=0, sticky="w", pady=(4, 0))
        tk.Label(r1, text="Sensors:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left", padx=(0, 4))
        for en_var, lbl, iv_var, fg_c in [
            (self._calc_temp_en,  "Temp",  self._calc_temp_iv,  "#7ec8e3"),
            (self._calc_bat_en,   "Bat",   self._calc_bat_iv,   "#7ec8e3"),
            (self._calc_light_en, "Light", self._calc_light_iv, "#7ec8e3"),
        ]:
            tk.Checkbutton(r1, text=lbl, variable=en_var, bg=BG, fg=fg_c,
                           selectcolor=BG2, activebackground=BG,
                           font=("Segoe UI", 8)).pack(side="left", padx=(0, 2))
            tk.Label(r1, text="every", bg=BG, fg=FG_DIM,
                     font=("Segoe UI", 8)).pack(side="left")
            tk.Spinbox(r1, textvariable=iv_var, from_=1, to=86400, width=6, **sp
                       ).pack(side="left", padx=(2, 1))
            tk.Label(r1, text="s  ", bg=BG, fg=FG_DIM,
                     font=("Segoe UI", 8)).pack(side="left")

        # ── Row 2: write mode + compression ──────────────────────────────────
        r2 = tk.Frame(cf, bg=BG); r2.grid(row=2, column=0, sticky="w", pady=(3, 0))
        tk.Label(r2, text="Write:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        om = tk.OptionMenu(r2, self._calc_mode_var, "always", "onchange", "adaptive")
        om.config(**cb_menu, width=9)
        om["menu"].config(bg=BG2, fg=FG, activebackground=ACCENT,
                          activeforeground="#fff", font=("Segoe UI", 8))
        om.pack(side="left", padx=(2, 10))
        tk.Label(r2, text="Compression:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Spinbox(r2, textvariable=self._calc_eff_var, from_=1, to=100, width=4, **sp
                   ).pack(side="left", padx=(2, 1))
        tk.Label(r2, text="%  (for onchange/adaptive — % of intervals that actually write)",
                 bg=BG, fg=FG_DIM, font=("Segoe UI", 7)).pack(side="left")

        # ── Row 3: separator ──────────────────────────────────────────────────
        tk.Frame(cf, bg=BG3, height=1).grid(row=3, column=0, sticky="ew", pady=(6, 4))

        # ── Row 4: forward result ─────────────────────────────────────────────
        r4 = tk.Frame(cf, bg=BG); r4.grid(row=4, column=0, sticky="w")
        tk.Label(r4, text="→ Records/hr:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Label(r4, textvariable=self._calc_rph_var,
                 bg=BG2, fg=BLUE, font=("Consolas", 9, "bold"),
                 padx=5, relief="groove", borderwidth=1).pack(side="left", padx=(3, 16))
        tk.Label(r4, text="Full in:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Label(r4, textvariable=self._calc_hrs_var,
                 bg=BG2, fg="#22c55e", font=("Consolas", 9, "bold"),
                 padx=5, relief="groove", borderwidth=1).pack(side="left", padx=(3, 4))
        tk.Label(r4, textvariable=self._calc_days_var,
                 bg=BG2, fg="#22c55e", font=("Consolas", 9, "bold"),
                 padx=5, relief="groove", borderwidth=1).pack(side="left", padx=(0, 0))

        # ── Row 5: separator / reverse ────────────────────────────────────────
        tk.Frame(cf, bg=BG3, height=1).grid(row=5, column=0, sticky="ew", pady=(6, 4))

        r5 = tk.Frame(cf, bg=BG); r5.grid(row=6, column=0, sticky="w")
        tk.Label(r5, text="Want to fill", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Spinbox(r5, textvariable=self._calc_target_pct, from_=1, to=100, width=4, **sp
                   ).pack(side="left", padx=(3, 1))
        tk.Label(r5, text="% in", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left", padx=(1, 3))
        tk.Spinbox(r5, textvariable=self._calc_target_days, from_=1, to=9999, width=5, **sp
                   ).pack(side="left", padx=(0, 1))
        tk.Label(r5, text="days:", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left", padx=(1, 8))
        tk.Label(r5, textvariable=self._calc_budget_var,
                 bg=BG2, fg="#a78bfa", font=("Consolas", 9, "bold"),
                 padx=5, relief="groove", borderwidth=1).pack(side="left", padx=(0, 8))
        tk.Label(r5, text="→ interval ≥", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Label(r5, textvariable=self._calc_sugg_iv_var,
                 bg=BG2, fg="#f59e0b", font=("Consolas", 9, "bold"),
                 padx=5, relief="groove", borderwidth=1).pack(side="left", padx=(3, 0))

        # ── Row 7: calculate button ───────────────────────────────────────────
        r7 = tk.Frame(cf, bg=BG); r7.grid(row=7, column=0, sticky="w", pady=(6, 0))
        ttk.Button(r7, text="Calculate", style="Accent.TButton",
                   command=self._calc_run).pack(side="left")
        tk.Label(r7, text=" — 16 bytes/record, 768 max records (6 × 2KB pages)",
                 bg=BG, fg=FG_DIM, font=("Segoe UI", 7, "italic")).pack(side="left")

    def _calc_import(self):
        self._calc_total_var.set("768")
        try:    free = int(self._log_sv_free.get())
        except: free = 768
        self._calc_free_var.set(str(min(free, 768)))
        self._calc_temp_en.set(self._log_mask_temp.get())
        self._calc_temp_iv.set(self._log_dt_var.get())
        self._calc_light_en.set(self._log_mask_light.get())
        self._calc_light_iv.set(self._log_dl_var.get())
        self._calc_bat_en.set(self._log_mask_batp.get())
        self._calc_bat_iv.set(self._log_db_var.get())
        self._calc_mode_var.set(self._log_mode_var.get())
        self._calc_run()

    def _calc_run(self):
        try:
            total = max(1, int(self._calc_total_var.get()))
            free  = max(0, min(int(self._calc_free_var.get()), total))
            mode  = self._calc_mode_var.get()
            eff   = max(1.0, min(100.0, float(self._calc_eff_var.get()))) / 100.0

            # Collect active sensor intervals
            sensors = []
            for en_var, iv_var in [(self._calc_temp_en,  self._calc_temp_iv),
                                   (self._calc_bat_en,   self._calc_bat_iv),
                                   (self._calc_light_en, self._calc_light_iv)]:
                if en_var.get():
                    iv = int(iv_var.get())
                    if iv > 0: sensors.append(iv)

            if not sensors:
                for sv in [self._calc_rph_var, self._calc_hrs_var,
                            self._calc_days_var, self._calc_budget_var, self._calc_sugg_iv_var]:
                    sv.set("—")
                return

            # Records/hr: fastest sensor drives the write rate
            # For ALWAYS: every min_interval → 1 record
            # For ON_CHANGE/ADAPTIVE: effective rate = raw_rate × efficiency
            min_iv = min(sensors)
            rph_raw = 3600.0 / min_iv
            rph = rph_raw if mode == "always" else rph_raw * eff

            self._calc_rph_var.set(f"{rph:.1f}")

            def _fmt_time(hours):
                if hours >= 24:
                    d, h = divmod(hours, 24)
                    return f"{hours:.1f}h ({d:.0f}d {h:.0f}h)"
                return f"{hours:.1f}h"

            if rph > 0:
                hrs = free / rph
                self._calc_hrs_var.set(_fmt_time(hrs))
                self._calc_days_var.set(f"{hrs/24:.2f} days")
            else:
                self._calc_hrs_var.set("∞")
                self._calc_days_var.set("∞")

            # Reverse: target fill
            tgt_pct  = max(1.0, min(100.0, float(self._calc_target_pct.get()))) / 100.0
            tgt_days = max(0.001, float(self._calc_target_days.get()))
            budget   = int(total * tgt_pct)
            budget_rph = budget / (tgt_days * 24.0)
            self._calc_budget_var.set(f"{budget} rec → {budget_rph:.1f}/hr")

            if budget_rph > 0:
                # min_iv ≥ 3600 × eff / budget_rph
                sugg_s = (3600.0 * eff) / budget_rph
                if sugg_s >= 3600:
                    sugg_str = f"{sugg_s:.0f}s (~{sugg_s/3600:.1f}h)"
                elif sugg_s >= 60:
                    sugg_str = f"{sugg_s:.0f}s (~{sugg_s/60:.0f}min)"
                else:
                    sugg_str = f"{sugg_s:.0f}s"
                self._calc_sugg_iv_var.set(sugg_str)
            else:
                self._calc_sugg_iv_var.set("∞")

        except (ValueError, ZeroDivisionError):
            for sv in [self._calc_rph_var, self._calc_hrs_var,
                       self._calc_days_var, self._calc_budget_var, self._calc_sugg_iv_var]:
                sv.set("ERR")

    # ── Flash Log helpers ─────────────────────────────────────────────────────
    def _log_set_mask(self):
        mask = 0
        if self._log_mask_temp.get():  mask |= 0x01
        if self._log_mask_light.get(): mask |= 0x02
        if self._log_mask_batp.get():  mask |= 0x04
        if self._log_mask_batmv.get(): mask |= 0x08
        self._cmd(f"log mask {mask:02X}")

    def _log_clear_confirm(self):
        if messagebox.askyesno("Clear Log",
                               "Erase all log data from flash?\nThis cannot be undone."):
            self._cmd("log clear yes")

    def _log_dump_all(self):
        self._log_in_dump = True
        self._log_csv_buf = []
        self._cmd("log dump")

    def _log_dump_range(self):
        fr = self._log_from_var.get()
        ct = self._log_count_var.get()
        self._log_in_dump = True
        self._log_csv_buf = []
        self._cmd(f"log read {fr} {ct}")

    def _log_clear_tree(self):
        if self._log_dump_tree:
            for item in self._log_dump_tree.get_children():
                self._log_dump_tree.delete(item)
        self._log_csv_buf = []

    @staticmethod
    def _epoch2000_to_str(ts_s: int) -> str:
        try:
            import datetime
            epoch_unix = ts_s + 946684800  # 2000-01-01 00:00:00 UTC as Unix
            return datetime.datetime.utcfromtimestamp(epoch_unix).strftime("%Y-%m-%d %H:%M:%S")
        except Exception:
            return str(ts_s)

    def _ts_display(self, raw_ts: str) -> str:
        if self._log_ts_as_date.get():
            try:
                return self._epoch2000_to_str(int(raw_ts.strip()))
            except ValueError:
                pass
        return raw_ts.strip()

    def _log_refresh_ts_column(self):
        if not self._log_dump_tree or not self._log_csv_buf:
            return
        for item, parts in zip(self._log_dump_tree.get_children(), self._log_csv_buf):
            if len(parts) < 2:
                continue
            new_ts = self._ts_display(parts[1])
            vals = list(self._log_dump_tree.item(item, "values"))
            if vals:
                vals[1] = new_ts
                self._log_dump_tree.item(item, values=vals)

    def _log_append_csv_row(self, s: str):
        parts = s.split(",")
        if len(parts) < 7:
            return
        self._log_csv_buf.append(parts)
        if self._log_dump_tree:
            vals = (parts[0].strip(),
                    self._ts_display(parts[1]),
                    parts[2].strip(), parts[3].strip(),
                    parts[4].strip(), parts[5].strip(), parts[6].strip())
            self._log_dump_tree.insert("", "end", values=vals)
            self._log_dump_tree.yview_moveto(1.0)

    def _log_save_csv(self):
        if not self._log_csv_buf:
            messagebox.showinfo("Save CSV", "No data. Run a dump first.")
            return
        fname = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
            title="Save flash log as CSV"
        )
        if not fname:
            return
        try:
            with open(fname, "w", newline="") as f:
                f.write("idx,ts_s,flags,temp_c,light_raw,bat_pct,bat_mv\n")
                for row in self._log_csv_buf:
                    f.write(",".join(p.strip() for p in row) + "\n")
            messagebox.showinfo("Save CSV",
                                f"Saved {len(self._log_csv_buf)} records to:\n{fname}")
        except Exception as e:
            messagebox.showerror("Save CSV", str(e))

    def _log_plot(self, which: str):
        if not _HAS_MPL:
            messagebox.showwarning("Matplotlib",
                                   "Install matplotlib:\n  pip install matplotlib")
            return
        if not self._log_csv_buf:
            messagebox.showinfo("Plot", "No data. Run a dump first.")
            return

        use_rtc = (self._log_ts_var.get() == "rtc")
        ts_l, temp_l, light_l, bat_pct_l, bat_mv_l = [], [], [], [], []
        for row in self._log_csv_buf:
            if len(row) < 7:
                continue
            try:
                raw_ts = int(row[1].strip())
                if use_rtc:
                    import datetime
                    ts_l.append(datetime.datetime.utcfromtimestamp(raw_ts + 946684800))
                else:
                    ts_l.append(float(raw_ts))
                temp_l.append(float(row[3].strip()) if row[3].strip() else None)
                light_l.append(float(row[4].strip()) if row[4].strip() else None)
                bat_pct_l.append(float(row[5].strip()) if row[5].strip() else None)
                bat_mv_l.append(float(row[6].strip()) if row[6].strip() else None)
            except ValueError:
                continue

        if not ts_l:
            messagebox.showinfo("Plot", "No parseable data.")
            return

        win = tk.Toplevel(self)
        win.title(f"Flash Log — {which.capitalize()} graph")
        win.configure(bg=BG)

        if which == "all":
            fig = Figure(figsize=(9, 7), facecolor="#f0f2f5")
            ax1 = fig.add_subplot(3, 1, 1)
            ax2 = fig.add_subplot(3, 1, 2)
            ax3 = fig.add_subplot(3, 1, 3)
            _mpl_plot_series(ax1, ts_l, temp_l,    "Temp (°C)",    "#dc2626")
            _mpl_plot_series(ax2, ts_l, light_l,   "Light (raw)",  "#d97706")
            _mpl_plot_series(ax3, ts_l, bat_mv_l,  "Battery (mV)", "#1d4ed8")
            fig.tight_layout(pad=1.5)
        elif which == "temp":
            fig = Figure(figsize=(9, 4), facecolor="#f0f2f5")
            ax1 = fig.add_subplot(1, 1, 1)
            _mpl_plot_series(ax1, ts_l, temp_l, "Temp (°C)", "#dc2626")
            ax1.set_title("Temperature")
            fig.tight_layout()
        elif which == "bat":
            fig = Figure(figsize=(9, 4), facecolor="#f0f2f5")
            ax1 = fig.add_subplot(1, 1, 1)
            ax2 = ax1.twinx()
            _mpl_plot_series(ax1, ts_l, bat_mv_l,  "Battery (mV)", "#1d4ed8")
            _mpl_plot_series(ax2, ts_l, bat_pct_l, "Battery (%)",  "#0891b2")
            ax2.set_ylabel("Battery (%)", color="#0891b2", fontsize=8)
            ax1.set_title("Battery")
            fig.tight_layout()
        else:  # light
            fig = Figure(figsize=(9, 4), facecolor="#f0f2f5")
            ax1 = fig.add_subplot(1, 1, 1)
            _mpl_plot_series(ax1, ts_l, light_l, "Light (raw)", "#d97706")
            ax1.set_title("Light")
            fig.tight_layout()

        canvas = FigureCanvasTkAgg(fig, master=win)
        canvas.draw()
        toolbar = NavigationToolbar2Tk(canvas, win)
        toolbar.update()
        canvas.get_tk_widget().pack(fill="both", expand=True)

    def _parse_log_line(self, s: str):
        m = re.search(r"\[LOG\]\s+Used:\s+(\d+)\s+entries.*?(\d+)%", s)
        if m:
            used, pct = int(m.group(1)), int(m.group(2))
            self._log_sv_used.set(f"{used}/768 ({pct}%)")
            self._log_prog_var.set(float(pct))
            return
        m = re.search(r"\[LOG\]\s+Free:\s+(\d+)\s+entries", s)
        if m:
            self._log_sv_free.set(m.group(1)); return
        m = re.search(r"\[LOG\]\s+hdr=pg\d+.*?(\d+)/(\d+)\s+entries", s)
        if m:
            used, total = int(m.group(1)), int(m.group(2))
            pct = int(used * 100 / total) if total else 0
            self._log_sv_used.set(f"{used}/{total}")
            self._log_sv_free.set(str(total - used))
            self._log_prog_var.set(float(pct)); return
        m = re.search(r"\[LOG\]\s+Init\s+OK:\s+(\d+)\s+records", s)
        if m:
            self._log_sv_used.set(m.group(1)); return
        m = re.search(r"\[LOG\]\s+free=(\d+)", s)
        if m:
            self._log_sv_free.set(m.group(1)); return
        m = re.search(r"\[LOG\]\s+Write:\s+(\S+)", s)
        if m:
            v = m.group(1).strip()
            self._log_sv_mode.set(v)
            self._log_mode_var.set({"ALWAYS": "always", "ON_CHANGE": "onchange",
                                    "ADAPTIVE": "adaptive"}.get(v, "always")); return
        m = re.search(r"\[LOG\]\s+Oflow:\s+(\S+)", s)
        if m:
            v = m.group(1).strip()
            self._log_sv_oflow.set(v)
            self._log_oflow_var.set("circular" if v == "CIRCULAR" else "stop"); return
        m = re.search(r"\[LOG\]\s+mask=0x([0-9a-fA-F]+)\s+oflow=(\d+)\s+mode=(\d+)\s+ckp=(\d+)(?:\s+ts=(\d+))?", s)
        if m:
            mv = int(m.group(1), 16)
            self._log_mask_temp.set(bool(mv & 0x01))
            self._log_mask_light.set(bool(mv & 0x02))
            self._log_mask_batp.set(bool(mv & 0x04))
            self._log_mask_batmv.set(bool(mv & 0x08))
            ov, mo, ck = int(m.group(2)), int(m.group(3)), int(m.group(4))
            self._log_sv_oflow.set("CIRCULAR" if ov else "STOP")
            self._log_oflow_var.set("circular" if ov else "stop")
            _modes = ["ALWAYS", "ON_CHANGE", "ADAPTIVE"]
            _modes_k = ["always", "onchange", "adaptive"]
            self._log_sv_mode.set(_modes[mo] if mo < 3 else "?")
            self._log_mode_var.set(_modes_k[mo] if mo < 3 else "always")
            self._log_ckp_var.set(str(ck))
            if m.group(5) is not None:
                self._log_ts_var.set("rtc" if int(m.group(5)) else "boot")
            if self._verify_active:
                self._verify_received.update({
                    'mask': mv, 'oflow': ov, 'mode': mo, 'ckp': ck,
                    'ts': int(m.group(5)) if m.group(5) is not None else 0,
                })
            return
        m = re.search(r"\[LOG\]\s+TS:\s+(\S+)", s)
        if m:
            self._log_ts_var.set("rtc" if "rtc" in m.group(1).lower() else "boot"); return
        m = re.search(r"\[LOG\]\s+ts=(rtc|boot)", s)
        if m:
            self._log_ts_var.set(m.group(1)); return
        m = re.search(r"\[LOG\]\s+temp=(\d+)s\s+light=(\d+)s\s+bat=(\d+)s", s)
        if m:
            self._log_dt_var.set(m.group(1))
            self._log_dl_var.set(m.group(2))
            self._log_db_var.set(m.group(3))
            if self._verify_active:
                vr = self._verify_received
                vr['temp']  = int(m.group(1))
                vr['light'] = int(m.group(2))
                vr['bat']   = int(m.group(3))
                self._verify_active = False
                sn = self._verify_snapshot
                results = {k: (vr.get(k) == sn.get(k)) for k in sn}
                self.after(50, lambda r=dict(results): self._log_flash_result(r))
            return
        if "cleared OK" in s:
            self._log_sv_used.set("0")
            self._log_sv_free.set("768")
            self._log_prog_var.set(0.0)
            self._log_clear_tree()

    # ── Flash log: traces, row states, verify ────────────────────────────────
    def _setup_traces(self):
        _mode_vars = [self._temp_mode_var, self._batt_mode_var, self._light_mode_var]
        _mask_vars = [self._log_mask_temp, self._log_mask_light, self._log_mask_batp]
        for v in _mode_vars + _mask_vars:
            v.trace_add('write', self._update_log_row_states)
            v.trace_add('write', self._update_sensor_flash_indicators)
        for v in [self._log_dt_var, self._log_dl_var, self._log_db_var]:
            v.trace_add('write', self._update_sensor_flash_indicators)
        for key, rv, wv in [
            ('temp',  self._temp_period_var,  self._log_dt_var),
            ('light', self._light_period_var, self._log_dl_var),
            ('bat',   self._batt_period_var,  self._log_db_var),
        ]:
            sv = self._log_sync_vars[key]
            def _mk(rv=rv, wv=wv, sv=sv):
                def _cb(*_):
                    if sv.get(): wv.set(rv.get())
                rv.trace_add('write', _cb)
                sv.trace_add('write', _cb)
            _mk()
        self._update_log_row_states()
        self._update_sensor_flash_indicators()

    def _update_log_row_states(self, *_):
        for key, mode_var, mask_var in [
            ('temp',  self._temp_mode_var,  self._log_mask_temp),
            ('light', self._light_mode_var, self._log_mask_light),
            ('bat',   self._batt_mode_var,  self._log_mask_batp),
        ]:
            row = self._log_row_widgets.get(key)
            if not row: continue
            mode   = mode_var.get()
            active = (mode != "off")
            logged = active and mask_var.get()
            for w in row.get('read_ws', []):
                try: w.config(state="normal" if active else "disabled")
                except: pass
            for w in row.get('write_ws', []):
                try: w.config(state="normal" if logged else "disabled")
                except: pass
            lbl = row.get('row_lbl')
            if lbl:
                try: lbl.config(fg=FG if active else FG_DIM)
                except: pass
            arr = row.get('arrow_w')
            if arr:
                try: arr.config(fg="#a78bfa" if logged else FG_DIM)
                except: pass

    def _update_sensor_flash_indicators(self, *_):
        for key, mode_var, mask_var, iv_var in [
            ('temp',  self._temp_mode_var,  self._log_mask_temp,  self._log_dt_var),
            ('light', self._light_mode_var, self._log_mask_light, self._log_dl_var),
            ('bat',   self._batt_mode_var,  self._log_mask_batp,  self._log_db_var),
        ]:
            sv = self._flash_ind_vars.get(key)
            if not sv: continue
            mode = mode_var.get()
            if mode == 'off':
                sv.set("→ Flash: sensor OFF")
            elif not mask_var.get():
                sv.set("→ Flash: mask disabled")
            else:
                sv.set(f"→ Flash: every {iv_var.get()}s")

    def _log_write_verify(self):
        if not (self.serial and self.serial.is_open):
            messagebox.showwarning("Not connected", "Connect to a COM port first")
            return
        mask = ((0x01 if self._log_mask_temp.get()  else 0) |
                (0x02 if self._log_mask_light.get() else 0) |
                (0x04 if self._log_mask_batp.get()  else 0) |
                (0x08 if self._log_mask_batmv.get() else 0))
        mode_n  = {"always": 0, "onchange": 1, "adaptive": 2}.get(self._log_mode_var.get(), 0)
        oflow_n = 1 if self._log_oflow_var.get() == "circular" else 0
        ts_n    = 1 if self._log_ts_var.get() == "rtc" else 0
        self._verify_snapshot = {
            'mask': mask, 'mode': mode_n, 'oflow': oflow_n,
            'ckp': int(self._log_ckp_var.get()), 'ts': ts_n,
            'temp': int(self._log_dt_var.get()),
            'light': int(self._log_dl_var.get()),
            'bat': int(self._log_db_var.get()),
        }
        self._verify_received = {}
        self._cmd(f"log mask {mask:02X}")
        self._cmd(f"log mode {mode_n}")
        self._cmd(f"log overflow {oflow_n}")
        self._cmd(f"log ckp {self._log_ckp_var.get()}")
        self._cmd(f"log ts {self._log_ts_var.get()}")
        self._cmd(f"log temp {self._log_dt_var.get()}")
        self._cmd(f"log light {self._log_dl_var.get()}")
        self._cmd(f"log bat {self._log_db_var.get()}")
        self._verify_active = True
        self.after(900, lambda: self._cmd("log get"))

    def _log_flash_result(self, results):
        GREEN_FG, RED_FG = "#22c55e", "#ef4444"
        GREEN_BG, RED_BG = "#14532d", "#7f1d1d"
        MS = 2500
        mask_ok = results.get('mask', True)
        for cb in self._verify_mask_cbs:
            fg = GREEN_FG if mask_ok else RED_FG
            try:
                cb.config(fg=fg)
                self.after(MS, lambda w=cb: w.config(fg=FG))
            except: pass
        for key, res_key in [('temp', 'temp'), ('light', 'light'), ('bat', 'bat')]:
            ok  = results.get(res_key, True)
            row = self._log_row_widgets.get(key, {})
            w   = row.get('write_sp')
            if w:
                try:
                    w.config(bg=GREEN_BG if ok else RED_BG)
                    self.after(MS, lambda ww=w: ww.config(bg=BG2))
                except: pass
        all_ok = all(results.values())
        if all_ok:
            messagebox.showinfo("Write & Verify", "All settings written and confirmed ✓")
        else:
            failed = [k for k, v in results.items() if not v]
            messagebox.showwarning("Write & Verify",
                                   f"Written, but MCU reported different values for:\n"
                                   f"{', '.join(failed)}\n\nCheck terminal for details.")

    # ── Status panel ──────────────────────────────────────────────────────────
    def _build_status_panel(self, parent):
        sf = ttk.LabelFrame(parent, text="  Live Status  ", padding=(10, 6))
        sf.grid(row=0, column=0, sticky="ew", padx=6, pady=(6, 4))
        sf.columnconfigure(1, weight=1)

        for r, (label, key) in enumerate([
            ("Mode",        "mode"),
            ("Channel",     "ch"),
            ("Power",       "pwr"),
            ("Pulse (ms)",  "pulse_ms"),
            ("Period (ms)", "period_ms"),
            ("LED mode",    "led_mode"),
            ("Chip temp",   "temp_c"),
            ("VDDA",        "vdda_mv"),
            ("Battery mV",  "batt_mv"),
            ("Battery %",   "batt_pct"),
            ("Light raw",   "light_raw"),
            ("Light lux~",  "light_lux"),
            ("Schedule",    "sched_status"),
        ]):
            tk.Label(sf, text=label, bg=BG, fg=FG_DIM, anchor="w", width=14
                     ).grid(row=r, column=0, sticky="w", padx=2, pady=2)
            tk.Label(sf, textvariable=self.sv[key],
                     bg=BG2, fg=BLUE, font=("Consolas", 10, "bold"),
                     anchor="w", padx=6, relief="groove", borderwidth=1
                     ).grid(row=r, column=1, sticky="ew", padx=2, pady=2)

        ttk.Button(sf, text="↺ Refresh status",
                   command=lambda: self._cmd("status")
                   ).grid(row=13, column=0, columnspan=2, sticky="ew", pady=(6, 2))

    # ── RTC panel ─────────────────────────────────────────────────────────────
    def _build_rtc_panel(self, parent):
        rf = ttk.LabelFrame(parent, text="  RTC Clock  ", padding=(10, 6))
        rf.grid(row=1, column=0, sticky="ew", padx=6, pady=(0, 6))
        rf.columnconfigure(1, weight=1)

        tk.Label(rf, text="Date:", bg=BG, fg=FG_DIM, anchor="w", width=6
                 ).grid(row=0, column=0, sticky="w", padx=2, pady=2)
        tk.Label(rf, textvariable=self.sv["rtc_date"],
                 bg=BG2, fg=GREEN, font=("Consolas", 9),
                 anchor="w", padx=6, relief="groove", borderwidth=1
                 ).grid(row=0, column=1, columnspan=2, sticky="ew", padx=2, pady=2)

        tk.Label(rf, text="Time:", bg=BG, fg=FG_DIM, anchor="w", width=6
                 ).grid(row=1, column=0, sticky="w", padx=2, pady=2)
        tk.Label(rf, textvariable=self.sv["rtc_time"],
                 bg=BG2, fg=GREEN, font=("Consolas", 14, "bold"),
                 anchor="w", padx=6, relief="groove", borderwidth=1
                 ).grid(row=1, column=1, columnspan=2, sticky="ew", padx=2, pady=2)

        ttk.Button(rf, text="↺ Get RTC",
                   command=lambda: self._cmd("rtc get")
                   ).grid(row=2, column=0, padx=(2, 3), pady=4, sticky="ew")
        ttk.Button(rf, text="⟳ Sync PC Time", style="Accent.TButton",
                   command=self._sync_rtc
                   ).grid(row=2, column=1, columnspan=2, padx=(3, 2), pady=4, sticky="ew")

        self._rtc_live_var = tk.BooleanVar(value=False)
        tk.Checkbutton(rf, text="Live RTC  (send every 1s while UART connected)",
                       variable=self._rtc_live_var,
                       command=self._on_rtc_live_change,
                       bg=BG, fg=FG, selectcolor=BG2, activebackground=BG,
                       font=("Segoe UI", 8)
                       ).grid(row=3, column=0, columnspan=3, sticky="w", padx=2, pady=(0, 2))

    # ── Schedule panel ────────────────────────────────────────────────────────
    def _build_sched_panel(self, parent):
        sf = ttk.LabelFrame(parent, text="  Schedule  ", padding=(8, 5))
        sf.grid(row=0, column=0, sticky="ew", padx=6, pady=6)
        sf.columnconfigure(0, weight=1)

        tk.Checkbutton(sf, text="Enable schedule  (unchecked = TX always active)",
                       variable=self._sched_enabled,
                       command=self._toggle_sched_enabled,
                       bg=BG, fg=FG, selectcolor=BG2, activebackground=BG,
                       font=("Segoe UI", 9, "bold")
                       ).grid(row=0, column=0, sticky="w", pady=(0, 3))

        # Hours: 3 rows × 8 columns
        lh = tk.Label(sf, text="Hours:", bg=BG, fg=FG_DIM, font=("Segoe UI", 8))
        lh.grid(row=1, column=0, sticky="w")
        self._sched_widgets.append(lh)
        hf = tk.Frame(sf, bg=BG); hf.grid(row=2, column=0, sticky="ew")
        self._sched_widgets.append(hf)
        for h in range(24):
            r, c = divmod(h, 8)
            cb = tk.Checkbutton(hf, text=f"{h:02d}", variable=self._hour_vars[h],
                                bg=BG, fg=FG, selectcolor=BG2, activebackground=BG,
                                disabledforeground=BG3, font=("Consolas", 8),
                                padx=0, pady=0, state="disabled",
                                command=self._on_hour_change)
            cb.grid(row=r, column=c, sticky="w", padx=1, pady=0)
            self._sched_widgets.append(cb)

        # Days
        ld = tk.Label(sf, text="Days:", bg=BG, fg=FG_DIM, font=("Segoe UI", 8))
        ld.grid(row=3, column=0, sticky="w", pady=(3, 0))
        self._sched_widgets.append(ld)
        df = tk.Frame(sf, bg=BG); df.grid(row=4, column=0, sticky="ew")
        self._sched_widgets.append(df)
        for d, name in enumerate(_DAY_NAMES):
            cb = tk.Checkbutton(df, text=name, variable=self._day_vars[d],
                                bg=BG, fg=FG, selectcolor=BG2, activebackground=BG,
                                disabledforeground=BG3, font=("Consolas", 8),
                                padx=2, state="disabled", command=self._on_day_change)
            cb.grid(row=0, column=d, sticky="w")
            self._sched_widgets.append(cb)

        # Months: 2 rows × 6
        lm = tk.Label(sf, text="Months:", bg=BG, fg=FG_DIM, font=("Segoe UI", 8))
        lm.grid(row=5, column=0, sticky="w", pady=(3, 0))
        self._sched_widgets.append(lm)
        mf = tk.Frame(sf, bg=BG); mf.grid(row=6, column=0, sticky="ew")
        self._sched_widgets.append(mf)
        for m, name in enumerate(_MONTH_NAMES):
            r, c = divmod(m, 6)
            cb = tk.Checkbutton(mf, text=name, variable=self._month_vars[m],
                                bg=BG, fg=FG, selectcolor=BG2, activebackground=BG,
                                disabledforeground=BG3, font=("Consolas", 8),
                                padx=0, state="disabled", command=self._on_month_change)
            cb.grid(row=r, column=c, sticky="w", padx=1, pady=0)
            self._sched_widgets.append(cb)

        # Compact MCU status block
        ttk.Separator(sf, orient="horizontal").grid(row=7, column=0, sticky="ew", pady=4)
        inf = tk.Frame(sf, bg=BG2, relief="groove", borderwidth=1)
        inf.grid(row=8, column=0, sticky="ew", pady=(0, 4))
        inf.columnconfigure(1, weight=2); inf.columnconfigure(3, weight=1)

        tk.Label(inf, text="H:", bg=BG2, fg=FG_DIM, font=("Segoe UI", 8), width=2, anchor="w"
                 ).grid(row=0, column=0, sticky="w", padx=(6, 1), pady=2)
        tk.Label(inf, textvariable=self._sv_hours_disp,
                 bg=BG2, fg=BLUE, font=("Consolas", 8), anchor="w"
                 ).grid(row=0, column=1, sticky="ew", padx=(1, 6), pady=2)
        tk.Label(inf, text="Active:", bg=BG2, fg=FG_DIM, font=("Segoe UI", 8), anchor="e"
                 ).grid(row=0, column=2, sticky="e", padx=(4, 2), pady=2)
        self._active_lbl = tk.Label(inf, textvariable=self._sv_active_now,
                                    bg=BG2, fg=BLUE, font=("Consolas", 8, "bold"),
                                    anchor="w", width=5)
        self._active_lbl.grid(row=0, column=3, sticky="w", padx=(1, 6), pady=2)

        tk.Label(inf, text="D:", bg=BG2, fg=FG_DIM, font=("Segoe UI", 8), width=2, anchor="w"
                 ).grid(row=1, column=0, sticky="w", padx=(6, 1), pady=2)
        tk.Label(inf, textvariable=self._sv_days_disp,
                 bg=BG2, fg=BLUE, font=("Consolas", 8), anchor="w"
                 ).grid(row=1, column=1, sticky="ew", padx=(1, 6), pady=2)
        tk.Label(inf, text="M:", bg=BG2, fg=FG_DIM, font=("Segoe UI", 8), anchor="e"
                 ).grid(row=1, column=2, sticky="e", padx=(4, 2), pady=2)
        tk.Label(inf, textvariable=self._sv_months_disp,
                 bg=BG2, fg=BLUE, font=("Consolas", 8), anchor="w"
                 ).grid(row=1, column=3, sticky="w", padx=(1, 6), pady=2)

        ttk.Button(sf, text="↺ Read from MCU",
                   command=lambda: self._cmd("sched show")
                   ).grid(row=9, column=0, sticky="w", pady=(2, 0))

    # ── Log ───────────────────────────────────────────────────────────────────
    def _build_log(self, row):
        lf = ttk.LabelFrame(self, text="  UART Log  ", padding=(4, 4))
        lf.grid(row=row, column=0, columnspan=2, sticky="nsew", padx=6, pady=(0, 6))
        lf.columnconfigure(0, weight=1); lf.rowconfigure(0, weight=1)
        self.rowconfigure(row, weight=1)

        self.log = scrolledtext.ScrolledText(
            lf, height=8, state="disabled",
            font=("Consolas", 9), bg="#1e2330", fg="#e2e8f0",
            insertbackground=FG, relief="sunken", borderwidth=1)
        self.log.grid(row=0, column=0, sticky="nsew")

        self.log.tag_configure("tx",     foreground="#60a5fa")
        self.log.tag_configure("tx_on",  foreground="#4ade80")
        self.log.tag_configure("tx_off", foreground="#94a3b8")
        self.log.tag_configure("gekon",  foreground="#fbbf24")
        self.log.tag_configure("shut",   foreground="#f87171")
        self.log.tag_configure("sys",    foreground="#64748b")
        self.log.tag_configure("diag",   foreground="#c084fc")
        self.log.tag_configure("rtc",    foreground="#34d399")
        self.log.tag_configure("sched",  foreground="#fb923c")
        self.log.tag_configure("log",    foreground="#a78bfa")

        bar = tk.Frame(lf, bg=BG); bar.grid(row=1, column=0, sticky="ew", pady=(4, 0))
        tk.Label(bar, text="CMD:", bg=BG, fg=FG_DIM).pack(side="left")
        self.cmd_var = tk.StringVar()
        e = tk.Entry(bar, textvariable=self.cmd_var, width=44,
                     bg=BG2, fg=FG, insertbackground=FG,
                     font=("Consolas", 10), relief="groove", borderwidth=1)
        e.pack(side="left", padx=4)
        e.bind("<Return>", lambda _: self._send_manual())
        ttk.Button(bar, text="Send",  command=self._send_manual).pack(side="left")
        ttk.Button(bar, text="Clear", command=self._clear_log).pack(side="right", padx=4)

    # ── Serial ────────────────────────────────────────────────────────────────
    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        last  = self._settings.get("last_port", "")
        menu  = self.port_cb["menu"]
        menu.delete(0, "end")
        for p in ports:
            menu.add_command(label=p, command=lambda v=p: self.port_var.set(v))
        if last in ports:
            self.port_var.set(last)
        elif ports:
            self.port_var.set(ports[0])

    def _toggle_connect(self):
        if self.serial and self.serial.is_open: self._disconnect()
        else: self._connect()

    def _connect(self):
        port = self.port_var.get()
        if not port:
            messagebox.showerror("Error", "Select a COM port"); return
        try:
            self.serial = serial.Serial(port, 115200, timeout=0.05)
            self._stop_rx.clear()
            self.rx_thread = threading.Thread(target=self._rx_worker, daemon=True)
            self.rx_thread.start()
            self.btn_conn.configure(text="Disconnect", style="Red.TButton")
            self.conn_lbl.configure(text=f"● {port}", fg=GREEN)
            self._log(f"[SYS] connected to {port}\n", "sys")
            self._settings["last_port"] = port
            self._save_settings()
            self.after(400, lambda: self._cmd("status"))
            self.after(800, lambda: self._cmd("log get"))
        except Exception as e:
            messagebox.showerror("Connect error", str(e))

    def _disconnect(self):
        self._stop_rx.set()
        if self.serial:
            try: self.serial.close()
            except: pass
            self.serial = None
        self.btn_conn.configure(text="Connect", style="Accent.TButton")
        self.conn_lbl.configure(text="● Disconnected", fg=RED)
        self._log("[SYS] disconnected\n", "sys")

    def _rx_worker(self):
        buf = b""
        while not self._stop_rx.is_set():
            try:
                data = self.serial.read(256)
            except Exception:
                break
            if data:
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.rstrip(b"\r").decode("latin-1", errors="replace")
                    if text:
                        self.rx_queue.put(text)

    def _cmd(self, text: str):
        if not (self.serial and self.serial.is_open):
            messagebox.showwarning("Not connected", "Connect to a COM port first"); return
        self._log(f">>> {text}\n", "tx")
        self.serial.write((text + "\r\n").encode())

    # ── Help window ───────────────────────────────────────────────────────────
    def _show_help(self):
        win = tk.Toplevel(self)
        win.title("TX Beacon — Help & Manual")
        win.configure(bg=BG)
        win.geometry("780x640")
        win.resizable(True, True)

        txt = scrolledtext.ScrolledText(win, font=("Consolas", 10),
                                         bg="#1e2330", fg="#e2e8f0",
                                         wrap="none", relief="flat", padx=14, pady=10)
        txt.pack(fill="both", expand=True, padx=6, pady=(6, 0))
        txt.insert("1.0", HELP_TEXT)
        txt.configure(state="disabled")

        ttk.Button(win, text="Close", command=win.destroy
                   ).pack(pady=6)

    # ── Actions ───────────────────────────────────────────────────────────────
    def _do_sleep(self):
        if messagebox.askyesno("Shutdown",
                               "Enter Shutdown mode?\n"
                               "MCU stops until GEKON button or power cycle."):
            self._cmd("sleep")

    def _do_reset(self):
        if messagebox.askyesno("Reset MCU", "Soft-reset the MCU?\n"
                               "MCU reboots and loads saved flash config."):
            self._cmd("reset")

    def _do_save(self):
        if messagebox.askyesno("Save & Restart",
                               "Apply all settings to MCU, save to flash\n"
                               "and restart MCU?\n\n"
                               "MCU will reboot and run the saved config."):
            self._apply_schedule()
            self._cmd(f"mode {self._mode_var.get()}")
            self._cmd(f"ch {self._ch_var.get()}")
            self._cmd(f"pwr {self._pwr_var.get()}")
            self._cmd(f"pulse {self._pulse_var.get()}")
            self._cmd(f"period {self._period_var.get()}")
            self._cmd(f"led {self._led_mode_var.get()}")
            self._cmd(f"temp mode {self._temp_mode_var.get()}")
            self._cmd(f"temp period {self._temp_period_var.get()}")
            self._cmd(f"temp offset {float(self._temp_offset_var.get()):.1f}")
            self._cmd(f"batt mode {self._batt_mode_var.get()}")
            self._cmd(f"batt period {self._batt_period_var.get()}")
            self._cmd(f"batt scale {float(self._batt_scale_var.get()):.1f}")
            self._cmd(f"light mode {self._light_mode_var.get()}")
            self._cmd(f"light period {self._light_period_var.get()}")
            self._cmd(f"rtc live {'on' if self._rtc_live_var.get() else 'off'}")
            self._cmd("save")
            self._cmd("reset")

    def _send_manual(self):
        text = self.cmd_var.get().strip()
        if text:
            self._cmd(text); self.cmd_var.set("")

    def _sync_rtc(self):
        now = datetime.datetime.now()
        self._cmd(now.strftime("rtc set %Y-%m-%d %H:%M:%S"))

    # ── Profile management ────────────────────────────────────────────────────
    def _load_profiles_file(self):
        try:
            if os.path.exists(PROFILES_FILE):
                with open(PROFILES_FILE, "r", encoding="utf-8") as f:
                    return json.load(f)
        except Exception: pass
        return {}

    def _save_profiles_file(self):
        try:
            with open(PROFILES_FILE, "w", encoding="utf-8") as f:
                json.dump(self._custom_profiles, f, indent=2, ensure_ascii=False)
        except Exception as e:
            messagebox.showerror("Profile", f"Cannot save profiles:\n{e}")

    def _load_settings(self):
        try:
            if os.path.exists(SETTINGS_FILE):
                with open(SETTINGS_FILE, "r", encoding="utf-8") as f:
                    return json.load(f)
        except Exception: pass
        return {}

    def _save_settings(self):
        try:
            with open(SETTINGS_FILE, "w", encoding="utf-8") as f:
                json.dump(self._settings, f, indent=2, ensure_ascii=False)
        except Exception: pass

    def _get_all_profiles(self):
        p = dict(PRESET_PROFILES); p.update(self._custom_profiles); return p

    def _refresh_profile_menu(self):
        names = list(self._get_all_profiles().keys())
        menu  = self._profile_om["menu"]
        menu.delete(0, "end")
        for name in names:
            menu.add_command(label=name, command=lambda v=name: self._profile_var.set(v))
        if self._profile_var.get() not in names:
            self._profile_var.set(names[0])

    def _load_selected_profile(self):
        name     = self._profile_var.get()
        profiles = self._get_all_profiles()
        if name not in profiles:
            messagebox.showerror("Profile", f"Profile '{name}' not found"); return
        self._apply_profile_to_gui(profiles[name])
        self._log(f"[PROFILE] loaded '{name}'\n", "sys")

    def _save_as_profile(self):
        name = simpledialog.askstring("Save Profile", "Profile name:", parent=self)
        if not name or not name.strip(): return
        name = name.strip()
        if name in PRESET_PROFILES:
            messagebox.showerror("Profile", f"Cannot overwrite preset '{name}'"); return
        self._custom_profiles[name] = self._capture_current_state()
        self._save_profiles_file()
        self._refresh_profile_menu()
        self._profile_var.set(name)
        self._log(f"[PROFILE] saved '{name}'\n", "sys")

    def _delete_profile(self):
        name = self._profile_var.get()
        if name in PRESET_PROFILES:
            messagebox.showerror("Profile", "Cannot delete a preset profile"); return
        if name not in self._custom_profiles:
            messagebox.showerror("Profile", f"Profile '{name}' not found"); return
        if messagebox.askyesno("Delete", f"Delete profile '{name}'?"):
            del self._custom_profiles[name]
            self._save_profiles_file()
            self._refresh_profile_menu()

    def _capture_current_state(self):
        return {
            "mode":          self._mode_var.get(),
            "ch":            self._ch_var.get(),
            "pwr":           self._pwr_var.get(),
            "pulse_ms":      int(self._pulse_var.get()  or 23),
            "period_ms":     int(self._period_var.get() or 2000),
            "led_mode":      self._led_mode_var.get(),
            "sched_enabled": self._sched_enabled.get(),
            "hours":   [h for h in range(24) if self._hour_vars[h].get()],
            "days":    [d for d in range(7)  if self._day_vars[d].get()],
            "months":  [m for m in range(12) if self._month_vars[m].get()],
        }

    def _apply_profile_to_gui(self, p):
        self._mode_var.set(p["mode"]);              self._cmd(f"mode {p['mode']}")
        self._ch_var.set(p["ch"]);                  self._cmd(f"ch {p['ch']}")
        self._pwr_var.set(p["pwr"]);                self._cmd(f"pwr {p['pwr']}")
        self._pulse_var.set(str(p["pulse_ms"]));    self._cmd(f"pulse {p['pulse_ms']}")
        self._period_var.set(str(p["period_ms"]));  self._cmd(f"period {p['period_ms']}")
        self._led_mode_var.set(p["led_mode"]);      self._cmd(f"led {p['led_mode']}")

        for v in self._hour_vars:  v.set(False)
        for v in self._day_vars:   v.set(False)
        for v in self._month_vars: v.set(False)
        for h in p.get("hours",  []):
            if 0 <= h <= 23: self._hour_vars[h].set(True)
        for d in p.get("days",   []):
            if 0 <= d <= 6:  self._day_vars[d].set(True)
        for m in p.get("months", []):
            if 0 <= m <= 11: self._month_vars[m].set(True)

        sched = p.get("sched_enabled", False)
        self._sched_enabled.set(sched)
        state = "normal" if sched else "disabled"
        for w in self._sched_widgets:
            try: w.configure(state=state)
            except Exception: pass
        if sched:
            self._on_hour_change(); self._on_day_change(); self._on_month_change()
        else:
            self._cmd("sched off")

    # ── Schedule helpers ──────────────────────────────────────────────────────
    def _toggle_sched_enabled(self):
        enabled = self._sched_enabled.get()
        state   = "normal" if enabled else "disabled"
        for w in self._sched_widgets:
            try: w.configure(state=state)
            except Exception: pass
        if enabled:
            self._on_hour_change(); self._on_day_change(); self._on_month_change()
        else:
            self._cmd("sched off")

    def _on_hour_change(self):
        hours = [h for h in range(24) if self._hour_vars[h].get()]
        if hours: self._cmd("sched hours " + " ".join(str(h) for h in hours))
        else:     self._cmd("sched hours")

    def _on_day_change(self):
        days = [d+1 for d in range(7) if self._day_vars[d].get()]
        if days: self._cmd("sched days " + " ".join(str(d) for d in days))
        else:    self._cmd("sched days")

    def _on_month_change(self):
        months = [m+1 for m in range(12) if self._month_vars[m].get()]
        if months: self._cmd("sched months " + " ".join(str(m) for m in months))
        else:      self._cmd("sched months")

    def _update_sched_from_parse(self):
        has = (any(v.get() for v in self._hour_vars) or
               any(v.get() for v in self._day_vars) or
               any(v.get() for v in self._month_vars))
        self._sched_enabled.set(has)
        state = "normal" if has else "disabled"
        for w in self._sched_widgets:
            try: w.configure(state=state)
            except Exception: pass

    def _apply_schedule(self):
        if not self._sched_enabled.get():
            self._cmd("sched off"); return
        self._on_hour_change(); self._on_day_change(); self._on_month_change()

    # ── HW Descriptor panel ───────────────────────────────────────────────────
    def _build_hwdesc_panel(self, parent):
        # StringVars for each field
        self._hw_ver     = tk.StringVar(value="1")
        self._hw_temp    = tk.StringVar(value="none")
        self._hw_light   = tk.StringVar(value="none")
        self._hw_light_m = tk.StringVar(value="")
        self._hw_batt    = tk.StringVar(value="none")
        self._hw_batt_full  = tk.StringVar(value="4200")
        self._hw_batt_empty = tk.StringVar(value="3000")
        self._hw_accel   = tk.StringVar(value="none")
        self._hw_accel_m = tk.StringVar(value="")
        self._hw_led     = tk.StringVar(value="none")
        self._hw_led_m   = tk.StringVar(value="")
        self._hw_freq    = tk.StringVar(value="30000000")
        self._hw_ch      = tk.StringVar(value="4")
        self._hw_pwr     = tk.StringVar(value="4")
        self._hw_txtype  = tk.StringVar(value="colpitts")
        self._hw_comment = tk.StringVar(value="")

        sp = dict(bg=BG2, fg=FG, insertbackground=FG, buttonbackground=BG3,
                  relief="groove", highlightthickness=1, highlightbackground=BG3,
                  font=("Consolas", 9))
        en = dict(bg=BG2, fg=FG, insertbackground=FG, relief="groove",
                  highlightthickness=1, highlightbackground=BG3, font=("Consolas", 9))
        dd_opts = dict(bg=BG2, fg=FG, activebackground=ACCENT, activeforeground="#fff",
                       relief="groove", font=("Segoe UI", 9), borderwidth=1,
                       highlightthickness=1, highlightbackground=BG3, anchor="w")

        sf = ttk.LabelFrame(parent, text="  Hardware Descriptor  ", padding=(10, 6))
        sf.grid(row=0, column=0, sticky="ew", padx=6, pady=6)
        sf.columnconfigure(1, weight=1)
        sf.columnconfigure(3, weight=1)

        def lbl(text, r, c, cs=1):
            tk.Label(sf, text=text, bg=BG, fg=FG_DIM,
                     font=("Segoe UI", 8), anchor="w"
                     ).grid(row=r, column=c, sticky="w", padx=(4, 2), pady=2)

        def dropdown(var, choices, r, c):
            om = tk.OptionMenu(sf, var, *choices)
            om.config(**dd_opts)
            om["menu"].config(bg=BG2, fg=FG, activebackground=ACCENT,
                              activeforeground="#fff", font=("Segoe UI", 9))
            om.grid(row=r, column=c, sticky="ew", padx=2, pady=2)

        # Row 0: HW version
        lbl("HW ver:", 0, 0)
        tk.Spinbox(sf, textvariable=self._hw_ver, from_=1, to=255, width=5, **sp
                   ).grid(row=0, column=1, sticky="w", padx=2, pady=2)

        # Row 1: Temperature
        lbl("Temp sensor:", 1, 0)
        dropdown(self._hw_temp, ["none","crystal","ntc","stts22h","lis2dw12"], 1, 1)

        # Row 2: Light sensor
        lbl("Light sensor:", 2, 0)
        dropdown(self._hw_light, ["none","present"], 2, 1)
        lbl("model:", 2, 2)
        tk.Entry(sf, textvariable=self._hw_light_m, width=14, **en
                 ).grid(row=2, column=3, sticky="ew", padx=2, pady=2)

        # Row 3: Battery
        lbl("Battery:", 3, 0)
        dropdown(self._hw_batt, ["none","adc","fuel"], 3, 1)
        lbl("full/empty mV:", 3, 2)
        bf = tk.Frame(sf, bg=BG); bf.grid(row=3, column=3, sticky="ew", padx=2)
        tk.Spinbox(bf, textvariable=self._hw_batt_full,  from_=1000, to=5000, width=6, **sp
                   ).pack(side="left", padx=(0, 2))
        tk.Spinbox(bf, textvariable=self._hw_batt_empty, from_=1000, to=5000, width=6, **sp
                   ).pack(side="left")

        # Row 4: Accelerometer
        lbl("Accel:", 4, 0)
        dropdown(self._hw_accel, ["none","ism330","lis2dw12","other"], 4, 1)
        lbl("model:", 4, 2)
        tk.Entry(sf, textvariable=self._hw_accel_m, width=14, **en
                 ).grid(row=4, column=3, sticky="ew", padx=2, pady=2)

        # Row 5: LED
        lbl("LED:", 5, 0)
        dropdown(self._hw_led, ["none","led","rgb"], 5, 1)
        lbl("model:", 5, 2)
        tk.Entry(sf, textvariable=self._hw_led_m, width=14, **en
                 ).grid(row=5, column=3, sticky="ew", padx=2, pady=2)

        ttk.Separator(sf, orient="horizontal").grid(row=6, column=0, columnspan=4,
                                                     sticky="ew", pady=4)

        # Row 7-9: TX
        lbl("TX freq (Hz):", 7, 0)
        tk.Spinbox(sf, textvariable=self._hw_freq, from_=1000000, to=500000000,
                   increment=1000000, width=12, **sp
                   ).grid(row=7, column=1, sticky="w", padx=2, pady=2)
        lbl("Channels:", 7, 2)
        tk.Spinbox(sf, textvariable=self._hw_ch, from_=1, to=255, width=5, **sp
                   ).grid(row=7, column=3, sticky="w", padx=2, pady=2)

        lbl("Pwr levels:", 8, 0)
        tk.Spinbox(sf, textvariable=self._hw_pwr, from_=1, to=255, width=5, **sp
                   ).grid(row=8, column=1, sticky="w", padx=2, pady=2)
        lbl("TX type:", 8, 2)
        tk.Entry(sf, textvariable=self._hw_txtype, width=14, **en
                 ).grid(row=8, column=3, sticky="ew", padx=2, pady=2)

        ttk.Separator(sf, orient="horizontal").grid(row=9, column=0, columnspan=4,
                                                     sticky="ew", pady=4)

        # Row 10: Comment
        lbl("Comment:", 10, 0)
        tk.Entry(sf, textvariable=self._hw_comment, **en
                 ).grid(row=10, column=1, columnspan=3, sticky="ew", padx=2, pady=2)
        tk.Label(sf, text="max 183 chars", bg=BG, fg=FG_DIM,
                 font=("Segoe UI", 7)).grid(row=11, column=1, sticky="w", padx=2)

        ttk.Separator(sf, orient="horizontal").grid(row=12, column=0, columnspan=4,
                                                     sticky="ew", pady=4)

        # Buttons
        bf2 = tk.Frame(sf, bg=BG); bf2.grid(row=13, column=0, columnspan=4, sticky="ew")
        bf2.columnconfigure(0, weight=1); bf2.columnconfigure(1, weight=1)
        bf2.columnconfigure(2, weight=1)

        ttk.Button(bf2, text="↺ Read from MCU",
                   command=lambda: self._cmd("hwdesc show")
                   ).grid(row=0, column=0, padx=3, pady=3, sticky="ew")
        ttk.Button(bf2, text="▶ Send to MCU", style="Accent.TButton",
                   command=self._hwdesc_send_all
                   ).grid(row=0, column=1, padx=3, pady=3, sticky="ew")
        ttk.Button(bf2, text="💾 Save to Flash", style="Save.TButton",
                   command=lambda: self._cmd("hwdesc save")
                   ).grid(row=0, column=2, padx=3, pady=3, sticky="ew")

    def _hwdesc_send_all(self):
        """Send all HW descriptor fields to MCU RAM (does not save to flash)."""
        v = self._hw_ver.get().strip() or "1"
        self._cmd(f"hwdesc ver {v}")

        self._cmd(f"hwdesc temp {self._hw_temp.get()}")

        light = self._hw_light.get()
        model = self._hw_light_m.get().strip() or "?"
        self._cmd(f"hwdesc light {'none' if light == 'none' else model}")

        batt = self._hw_batt.get()
        if batt == "none":
            self._cmd("hwdesc batt none")
        elif batt == "adc":
            self._cmd(f"hwdesc batt adc {self._hw_batt_full.get()} {self._hw_batt_empty.get()}")
        else:
            self._cmd("hwdesc batt fuel")

        accel = self._hw_accel.get()
        if accel == "other":
            self._cmd(f"hwdesc accel {self._hw_accel_m.get().strip() or '?'}")
        else:
            self._cmd(f"hwdesc accel {accel}")

        led = self._hw_led.get()
        lm  = self._hw_led_m.get().strip() or "?"
        if led == "none":
            self._cmd("hwdesc led none")
        else:
            self._cmd(f"hwdesc led {led} {lm}")

        freq = self._hw_freq.get().strip() or "30000000"
        ch   = self._hw_ch.get().strip()  or "4"
        pwr  = self._hw_pwr.get().strip() or "4"
        tt   = self._hw_txtype.get().strip() or "colpitts"
        self._cmd(f"hwdesc tx {freq} {ch} {pwr} {tt}")

        cmt = self._hw_comment.get().strip()
        if len(cmt) > 183:
            cmt = cmt[:183]
        if cmt:
            self._cmd(f"hwdesc comment {cmt}")
        else:
            self._cmd("hwdesc comment")

    def _parse_hwdesc_line(self, s: str):
        """Parse [HWDESC] key = value lines and update GUI fields."""
        m = re.match(r"^\[HWDESC\]\s+(\w[\w_]*)\s*=\s*(.+)$", s)
        if not m: return
        key = m.group(1).strip()
        val = m.group(2).strip()

        if key == "hw_version":
            self._hw_ver.set(val)
        elif key == "temp":
            # val = "crystal" / "none" / "NTC" / ...
            self._hw_temp.set(val.lower() if val.lower() in
                              ["none","crystal","ntc","stts22h","lis2dw12"] else "none")
        elif key == "light":
            if val.startswith("none"):
                self._hw_light.set("none"); self._hw_light_m.set("")
            else:
                self._hw_light.set("present")
                m2 = re.search(r"\((.+)\)", val)
                if m2: self._hw_light_m.set(m2.group(1))
        elif key == "batt":
            if val.startswith("none"):
                self._hw_batt.set("none")
            elif val.startswith("ADC"):
                self._hw_batt.set("adc")
                m2 = re.search(r"full=(\d+)mV", val)
                m3 = re.search(r"empty=(\d+)mV", val)
                if m2: self._hw_batt_full.set(m2.group(1))
                if m3: self._hw_batt_empty.set(m3.group(1))
            else:
                self._hw_batt.set("fuel")
        elif key == "accel":
            v = val.lower()
            if v == "none":   self._hw_accel.set("none"); self._hw_accel_m.set("")
            elif "ism330" in v: self._hw_accel.set("ism330"); self._hw_accel_m.set("")
            elif "lis2dw12" in v: self._hw_accel.set("lis2dw12"); self._hw_accel_m.set("")
            else:
                self._hw_accel.set("other")
                m2 = re.search(r"\((.+)\)", val)
                if m2: self._hw_accel_m.set(m2.group(1))
        elif key == "led":
            if val.startswith("none"):
                self._hw_led.set("none"); self._hw_led_m.set("")
            else:
                self._hw_led.set("rgb" if "RGB" in val else "led")
                m2 = re.search(r"\((.+)\)", val)
                if m2: self._hw_led_m.set(m2.group(1))
        elif key == "tx_freq":
            m2 = re.search(r"(\d+)", val)
            if m2: self._hw_freq.set(m2.group(1))
        elif key == "tx_channels":
            self._hw_ch.set(val.strip())
        elif key == "tx_pwr_lvls":
            self._hw_pwr.set(val.strip())
        elif key == "tx_type":
            self._hw_txtype.set(val.strip())
        elif key == "comment":
            self._hw_comment.set(val.strip())

    # ── RX parsing ────────────────────────────────────────────────────────────
    def _poll_queue(self):
        try:
            while True:
                line = self.rx_queue.get_nowait()
                self._dispatch(line)
        except queue.Empty:
            pass
        self.after(80, self._poll_queue)

    def _dispatch(self, line: str):
        s = line.strip()

        if "[RTC]" in s:
            tag = "rtc"
            m = re.search(r"\[RTC\]\s+(\d{4}-\d{2}-\d{2})\s+(\d{2}:\d{2}:\d{2})\s+(\w+)", s)
            if m:
                self.sv["rtc_date"].set(f"{m.group(1)}  {m.group(3)}")
                self.sv["rtc_time"].set(m.group(2))
            else:
                m2 = re.search(r"\[RTC\]\s+(.+)", s)
                if m2: self.sv["rtc_time"].set(m2.group(1).strip())
        elif "[SCHED]" in s:
            tag = "sched"
            m = re.search(r"\[SCHED\]\s+(.+)", s)
            if m: self.sv["sched_status"].set(m.group(1).strip()[:20])
        elif "[TEMP]" in s:
            tag = "temp"
            m = re.search(r"\[TEMP\].*chip=(-?\d+\.\d+)C\s+VDDA=(\d+)mV", s)
            if m:
                self.sv["temp_c"].set(m.group(1) + " °C")
                self.sv["vdda_mv"].set(m.group(2) + " mV")
        elif "[BATT]" in s:
            tag = "temp"
            m = re.search(r"Battery:\s*(\d+)mV\s+(\d+)%(?:\s+raw=(\d+)\s+vref=(\d+))?", s)
            if m:
                self.sv["batt_mv"].set(m.group(1) + " mV")
                self.sv["batt_pct"].set(m.group(2) + " %")
                if m.group(3):
                    self.sv["batt_raw"].set(m.group(3))
                    self.sv["batt_vref"].set(m.group(4))
        elif "[LIGHT]" in s:
            tag = "temp"
            m = re.search(r"Light:\s*(\d+)\s+\(raw\)\s+~(\d+)\s+lux", s)
            if m:
                self.sv["light_raw"].set(m.group(1))
                self.sv["light_lux"].set("~" + m.group(2) + " lux")
        elif "[TX ON"  in s or ("[TX]" in s and " ON"  in s):  tag = "tx_on"
        elif "[TX OFF]" in s or ("[TX]" in s and " OFF" in s) or "[ECO" in s: tag = "tx_off"
        elif "[GEKON]" in s:  tag = "gekon"
        elif "[SHUTDOWN]" in s or "Shutdown" in s or "SLEEP" in s: tag = "shut"
        elif "[PWR DIAG]" in s or "[PRE-WFI]" in s: tag = "diag"
        elif "[LOG]" in s:
            tag = "log"
            self._log_in_dump = False
            self._parse_log_line(s)
        elif s.startswith("idx,ts,flags"):
            tag = "log"
            self._log_in_dump = True
            self._log_csv_buf = []
            if self._log_dump_tree:
                for item in self._log_dump_tree.get_children():
                    self._log_dump_tree.delete(item)
        elif self._log_in_dump and re.match(r"^\d+,", s):
            tag = "log"
            self._log_append_csv_row(s)
        else: tag = ""

        self._log(f"<<< {line}\n", tag)
        self._parse_status(s)
        self._parse_sched_line(s)
        if "[HWDESC]" in s:
            self._parse_hwdesc_line(s)

    def _parse_status(self, s: str):
        m = re.match(r"^\s*(\w+)\s*=\s*(.+)$", s)
        if m:
            key = m.group(1).strip()
            val = m.group(2).strip().split("[")[0].strip()
            if key in self.sv: self.sv[key].set(val)
            if   key == "mode":     self._mode_var.set(val)
            elif key == "ch":
                n = re.search(r"(\d+)", val)
                if n: self._ch_var.set(int(n.group(1)))
            elif key == "pwr":
                n = re.search(r"(\d+)", val)
                if n: self._pwr_var.set(int(n.group(1)))
            elif key == "pulse_ms":  self._pulse_var.set(val)
            elif key == "period_ms": self._period_var.set(val)
            elif key == "led_mode":  self._led_mode_var.set(val)
            elif key == "temp_mode":
                v = val.strip()
                if v in ("off", "periodic", "tx"):
                    self._temp_mode_var.set(v)
            elif key == "temp_period":
                self._temp_period_var.set(val.split()[0])
            elif key == "temp_offset":
                self._temp_offset_var.set(val.split()[0])
            elif key == "rtc_live":
                self._rtc_live_var.set(val.strip() == "on")
            elif key == "batt_mode":
                v = val.strip()
                if v in ("off", "periodic"):
                    self._batt_mode_var.set(v)
            elif key == "batt_period":
                self._batt_period_var.set(val.split()[0])
            elif key == "batt_scale":
                self._batt_scale_var.set(val.strip().split()[0])
            elif key == "batt_mv":
                self.sv["batt_mv"].set(val.strip() + " mV")
            elif key == "batt_pct":
                self.sv["batt_pct"].set(val.strip() + " %")
            elif key == "light_mode":
                v = val.strip()
                if v in ("off", "periodic"):
                    self._light_mode_var.set(v)
            elif key == "light_period":
                self._light_period_var.set(val.split()[0])
            elif key == "light_raw":
                self.sv["light_raw"].set(val.strip())
            elif key == "light_lux":
                self.sv["light_lux"].set("~" + val.strip() + " lux")
            return

        m = re.match(r"^\s*active now:\s*(.+)$", s)
        if m:
            val = m.group(1).strip()
            self.sv["sched_status"].set(val)
            self._sv_active_now.set(val)
            if self._active_lbl:
                self._active_lbl.configure(fg=GREEN if val == "YES" else RED)

    def _parse_sched_line(self, s: str):
        m = re.match(r"^\s*hours\s*:\s*(.+)$", s)
        if m:
            val = m.group(1).strip()
            self._sv_hours_disp.set(val)
            for v in self._hour_vars: v.set(False)
            if val != "all":
                for tok in val.split():
                    if tok.isdigit():
                        h = int(tok)
                        if 0 <= h <= 23: self._hour_vars[h].set(True)
            self._update_sched_from_parse(); return

        m = re.match(r"^\s*days\s*:\s*(.+)$", s)
        if m:
            val = m.group(1).strip()
            self._sv_days_disp.set(val)
            for v in self._day_vars: v.set(False)
            if val != "all":
                day_map = {name: i for i, name in enumerate(_DAY_NAMES)}
                for tok in val.split():
                    if tok in day_map: self._day_vars[day_map[tok]].set(True)
            self._update_sched_from_parse(); return

        m = re.match(r"^\s*months\s*:\s*(.+)$", s)
        if m:
            val = m.group(1).strip()
            self._sv_months_disp.set(val)
            for v in self._month_vars: v.set(False)
            if val != "all":
                mon_map = {name: i for i, name in enumerate(_MONTH_NAMES)}
                for tok in val.split():
                    if tok in mon_map: self._month_vars[mon_map[tok]].set(True)
            self._update_sched_from_parse()

    # ── Log ───────────────────────────────────────────────────────────────────
    def _log(self, text: str, tag: str = ""):
        self.log.configure(state="normal")
        self.log.insert("end", text, tag if tag else ())
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
    app = TxBeaconGUI()
    app.mainloop()
