# Task: Fix critical UX regressions in Beacon Manager Flutter app

## Context

Re-read PROMPT_FLUTTER_V3_FINAL.md completely, especially the section
"ADDENDUM: Two home layouts, user-selectable" and the "OFFLINE MODE +
PROFILES (mandatory)" section from the original v2 prompt it was
based on. The current build violates these sections. Screenshots of
the actual running app are attached for reference — study them.

The app currently gates almost everything behind "must be connected",
showing a blocking "Connect to a beacon first" message instead of a
usable offline form. This is the opposite of the spec. Fix root
causes, not just cosmetics.

---

## BUG 1 (CRITICAL): Beacon/Logging tabs must be usable offline

### Current (wrong)
Beacon tab body = single centered text "Connect to a beacon first".
Same for Logging tab. Nothing is editable without a connection.

### Required
These tabs ALWAYS render their full form (mode/channel/power/pulse/
schedule chips on Beacon; sensor cards + calculator on Logging) —
disconnection never hides the form. Only the footer button's label
and action change based on connection state:

```
Disconnected: footer button = "Save as profile…"
              → opens save-profile dialog, writes to local storage
Connected:    footer button = "Apply to beacon"
              → sends config, verifies, shows result banner
```

A profile selector bar stays visible and functional at the top of
the Beacon tab in BOTH states (dropdown + Apply/Save as/Delete),
exactly as specified in the original OFFLINE MODE section. Loading
a profile always works, offline or online — it only fills the form;
sending to hardware is a separate step gated by connection.

If no beacon has ever connected and no profile is loaded, the form
still shows with sensible defaults (mirror `ConfigBlob` defaults),
not a blank/blocked screen.

---

## BUG 2 (CRITICAL): Home tab must never fully block

### Current (wrong)
Home body = Bluetooth-off icon + "Not connected" + single
"Go to Devices" button. All value cards, storage bar, schedule
summary are completely absent when disconnected.

### Required
Home ALWAYS renders the full card layout (temperature, battery,
light, transmitter, storage, schedule summary). When disconnected:
- Every value shows `—` instead of a number
- Cards render at reduced opacity (~0.45) or a muted/greyed style
  so it's visually obvious they're stale/inactive, but the LAYOUT
  and every card stays in place — nothing disappears
- A slim non-blocking banner/chip near the top: "Not connected —
  tap to scan" that navigates to Devices, WITHOUT replacing the
  rest of the screen
- The "Show temperature chart" expand control from the original
  spec stays present (chart area just shows no data / flat line
  when disconnected)

---

## BUG 3 (CRITICAL): Bottom navigation bar layout broken

### Current (wrong)
Icons overlap the Android system gesture bar at the bottom; icon +
label spacing is cramped/inconsistent; the selected-tab pill shape
partially clips its own icon (see "Beacon" tab screenshot — the
radio icon overlaps the pill edge).

### Required
- Wrap the Scaffold body in `SafeArea` (or ensure the
  `bottomNavigationBar` is a real `NavigationBar`/`BottomNavigationBar`
  widget, which handles system inset padding automatically — do NOT
  build the bottom bar as a manually-positioned Row/Stack that
  ignores `MediaQuery.viewPadding.bottom`)
- Verify on a phone with 3-button AND gesture navigation that no
  icon/label is ever obscured or clipped
- Selected item styling: icon must render fully inside its
  indicator pill, not overflow it — check padding/sizing on the
  pill decoration

---

## BUG 4 (CRITICAL): BLE scan is not actually working

### Current (wrong)
Devices tab, "Scan" sub-tab, is completely empty — no button to
start a scan exists anywhere on screen.

### Required
- A clearly visible, always-present "Scan for beacons" button
  (full-width or prominent, matching the original mockup) at the
  top of the Scan view — not hidden in an app bar icon
- Tapping it: request BLE permissions if not granted (show a clear
  system permission dialog, and a fallback UI state if permission
  denied: "Bluetooth permission required" with a button to open
  app settings)
- While scanning: show a progress indicator and live-updating list
  of found devices (beacons matching the expected name/service
  highlighted, others greyed, per original spec)
- Stop scanning after ~10-15s automatically, allow manual re-scan
- TEST THIS on a real device with a real beacon advertising nearby
  before considering this bug closed — a visually-present button
  that silently fails is not a fix

---

## BUG 5: Primary actions hidden as small app-bar icons

### Current (wrong)
Data tab: Download and Share/Export are tiny disabled-looking icons
in the top app bar, easy to miss, unclear when they're actionable.

### Required
Primary actions belong in the screen body as regular buttons, sized
and labeled clearly (as in the original mockups: "Download from
beacon" full-width button, "Export CSV" outlined button, etc).
Reserve the app bar for at most one overflow menu (⋮) with rarely
used actions (About, debug options) — not primary workflow buttons.
Audit ALL screens for this pattern, not just Data.

---

## BUG 6: Missing Accelerometer chart option

Data tab currently shows chips for Temperature / Battery / Light
only. Add the fourth "Accelerometer" chip (X/Y/Z series) as
specified in the original prompt's Data tab section — this was an
explicit priority item ("the tab the user says was incomplete").

---

## BUG 7 (CRITICAL): Settings screen does not exist

### Required
Build the full Settings screen as specified across the original
prompt and its addenda:
- Theme toggle (light/dark) — reachable from app bar overflow menu
  or a dedicated Settings destination
- App layout switcher (Focused control / Fleet dashboard) per the
  "Two home layouts" addendum — with the descriptive subtext for
  each option, persisted, switches instantly without restart
- BLE settings section (op_mode, tx_power, advertising interval,
  window/sleep duration, name suffix) — maps to `BleSettings` /
  `OP_BLE_GET` / `OP_BLE_SET`, editable offline (queued), applied
  when connected (same pattern as Bug 1: form always visible,
  action button adapts to connection state)
- Make Settings reachable from every screen (e.g. persistent gear
  icon in the main app bar, or as a 6th destination if that reads
  more clearly than burying it in overflow — your call, but it
  must NOT be undiscoverable)

---

## General audit checklist before calling this done

For EVERY screen, verify:
1. Disconnected state shows the full UI structure (greyed/disabled
   values), never a blocking "connect first" wall — EXCEPT for
   actions that are inherently impossible offline (e.g. "Download
   from beacon" itself, live sensor notify) which should be
   individually disabled/greyed, not used as an excuse to hide the
   whole screen
2. No primary action is icon-only in an app bar — primary actions
   live in the body as visible buttons
3. Bottom nav renders correctly with system insets on a real device
   or an emulator with gesture navigation enabled
4. Every interactive control that "does nothing" currently either
   gets wired up for real or is temporarily hidden — no dead buttons
5. Re-test the full Devices → Scan → Connect → Home shows live
   data flow end to end on real hardware
