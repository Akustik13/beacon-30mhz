# Task: Beacon Manager — Android app (Flutter) v3, ported from working PC app v4

## Context

Reference implementation (READ COMPLETELY FIRST, it is the source of truth):
```
C:\Users\prymv\STM32CubeIDE\workspace_1.19.0\Beacon_30MHz\pc_app\tx_beacon_gui_v4.py
```
This is a WORKING, STABLE PyQt6 desktop app — both UART and BLE
transports confirmed stable on real hardware. Port its protocol,
data model and screen logic to a Flutter Android app. Do NOT
reinvent the wire protocol — copy it exactly from this file.

Also read CLAUDE.md for project/hardware background.

**BLE ONLY on Android — do not implement UART/serial.** The phone
has no USB-serial role here; only the BLE path matters.

There is an OLDER, incomplete Flutter attempt somewhere in the
project history — IGNORE it, do not reuse its code. Build fresh
using v4's protocol as ground truth. If an existing `flutter_app`
folder exists, back it up first:
```cmd
xcopy /E /I flutter_app flutter_app_v2_backup
```
New/working folder: `flutter_app`

---

## CRITICAL: the transport is Nordic UART Service (NUS) — not custom GATT

Read `BleTransport(UartTransport)` in tx_beacon_gui_v4.py (~line 1748)
before writing any Dart code. Key facts to replicate exactly:

```
Service:  6e400001-b5a3-f393-e0a9-e50e24dcca9e   (Nordic UART Service)
RX char:  6e400002-b5a3-f393-e0a9-e50e24dcca9e   write  (phone → MCU)
TX char:  6e400003-b5a3-f393-e0a9-e50e24dcca9e   notify (MCU → phone)
```

The wire protocol is the SAME TEXT LINE PROTOCOL used over UART:
```
phone → MCU:  "#<id> <VERB>[ <hex payload>]\r\n"
MCU → phone:  "#<id> <line>\r\n"  (0..N data lines)
              "#<id> OK\r\n"           (success)
           or "#<id> ERR<reason>\r\n"  (failure)
```
- `id` is a request counter (1..65535, wrap around), chosen by the
  client, matched in the response — exactly like `_request()` in
  UartTransport/BleTransport.
- Outgoing writes are chunked to `MTU - 3` bytes, `write_gatt_char`
  with `response=false` (write-without-response), ~10ms gap between
  chunks if the command is longer than one chunk (see `_async_write`
  in the reference).
- Negotiate MTU 247 on connect (`request_mtu` equivalent — check
  what flutter_blue_plus exposes; if not available just proceed with
  default MTU 23 gracefully, don't crash).
- Incoming notifications arrive as raw bytes; **accumulate** and
  split on `\n`, strip trailing `\r`, feed each resulting text line
  to the response router (see `_on_notify` in reference).
- ONE command in flight at a time — serialize with a lock/mutex,
  exactly like `_ble_cmd_lock` in the reference. Sending a new
  command while one is awaiting its `#id OK/ERR` must queue, not
  interleave.
- Timeout per command: default 5s (align with reference per-opcode
  timeouts — some ops like OP_LOG_READ or OP_BLE_SET use longer
  timeouts, check the reference `send_cmd(..., timeout=...)` calls
  and replicate the same per-opcode timeout values).

---

## Opcodes (copy exactly from reference file, do not renumber)

Read the full `OP_*` constant block (~line 624-665) and the
`CMD_ERR_*` constants (~line 617) in tx_beacon_gui_v4.py. Reproduce
every opcode value in Dart. Includes at minimum:

```
OP_PING, OP_TIME_GET, OP_TIME_SET, OP_REBOOT
OP_CONFIG_INFO, OP_CONFIG_READ, OP_CONFIG_WRITE, OP_CONFIG_COMMIT, OP_CONFIG_RESET
OP_LOG_INFO, OP_LOG_READ, OP_LOG_ERASE, OP_LOG_MARK
OP_SENSOR_LIST, OP_SENSOR_ENABLE, OP_SENSOR_INTERVAL, OP_SENSOR_READ_NOW
OP_ACCEL_CFG_GET, OP_ACCEL_CFG_SET, OP_ACCEL_POWER, OP_ACCEL_PROBE
OP_TX_GET, OP_TX_SET, OP_TX_SCHEDULE_GET, OP_TX_SCHEDULE_SET
OP_WAKE_CFG_GET, OP_WAKE_CFG_SET, OP_WAKE_STATUS, OP_WAKE_CLEAR
OP_HWDESC_GET, OP_HWDESC_SET, OP_HWDESC_COMMIT
OP_UART_MODE   (no-op over BLE — return CMD_OK immediately, do not send)
OP_BLE_GET, OP_BLE_SET, OP_BLE_RSSI, OP_MEASURE_ALL
```

---

## Data structures — port field-for-field from reference

Read these dataclasses in the reference and reproduce them as
immutable Dart classes with `fromBytes()`/`toBytes()` using
`dart:typed_data` (`ByteData`, little-endian throughout):

1. **ConfigBlob** (64 bytes, ~line 388) — every field, exact byte
   offsets from `_CFG_FMT` struct format string. Includes CRC32
   over bytes[0:60] stored at bytes[60:64] (`verify_crc()` logic
   must be replicated exactly — reuse the same CRC32 algorithm,
   zlib-compatible, poly 0xEDB88320, init/final 0xFFFFFFFF).
2. **StatusBlob** (24 bytes, ~line 479, format `_STAT_FMT`)
3. **InfoBlob** (~line 512)
4. **LogRecord** (v1, ~line 550) and **LogRecordV2** (~line 592)
   with `LOGREC_TYPE_*` constants (TEMP=0x00, BATT=0x01, LIGHT=0x02,
   ACCEL=0x03, MARKER=0xFE, EMPTY=0xFF) — port the exact payload
   parsing per type, including the ACCEL x/y/z/fs decode.
5. **BleSettings** (~line 682) with `BLE_OP_*` mode constants
   (OFF/CONTINUOUS/SCHEDULE/GEKON)
6. **HwDescBlob** (~line 721)
7. **SENSOR_ID_*** constants (~line 785)

Write a Dart equivalent of `run_selftest()` (~line 802): pack→unpack
roundtrip for ConfigBlob, CRC corruption detection, and a known-value
CRC32 check. Run it once at app startup in debug mode (assert or
print PASS/FAIL) so protocol bugs are caught immediately, not after
hours of confusing UI testing.

---

## Tech stack

```yaml
flutter_blue_plus: ^1.32.0   # BLE
provider: ^6.1.0             # state
shared_preferences: ^2.2.0   # settings persistence
sqflite: ^2.3.0              # profiles + session log storage
path_provider: ^2.1.0
fl_chart: ^0.68.0            # charts (this is the "графіків нема" gap —
                              # make sure EVERY chart in the reference
                              # DataTab has a Flutter equivalent, see below)
geolocator: ^11.0.0          # session GPS logging (optional field)
share_plus: ^7.2.0           # CSV export sharing
permission_handler: ^11.0.0
intl: ^0.19.0
```

---

## Design language

Match the reference app's actual look — it already has a finished,
tested QSS theme (`build_qss()`, ~line 149) with light AND dark
palettes, `ToggleSwitch`, `ChipButton`, `ValueCard`, `OverwriteBar`,
`BannerWidget` custom widgets (~lines 2313-2601). Read these and
reproduce the same visual language in Flutter:

- Card-based layout, rounded corners ~10px, subtle borders
- Chip-style segmented selectors (channel/power/mode buttons)
- iOS-style toggle switches for on/off sensors
- Colored progress/overwrite bar for flash usage
- Banner widget for apply-result feedback (green success / red error)
- Light theme default, dark theme toggle available (reuse the two
  palettes' hex values from `build_qss` — extract COLORS dict)

Mobile adaptations vs desktop:
- Bottom navigation bar instead of top QTabWidget (5 tabs, see below)
- Larger touch targets (min 48dp)
- Single-column layouts instead of side-by-side panels
- Pull-to-refresh where the desktop app has a manual refresh button

---

## Screens (bottom nav, 5 tabs) — mirror reference tabs, mobile-adapted

### Tab 1: Home (= reference OverviewTab, ~line 2621)

Primary screen on connect. Read `OverviewTab` fully and port:
- Connection status card (device name, RSSI with color coding —
  reuse `_rssi_color()` logic, ~line 1912)
- Value cards grid (temperature/battery/light/tx status) — same
  data as reference `ValueCard` usage, same color thresholds for
  temperature (normal/warning/critical — check exact °C bounds
  used in the reference)
- Device/schedule summary section
- Storage usage bar (reuse `OverwriteBar` concept)
- **Chart is collapsed by default** (user requirement: "графік
  скритий" = chart hidden). Show a small "▾ Show chart" expand
  toggle under the value cards; when expanded, show the temperature
  sparkline (last N minutes) using fl_chart. Collapsed by default
  keeps Home clean and fast on small screens.

### Tab 2: Beacon (= reference BeaconTab, ~line 3149)

TX mode/channel/power/pulse/period controls + schedule (hours/days/
months chip grid + sched_scope selector: TX-only vs TX+logging —
read how `sched_scope` is used in the reference ConfigBlob and
BeaconTab UI). Apply footer → sends OP_CONFIG_WRITE + OP_CONFIG_COMMIT,
shows BannerWidget-equivalent result (reuse the apply+verify pattern
from the reference — read how `StickyFooter`, ~line 4790, and the
apply flow in BeaconTab work).

### Tab 3: Logging (= reference LoggingTab, ~line 3670)

Per-sensor toggle cards (temp/light/battery/accel) with interval
fields, write-mode selector (always/on_change/adaptive), overflow
mode, change thresholds, and the **memory calculator** — port the
exact calculation logic from LoggingTab (writes/hour, time-to-full
or history-depth for circular mode, reverse "I want N days →
suggested interval").

### Tab 4: Data (= reference DataTab, ~line 4023)

**This is explicitly the tab the user says was incomplete — do it
thoroughly.** Read ALL of DataTab (lines 4023-4790) before writing
Flutter code. It has multiple charts: Temperature, Battery, Light,
Accelerometer (X/Y/Z) — find every `HAS_PG` guarded chart block and
give each one a working fl_chart equivalent, not a placeholder.
Also port:
- Log download with progress (chunked OP_LOG_READ reads)
- Mixed v1/LogRecord + v2/LogRecordV2 record display (see how the
  reference table/chart code branches on `fmt_ver` — read
  `_on_dl_batch` logic mentioned in prior session notes, port the
  same v1/v2 dispatch)
- Table view with sortable columns
- CSV export (use `share_plus` to trigger Android share sheet
  instead of desktop file dialog)
- Erase log with confirmation

### Tab 5: Devices (scan + saved beacons + BLE settings)

Combine reference `BleScanDialog` (~line 2059) + `BleTab` (~line 5492)
content into one mobile tab:
- Scan button → list of found devices, beacons (advertising the
  known service/name pattern) highlighted, others greyed
- RSSI bar per device (reuse `_rssi_color`)
- Tap → connect
- Saved beacons list (name, icon, last seen, last battery%) — persist
  in sqflite, port `ProfileManager` (~line 2261) concept for
  saved-device + profile management
- BLE settings section: op_mode (off/continuous/schedule/gekon),
  tx_power, advertising interval, window/sleep duration, name suffix
  — maps to `BleSettings`/`OP_BLE_GET`/`OP_BLE_SET`

---

## Profiles (port ProfileManager, ~line 2261)

Local storage (sqflite or shared_preferences json blob), same
concept as desktop: named ConfigBlob presets, built-in presets
(Always active / Day / Night / Field season) not deletable, user
presets fully editable. Works fully offline; Apply only sends to
beacon when connected.

---

## Settings tab content (from reference SettingsTab, ~line 4843)

Fold into Beacon/Logging/Devices tabs rather than a 6th nav item —
mobile apps benefit from fewer top-level destinations. Specifically:
- Accelerometer config (ODR/FS/mode) → Logging tab, accel card
- Sensor calibration (temp offset, battery scale) → Logging tab,
  per-sensor card advanced section (collapsible)
- HW descriptor (board/firmware info, read-only mostly) → About
  screen (accessible from app bar overflow menu)
- Time sync (OP_TIME_SET) → Home tab, small "Sync time" action
- Wake config (OP_WAKE_CFG_*, gekon wake status) → Devices tab,
  BLE settings section (wake and BLE op_mode are related concepts)

## About screen (app bar overflow menu)

- App name, version, Sevskiy GmbH, contact info (same as prior
  Flutter prompt's About screen content)
- HW descriptor display (from OP_HWDESC_GET)
- Firmware version (from InfoBlob)
- Debug: protocol self-test result (PASS/FAIL badge)

---

## Android manifest

```xml
<uses-permission android:name="android.permission.BLUETOOTH_SCAN"
    android:usesPermissionFlags="neverForLocation" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
<uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION" />
```
minSdkVersion 21, targetSdkVersion 34.

---

## File structure

```
lib/
├── main.dart
├── theme/
│   └── app_theme.dart          ← light+dark palettes ported from build_qss()
├── protocol/
│   ├── opcodes.dart             ← OP_*, CMD_ERR_* constants
│   ├── config_blob.dart
│   ├── status_blob.dart
│   ├── info_blob.dart
│   ├── log_record.dart          ← v1 + v2 + LOGREC_TYPE_*
│   ├── ble_settings.dart
│   ├── hwdesc_blob.dart
│   ├── crc32.dart
│   └── selftest.dart
├── transport/
│   └── nus_ble_transport.dart   ← flutter_blue_plus, chunked write,
│                                   #id request/response router, 1-at-a-time
├── services/
│   ├── profile_service.dart
│   ├── storage_service.dart
│   └── export_service.dart
├── providers/
│   ├── ble_provider.dart
│   ├── beacon_provider.dart
│   └── devices_provider.dart
└── screens/
    ├── main_screen.dart          ← bottom nav shell
    ├── home_tab.dart
    ├── beacon_tab.dart
    ├── logging_tab.dart
    ├── data_tab.dart
    ├── devices_tab.dart
    ├── about_screen.dart
    └── widgets/
        ├── value_card.dart
        ├── chip_button.dart
        ├── toggle_switch.dart
        ├── overwrite_bar.dart
        ├── banner_widget.dart
        └── rssi_indicator.dart
```

---

## Build order

1. `protocol/` — all structs + CRC32 + selftest, verify with
   `flutter test` before touching UI
2. `transport/nus_ble_transport.dart` — connect, request/response,
   chunking, single-flight lock; test manually against real beacon
   with just OP_PING and OP_CONFIG_INFO before building screens
3. Devices tab (scan + connect) — needed to test everything else
4. Home tab
5. Beacon tab
6. Logging tab
7. Data tab (the priority gap — budget the most time here, verify
   every chart type renders with real downloaded data)
8. Profiles + About
9. Theme polish (light/dark)

## Definition of done

- Scans, finds beacon, connects via NUS
- OP_PING roundtrip succeeds, protocol selftest passes
- Home shows live StatusBlob values updating (via OP_MEASURE_ALL
  or periodic OP_SENSOR_READ_NOW — check how reference OverviewTab
  polls, replicate same interval)
- Beacon tab: change channel/power, Apply → CONFIG_WRITE+COMMIT →
  verify readback matches → green confirmation
- Logging tab: toggle a sensor, calculator updates live
- Data tab: download log, see Temperature AND Battery AND Light
  AND Accelerometer charts all rendering (this is the explicit
  bug-fix target from the previous broken attempt)
- CSV export triggers Android share sheet with a valid file
- Works with BLE disconnected mid-operation without crashing
  (show reconnect prompt, don't lose entered config)
