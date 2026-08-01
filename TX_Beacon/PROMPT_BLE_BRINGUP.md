# Task: BLE Stack Bring-up (Phase 1: minimal) + Configurable Presets (Phase 2)

## Context

Project: C:\Users\prymv\STM32CubeIDE\workspace_1.19.0\Beacon_30MHz\TX_Beacon
Read CLAUDE.md, PROMPT_FIRMWARE_V3.md, PROMPT_BINARY_PROTOCOL.md first.

Current state: TX + sleep (Stop2 verified <2µA) + gekon + UART binary
protocol (#id CFG?/CFG!/STAT?/INFO?/LOG?/SAVE!) + Flash log v2 (with
ACCEL record type) — all debugged and stable on real hardware.

BLE stack is ALREADY flashed and verified working on this board from
earlier debugging session:
```
FUS Version:   v2.1.0
STACK Version: v1.23.0.3  @ 0x08022000
```
Do NOT re-flash the BLE stack unless bring-up fails and you suspect
stack corruption — ask first.

## CRITICAL constraints from earlier debugging (do not re-break these)

1. **CPU2 low-power quirk**: `svc_power.c` calls
   `LL_C2_PWR_SetPowerMode(LL_PWR_MODE_SHUTDOWN)` before every Stop2
   entry to get <2µA. Once BLE is active, CPU2 must NOT be forced to
   Shutdown while advertising/connected — that kills the radio.
   New rule: Stop2 with CPU2-shutdown is only allowed when
   BLE is fully idle (not advertising, not connected). Add a guard:
   ```c
   bool BLE_IsIdle(void); /* false while advertising or connected */
   ```
   `Power_EnterStop2()` must check this and skip the CPU2-shutdown
   step (use a lighter Stop mode or just don't enter Stop2) whenever
   BLE_IsIdle() is false.

2. **UART command buffer**: was enlarged for the #id binary protocol
   (from 64 to 192 bytes) — don't shrink it back.

3. **Flash page size = 2048 bytes** (WB1M, not 4096). BLE stack sits
   at page 68+ — never touch.

---

## PHASE 1 — Minimal bring-up (goal: prove the radio works)

### Objective
CPU2 advertises once per second. Connect via nRF Connect (Android/
iOS app). See a simple counter or the existing StatusBlob update
live. Nothing configurable yet — hardcoded sane defaults.

### Files to create

```
STM32_WPAN/App/ble_beacon_service.c/.h   ← minimal GATT service
```

Reuse existing `app_ble.c`/`app_entry.c` BLE stack init pattern from
the earlier BLE_HR_p2p_Sensor reference project if useful for
initialization sequence, but build a FRESH minimal service — do not
port the Heart Rate / p2p LED characteristics.

### GATT service (minimal, phase 1)

```
Service UUID:  0x0000BEAC-0000-1000-8000-00805F9B34FB
Characteristic 0xBEA1 — StatusBlob (READ + NOTIFY)
  Same 24-byte StatusBlob_t from PROMPT_BINARY_PROTOCOL.md.
  Notify every 1 second while connected (reuse existing status
  gathering code — do not duplicate sensor read logic).
```

That's it for phase 1. One characteristic, read-only, notify-only.
No write, no config exchange yet — we're proving the radio and
GATT stack work, nothing more.

### Advertising

```
Device name: "BCN_" + last 4 hex digits of MCU UID
             (reuse UID read already used in InfoBlob_t.uid)
Interval: 1000 ms  (fixed for phase 1 — "advertise once per second"
                    as requested)
TX power: default/max for phase 1, tune in phase 2
Advertise continuously — no timeout for this bring-up test.
```

### Boot sequence changes in main.c

```
1. Existing init (unchanged)
2. MX_APPE_Init()          — start BLE stack (CPU2)
3. BLE_Beacon_Init()        — register GATT service, start advertising
4. Existing main loop, PLUS:
   - Every 1s: if BLE connected, update+notify StatusBlob
   - Power_EnterStop2() calls now gated by BLE_IsIdle() per
     constraint #1 above
```

### LED behavior (extend existing led_manager)

```
Advertising (not connected): LED_BLINK_SLOW (1 Hz)
Connected:                   LED_ON (solid)
```

### UART debug output (extend existing style)

```
[BLE] Stack ready. UID=0xA3F2C1B0
[BLE] Advertising as "BCN_C1B0" (interval=1000ms)
[BLE] Client connected. RSSI will vary.
[BLE] Notify StatusBlob (24 bytes)
[BLE] Client disconnected. Resuming advertising.
```

### Testing protocol for Phase 1

```
1. Flash, open serial monitor → verify boot + "[BLE] Advertising..."
2. LED blinks 1 Hz
3. Open nRF Connect → scan → find "BCN_XXXX"
4. Connect → LED goes solid
5. Find service 0xBEAC → characteristic 0xBEA1
6. Enable notifications → see 24-byte updates once per second
7. Manually decode a few bytes (temp field) → sanity check vs UART
   STAT? output for the same moment
8. Disconnect → LED resumes blinking, advertising resumes
9. Verify Stop2 still reaches <2µA when BLE is idle (gekon test,
   measure with µA meter) — confirms constraint #1 didn't break
   existing power behavior
10. Reconnect 5+ times in a row — no hang, no HardFault
```

STOP HERE and report results before starting Phase 2. Phase 2 only
makes sense once Phase 1 is confirmed stable on real hardware.

---

## PHASE 2 — Configurable BLE presets via UART (do only after Phase 1 confirmed)

### Objective
Control BLE behavior the same way RF/logging is controlled: through
the existing #id binary protocol and ConfigBlob, so presets can be
saved to flash and later exposed as a GATT Config characteristic too
(same "one struct, two transports" principle as before).

### Extend ConfigBlob_t (in proto_structs.h)

ConfigBlob_t is 64 bytes with `reserved4[14]` padding before crc32.
Consume bytes from there — do NOT change the struct size or move
existing fields (breaks compatibility with everything already built).

```c
    /* was: uint8_t reserved4[14]; */
    uint8_t  ble_adv_interval_100ms; /* advertising interval,
                                        units of 100ms, e.g. 10=1000ms */
    uint8_t  ble_tx_power;           /* 0..7 index into a fixed
                                        power table (define below) */
    uint16_t ble_window_min;         /* how many minutes beacon stays
                                        awake/advertising per cycle */
    uint16_t ble_sleep_min;          /* how many minutes beacon sleeps
                                        (no advertising) between windows.
                                        0 = always advertise (phase 1
                                        behavior) */
    char     ble_name_suffix[4];     /* optional 4-char custom suffix,
                                        empty = use UID-based name */
    uint8_t  reserved4[6];           /* still reserved, shrunk from 14 */
```

Recompute `_Static_assert(sizeof(ConfigBlob_t) == 64, ...)` still
holds — verify field byte offsets sum correctly, adjust reserved4
size as needed to keep exactly 64 bytes total.

TX power table (index → dBm, typical STM32WB BLE range):
```c
static const int8_t BLE_TX_POWER_DBM[8] = {
    -20, -15, -10, -5, 0, 3, 5, 6
};
```

### New behavior: BLE duty-cycle sleep

When `ble_sleep_min > 0`:
```
Advertise for ble_window_min minutes
  → stop advertising, allow CPU2 shutdown + Stop2 (constraint #1
    now applies — BLE_IsIdle() becomes true)
  → sleep ble_sleep_min minutes (RTC wakeup, same mechanism as
    existing TX schedule sleep)
  → wake, resume advertising for ble_window_min
  → repeat
```
This is independent from the TX schedule (sched_hours/days/months) —
BLE duty-cycle runs continuously regardless of TX schedule state,
unless the user wants them coupled (not for this task — keep BLE
duty-cycle and TX schedule as two independent timers for now).

UART text command extensions (`svc_uart_cmd.c`, human protocol):
```
ble show                    → print current BLE config
ble adv <ms>                → set advertising interval (100-10000)
ble power <0-7>              → set TX power index
ble window <min>             → set advertising window duration
ble sleep <min>              → set sleep duration (0=always on)
ble name <suffix>            → set 4-char name suffix
```

Binary protocol: these fields are part of CFG!/CFG? like everything
else — no separate binary verb needed, ConfigBlob already covers it.

### Testing protocol for Phase 2

```
1. `ble show` → see defaults (adv=1000ms window=always-on)
2. `ble sleep 5` `ble window 1` `save` → power cycle
3. Watch UART: advertises 1 min, "[BLE] duty sleep 5 min", µA meter
   should show Stop2-level current during the sleep phase
4. nRF Connect: confirm you can connect DURING the window, cannot
   find the device during the sleep phase
5. `#id CFG!` with modified ble_* fields via the PC GUI (once GUI
   exposes these fields) → verify same behavior as text commands
6. `ble power 0` (lowest, -20dBm) → verify with nRF Connect RSSI
   noticeably weaker than `ble power 7`
```

## Do not

- Do not touch BLE stack binary (page 68+) unless bring-up fails
- Do not remove or reorder existing ConfigBlob_t fields
- Do not force CPU2 shutdown while BLE is advertising/connected
- Do not implement Phase 2 before Phase 1 is verified on hardware
