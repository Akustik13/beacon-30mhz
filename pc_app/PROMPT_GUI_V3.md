# Task: TX Beacon GUI v3 — Light theme, transport abstraction, BLE-ready

## Context

Path: C:\Users\prymv\STM32CubeIDE\workspace_1.19.0\Beacon_30MHz\pc_app\
New file: tx_beacon_gui_v3.py (single file).
Reference (do not modify): tx_beacon_gui.py (v1, tkinter).
Firmware binary protocol: read PROMPT_BINARY_PROTOCOL.md — v3 talks
this protocol (#id CFG?/CFG!/STAT?/INFO?/LOG?/SAVE!), NOT the old
per-parameter text commands.

## Philosophy

SIMPLE > clever. This is for END USERS (field researchers), not
developers. Fewer controls, logical groups, obvious flow:
connect → see everything → change → apply → clear confirmation.
No terminal tab, no raw commands, no register dumps in this app
(v1 stays for lab debugging).

## Tech

PyQt6 + pyserial + pyqtgraph. Python 3.10+.
`pip install PyQt6 pyserial pyqtgraph`

---

## LIGHT THEME

```python
COLORS = {
    'bg':        '#f5f6fa',   # window background
    'card':      '#ffffff',   # panels
    'border':    '#e1e4ea',
    'accent':    '#2563eb',   # primary blue
    'success':   '#16a34a',
    'warning':   '#d97706',
    'danger':    '#dc2626',
    'text':      '#1e293b',
    'text_dim':  '#64748b',
    'chip_on':   '#dbeafe',   # selected chip bg (blue-100)
    'chip_on_t': '#1d4ed8',   # selected chip text
}
```
Cards: white, 1px border #e1e4ea, radius 10px, subtle padding.
Buttons: accent filled for primary actions, outlined for secondary.
Font: Segoe UI 10-11pt; values on dashboard 22-26pt semibold.
No emojis in widget labels — use short clear English words.

---

## TRANSPORT ABSTRACTION (core of v3!)

```python
class Transport(QObject):
    """Abstract transport. Same byte structures over UART or BLE."""
    telemetry  = pyqtSignal(str)          # raw untagged lines (UART only)
    connected_changed = pyqtSignal(bool)

    def connect(self, target: str) -> bool: ...
    def disconnect(self): ...
    def read_info(self)   -> bytes: ...   # 32 B InfoBlob
    def read_config(self) -> bytes: ...   # 64 B ConfigBlob
    def write_config(self, blob: bytes) -> tuple[bool, str]: ...
    def save_flash(self)  -> tuple[bool, str]: ...
    def read_status(self) -> bytes: ...   # 24 B StatusBlob
    def read_log(self, offset: int, count: int) -> bytes: ...

class UartTransport(Transport):
    """
    Serial + '#id VERB hex' protocol from PROMPT_BINARY_PROTOCOL.md.
    - QThread reader; splits tagged (#) vs telemetry lines
    - request(): send '#<id> ...', wait for matching '#<id> ...' lines
      until '#<id> OK/ERR', timeout 2 s
    - hex encode/decode payloads
    """

class MockBleTransport(Transport):
    """
    BLE SIMULATOR — full featured, no hardware needed.
    - Holds a fake ConfigBlob/StatusBlob/log in memory
    - read_status(): returns slowly drifting temp (36.0-37.5),
      bat slowly dropping, light day-curve; 30-80 ms artificial delay
    - write_config(): validates CRC32 exactly like firmware,
      returns (False, 'crc') on mismatch — lets you test error UI
    - read_log(): generates plausible history records
    - Purpose: develop/demo the whole GUI without a beacon,
      and rehearse the future BLE flow (delays, chunked log reads)
    """
```

Struct pack/unpack: implement `ConfigBlob`, `StatusBlob`, `InfoBlob`,
`LogRecord` dataclasses with `from_bytes()` / `to_bytes()` using
`struct` module, little-endian, layouts EXACTLY per
PROMPT_BINARY_PROTOCOL.md. CRC32 = `binascii.crc32`.

Connection bar has a source selector: `[Serial COMx ▾] [BLE (demo)]`.

---

## WINDOW LAYOUT — 4 tabs only

```
┌──────────────────────────────────────────────────────┐
│  ● Connected COM3          [Serial ▾] [Disconnect]   │ top bar
├──────────────────────────────────────────────────────┤
│  [ Overview ] [ Beacon ] [ Logging ] [ Data ]        │ tabs
└──────────────────────────────────────────────────────┘
```

### TAB 1 — Overview (default)

Read-only. Auto-refresh STAT? every 1 s while connected.

Row of 4 value cards:
```
 Temperature      Battery          Light           Transmitter
   36.4 °C        3.72 V · 82%      1240            CH1 · P3
   normal         ~14 months        ~350 lux        ● transmitting
```
Temp value colored: green 36-38, amber 35-36/38-39, red outside.
TX card: green dot when tx_active, gray when idle.

Below, two cards side by side:
- "Device": fw version, uid, tag, uptime, RTC time, schedule state
  ("Active now · next change 18:00")
- "Storage": progress bar log_used/log_total, "247 / 768 records",
  estimate line "≈ 12.6 h history at current rate"

Bottom: temperature sparkline (pyqtgraph, last 10 min, no axes
clutter, light background).

### TAB 2 — Beacon  (transmitter + schedule)

Card "Transmitter":
```
 Mode      ( ) Off   (•) Pulse   ( ) Continuous   ( ) Eco
 Channel   [CH0] [CH1] [CH2] [CH3]          ← segmented chips
 Power     [P1] [P2] [P3] [P4]
 Pulse     [  23 ] ms      Period [ 2000 ] ms
```

Card "Schedule":
```
 [x] Enable schedule
 Hours   00-23 chips in 2 rows, quick: All · None · Day 8-18 · Night
 Days    Mon..Sun chips, quick: All · Weekdays
 Months  Jan..Dec chips, quick: All · Apr-Sep
 Status line: "Active now" (green) / "Inactive — next window 08:00"
```
Chips = QPushButton checkable, chip_on colors when checked.
Editing anything only changes local state and enables the footer.

Sticky footer (both edit tabs):
```
 [ Revert ]                    [ Apply to beacon ]
```
Apply → build ConfigBlob → CRC → CFG! → then CFG? re-read →
compare byte-by-byte → result banner INSIDE the window top:
- green "✓ All settings applied and verified" (auto-hide 4 s)
- red "✗ Not applied: <field/reason>" (stays until dismissed)
After verified apply → automatically SAVE! (persist), include in
banner: "…and saved to flash".

### TAB 3 — Logging  (sensor logging config + calculator)

Card per sensor — collapsed row when off, expanded when on:
```
 Temperature                                   [ON  ●──]
   Measure & log every [ 60 ] s
 Battery                                       [ON  ●──]
   Measure & log every [ 3600 ] s
 Light                                         [OFF ──○]
```
(One interval = poll + flash write, per user requirement.)

Card "Write strategy":
```
 (•) Every interval      ( ) Only when changed   ( ) Adaptive
 When full:  (•) Overwrite oldest   ( ) Stop logging
```
Thresholds row appears only for on_change/adaptive modes.

Card "Memory calculator" (live-updates as fields above change):
```
 768 records total · 16 B each
 Rate: 61 writes/hour
 ▸ Overwrite mode: history depth ≈ 12.6 hours
 ▸ (Stop mode: full in ≈ 8.5 h)
 Target: keep [ 7 ] days  → suggested interval ≥ 13 min
```
Same footer Apply/Revert as Tab 2 (one shared ConfigBlob).

### TAB 4 — Data  (download + chart + export)

Top bar: [ Download from beacon ]  progress bar  "768 records"
Then two sub-views toggled by segmented control [Chart | Table]:
- Chart: pyqtgraph, series selector chips (Temp / Light / Battery),
  X = datetime (rtc_unix base) or +seconds if RTC unset
- Table: idx, time, temp, light, bat% — plain, sortable
Buttons: [ Export CSV ]  [ Erase beacon log… ] (confirm dialog).
Download runs in QThread via read_log(offset, 8) loop; delta records
are decoded to absolute using last checkpoint before display.

---

## Behaviors

- On connect: INFO? → CFG? → populate everything → Overview.
- Any transport error → red toast, stay usable, no crash.
- Window remembers geometry + last port (settings json).
- All texts English. Tooltips on every field (1 short sentence).
- No operation blocks UI thread — requests run in worker with
  busy cursor on the pressed button only.

## Build order

1. Struct dataclasses + CRC + unit self-test in `__main__ --selftest`
   (pack→unpack roundtrip, CRC vector check)
2. Transport base + MockBleTransport (GUI runnable day one!)
3. UartTransport
4. Main window + top bar + tabs skeleton
5. Overview, then Beacon, Logging, Data
6. Apply/verify flow + banners
7. Polish: tooltips, settings persistence

## Definition of done

- `python tx_beacon_gui_v3.py` with NO beacon: pick "BLE (demo)",
  full app works — live values drift, apply succeeds, log downloads
- With beacon on COM: same flows over UART protocol
- Corrupt a byte in mock write path → red banner shows crc error
- Non-technical user can change schedule in <30 seconds

---

## VISUAL REFERENCE — MANDATORY

Four mockup images sit next to this prompt:
```
gui_v3_tab1_overview.png
gui_v3_tab2_beacon.png
gui_v3_tab3_logging.png
gui_v3_tab4_data.png
```
READ ALL FOUR IMAGES FIRST. Reproduce them as closely as PyQt6
allows: same layout, same grouping, same proportions, same colors,
same chip/toggle style, same sticky footer. Where the mockup and
the text above disagree, THE IMAGE WINS for visual matters and
the text wins for behavior/logic.

Specific pixel guidance from mockups:
- Value cards row: 4 equal cards, ~118 px tall, value in 26 px
  semibold, caption 11 px dim below
- Chips: 26-28 px tall, radius 6, selected = #dbeafe bg +
  #1d4ed8 text + #2563eb 1px border; unselected = white bg,
  #64748b text, #e1e4ea border
- Toggle switch: 46×24 px pill, green #16a34a when on, white knob
- Sticky footer: 64 px, white, top border, Revert left (outlined),
  "Apply to beacon" right (filled accent, 190×36)
- Success banner: #dcfce7 bg, #16a34a border, dark-green bold text
- Cards: white, radius 10, 1 px #e1e4ea border, 16 px padding

---

## THEME SWITCHER (light/dark)

Implement BOTH themes from day one:

1. All colors live in `THEMES = {'light': {...}, 'dark': {...}}`
   (light palette = COLORS above; dark palette:
   bg #0f1117, card #1a1d27, border #2e3147, accent #4f8ef7,
   text #e2e8f0, text_dim #94a3b8, chip_on #1e3a8a,
   chip_on_t #93c5fd, input #242736, banner_ok #14532d,
   success #22c55e, warning #f59e0b, danger #ef4444).
2. RULE: zero hard-coded colors inside widget code. One
   `build_qss(C)` template string generates the app-wide
   stylesheet; anything QSS can't style (pyqtgraph plots, dynamic
   value colors like temperature status) goes through a
   `retheme()` method that re-applies from the active palette.
3. Top bar gets a small icon button (sun/moon) on the right of
   Disconnect. Click → instant switch, no restart.
4. Persist choice in settings json (`"theme": "light"` default).
5. Both themes must look correct on all 4 tabs including chips,
   toggles, banners, charts, table.

---

## OFFLINE MODE + PROFILES (mandatory)

### Offline editing
The app is fully usable with NO connection:
- Beacon and Logging tabs are editable offline (footer button reads
  "Save as profile…" instead of "Apply to beacon" when disconnected).
- Memory calculator works offline using default 768 total records
  (small hint "estimate — beacon not connected"); after connect it
  switches to real total/used from InfoBlob/StatusBlob and re-renders.
- Overview shows placeholder dashes when disconnected.

### Profiles
Local JSON file tx_beacon_profiles_v3.json. A profile = full
ConfigBlob content + name + optional note.
- Profile bar at top of Beacon tab: [Profile: Field season ▾]
  [Apply] [Save as…] [Delete]
- Built-in presets (not deletable): "Always active",
  "Day 08-18", "Night 22-06", "Field season Apr-Sep eco".
- Selecting a profile fills Beacon+Logging tabs (GUI only).
- [Apply] when connected: one CFG! transaction + verify + SAVE!
  + result banner — the fast field workflow: connect → pick →
  Apply → done in 3 clicks.
- [Apply] when disconnected: fills GUI and shows toast
  "Profile loaded — connect beacon to apply".
- Editing any field after loading a profile marks the selector
  "Field season (modified)".

---

## ADDENDUM: schedule scope selector

ConfigBlob has `sched_scope` (byte after sched_en; see protocol
prompt addendum): 0 = TX only, 1 = TX + logging.

In the Schedule card (Beacon tab), directly under "Enable
schedule", add a two-chip selector:

```
 Schedule applies to:  [Transmitter only]  [Transmitter + logging]
```

- Default: "Transmitter only".
- Dynamic hint text under the chips:
  - TX only: "Sensors keep measuring and logging around the clock."
  - TX + logging: "Outside the schedule everything sleeps —
    no sensor data is recorded."
- Memory calculator MUST account for scope: when
  sched_scope=1 and schedule enabled, effective writes/hour are
  scaled by (active hours count / 24) × (active days / 7) —
  show the scaled rate and note "schedule-adjusted".
- Profiles store sched_scope like every other field.
