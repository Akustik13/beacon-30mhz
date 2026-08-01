# Task: Fix bugs in tx_beacon_gui.py (v1)

## Context

File: C:\Users\prymv\STM32CubeIDE\workspace_1.19.0\Beacon_30MHz\pc_app\tx_beacon_gui.py
Tkinter GUI for TX Beacon 30MHz, communicates via UART 115200.
C:\Users\prymv\STM32CubeIDE\workspace_1.19.0\Beacon_30MHz\TX_Beacon\tools\tx_beacon_gui.py 

**Make a backup copy first: tx_beacon_gui_backup.py**

Fix the bugs below one by one. After each fix, verify the file still
runs: `python tx_beacon_gui.py` must open the window without errors.

---

## BUG 1 (CRITICAL): Command storm — commands sent without waiting for OK

### Problem
`_do_save()` sends ~18 commands in a burst, `_apply_profile_to_gui()`
sends ~9. The MCU has a 2-slot UART command buffer — commands can be
silently dropped. Then `save` stores incomplete state, and `reset`
right after `save` may interrupt the Flash write.

User requirement: every command gets confirmed with OK from MCU.
If some parameter was NOT applied — user must see it in the GUI,
not only in the terminal.

### Fix: Command queue with OK tracking + verification report

Implement a sequential command queue:

```python
class CmdQueue:
    """
    Sends commands one at a time. Waits for OK/ERR (or timeout)
    before sending the next. Reports results when queue is done.
    """
    def __init__(self, gui):
        self._gui = gui
        self._queue = []          # list of (cmd_str, description)
        self._current = None      # (cmd_str, description) in flight
        self._results = []        # list of (cmd, desc, status)
                                  # status: 'OK' | 'ERR:<msg>' | 'TIMEOUT'
        self._timeout_id = None
        self._on_done = None      # callback(results)
        self.TIMEOUT_MS = 1500

    def run(self, commands, on_done=None):
        """
        commands: list of (cmd_str, human_description)
        on_done: callback(results) called after last command
        """
        self._queue = list(commands)
        self._results = []
        self._on_done = on_done
        self._send_next()

    def _send_next(self):
        if self._timeout_id:
            self._gui.after_cancel(self._timeout_id)
            self._timeout_id = None
        if not self._queue:
            self._current = None
            if self._on_done:
                self._on_done(self._results)
            return
        self._current = self._queue.pop(0)
        cmd, desc = self._current
        self._gui._cmd_raw(cmd)   # send without popping messagebox
        self._timeout_id = self._gui.after(
            self.TIMEOUT_MS, self._on_timeout)

    def on_line(self, line: str):
        """Call from _dispatch for every received line."""
        if not self._current:
            return
        if line.strip() == 'OK' or line.startswith('OK '):
            cmd, desc = self._current
            self._results.append((cmd, desc, 'OK'))
            self._send_next()
        elif line.startswith('ERR'):
            cmd, desc = self._current
            self._results.append((cmd, desc, line.strip()))
            self._send_next()

    def _on_timeout(self):
        if self._current:
            cmd, desc = self._current
            self._results.append((cmd, desc, 'TIMEOUT'))
            self._send_next()

    @property
    def busy(self):
        return self._current is not None
```

Add `_cmd_raw()` next to existing `_cmd()`:
```python
def _cmd_raw(self, text: str):
    """Send without connection warning popup (used by CmdQueue)."""
    if self.serial and self.serial.is_open:
        self._log(f">>> {text}\n", "tx")
        self.serial.write((text + "\r\n").encode())
```

Wire into `_dispatch()`: first line of dispatch:
```python
self._cmd_queue.on_line(s)
```

### Rewrite _do_save() using the queue:

```python
def _do_save(self):
    if not messagebox.askyesno("Save & Restart", ...):
        return
    cmds = [
        (f"mode {self._mode_var.get()}",        "TX mode"),
        (f"ch {self._ch_var.get()}",            "Channel"),
        (f"pwr {self._pwr_var.get()}",          "Power"),
        (f"pulse {self._pulse_var.get()}",      "Pulse ms"),
        (f"period {self._period_var.get()}",    "Period ms"),
        (f"led {self._led_mode_var.get()}",     "LED mode"),
        (f"temp mode {self._temp_mode_var.get()}",   "Temp mode"),
        (f"temp period {self._temp_period_var.get()}", "Temp period"),
        (f"temp offset {float(self._temp_offset_var.get()):.1f}", "Temp offset"),
        (f"batt mode {self._batt_mode_var.get()}",   "Batt mode"),
        (f"batt period {self._batt_period_var.get()}", "Batt period"),
        (f"batt scale {float(self._batt_scale_var.get()):.1f}", "Batt scale"),
        (f"light mode {self._light_mode_var.get()}", "Light mode"),
        (f"light period {self._light_period_var.get()}", "Light period"),
        (f"rtc live {'on' if self._rtc_live_var.get() else 'off'}", "RTC live"),
    ]
    # Schedule commands
    cmds += self._schedule_cmds()   # see BUG 7 fix
    cmds.append(("save", "Save to Flash"))

    def on_done(results):
        failed = [(d, st) for c, d, st in results if st != 'OK']
        self._show_apply_report(results)
        if not failed:
            # only reset if EVERYTHING succeeded
            self._cmd_queue.run([("reset", "MCU reset")])
    self._cmd_queue.run(cmds, on_done)
```

### Apply report dialog (GUI, not just terminal):

```python
def _show_apply_report(self, results):
    """
    Show verification result window.
    Green: all OK. Red rows: failed commands.
    """
    failed = [(c, d, st) for c, d, st in results if st != 'OK']
    win = tk.Toplevel(self)
    win.title("Apply Report")
    win.configure(bg=BG)
    win.geometry("420x360")

    if not failed:
        hdr = tk.Label(win, text="✓ All settings applied successfully",
                       bg=BG, fg="#166534",
                       font=("Segoe UI", 12, "bold"))
    else:
        hdr = tk.Label(win,
                       text=f"⚠ {len(failed)} of {len(results)} settings FAILED",
                       bg=BG, fg="#991b1b",
                       font=("Segoe UI", 12, "bold"))
    hdr.pack(pady=(12, 6))

    frame = tk.Frame(win, bg=BG2, relief="groove", borderwidth=1)
    frame.pack(fill="both", expand=True, padx=10, pady=6)
    canvas = tk.Canvas(frame, bg=BG2, highlightthickness=0)
    sb = ttk.Scrollbar(frame, orient="vertical", command=canvas.yview)
    inner = tk.Frame(canvas, bg=BG2)
    inner.bind("<Configure>",
               lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
    canvas.create_window((0, 0), window=inner, anchor="nw")
    canvas.configure(yscrollcommand=sb.set)
    canvas.pack(side="left", fill="both", expand=True)
    sb.pack(side="right", fill="y")

    for cmd, desc, status in results:
        ok = (status == 'OK')
        row = tk.Frame(inner, bg=BG2)
        row.pack(fill="x", padx=6, pady=1)
        tk.Label(row, text="✓" if ok else "✗",
                 fg="#22c55e" if ok else "#ef4444",
                 bg=BG2, font=("Segoe UI", 10, "bold"), width=2
                 ).pack(side="left")
        tk.Label(row, text=desc, bg=BG2, fg=FG,
                 font=("Segoe UI", 9), anchor="w", width=18
                 ).pack(side="left")
        tk.Label(row, text=status if not ok else "",
                 bg=BG2, fg="#ef4444",
                 font=("Consolas", 8), anchor="w"
                 ).pack(side="left")

    ttk.Button(win, text="Close", command=win.destroy).pack(pady=8)
```

### Rewrite _apply_profile_to_gui() the same way:
1. First: set all GUI variables (no commands)
2. Then: build cmds list and run through queue
3. On done: _show_apply_report(results)

---

## BUG 2: Race condition in _rx_worker

### Problem
`_disconnect()` sets `self.serial = None` while `_rx_worker` may be
between `read()` calls → AttributeError, thread dies silently.

### Fix
```python
def _rx_worker(self):
    ser = self.serial          # local reference
    buf = b""
    while not self._stop_rx.is_set():
        try:
            data = ser.read(256)
        except (serial.SerialException, OSError, AttributeError):
            break
        if data:
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.rstrip(b"\r").decode("utf-8", errors="replace")
                if text:
                    self.rx_queue.put(text)
```
Note: also fixes BUG 5 (utf-8 instead of latin-1).

In `_disconnect()`: set `self._stop_rx.set()` FIRST, then
`time.sleep(0.1)` (or join with timeout), then close port.

---

## BUG 3: _calc_import() imports wrong fields

### Problem
```python
self._calc_temp_iv.set(self._log_dt_var.get())   # dt = delta threshold!
```
Imports delta thresholds instead of log intervals.

### Fix
The log intervals are in `self._log_dt_var` — WAIT. Check the code
carefully: find which variables hold the per-sensor LOG INTERVALS
(seconds between writes: `log temp <s>`, `log light <s>`, `log bat <s>`)
vs the delta thresholds (`log dt/dl/db`).
Looking at _log_write_verify():
```python
self._cmd(f"log temp {self._log_dt_var.get()}")   # ← dt IS the interval!
```
So in THIS codebase `_log_dt_var` = temp interval (confusing name).
Verify by reading _build_flash_log_panel() and the [LOG] parse:
```python
m = re.search(r"\[LOG\]\s+temp=(\d+)s\s+light=(\d+)s\s+bat=(\d+)s", s)
self._log_dt_var.set(m.group(1))   # temp INTERVAL in seconds
```
CONCLUSION: `_calc_import()` is actually CORRECT — the variable names
are just misleading (dt = "delta time"?). 

ACTION: rename for clarity:
`_log_dt_var → _log_iv_temp_var`
`_log_dl_var → _log_iv_light_var`
`_log_db_var → _log_iv_bat_var`
Update ALL usages consistently. This prevents future bugs.

---

## BUG 4: Verify without timeout

### Problem
```python
self._verify_active = True
self.after(900, lambda: self._cmd("log get"))
```
If MCU doesn't answer, `_verify_active` stays True forever. A later
unrelated [LOG] line triggers `_log_flash_result` with a stale snapshot.

### Fix
```python
self._verify_active = True
self._verify_deadline_id = self.after(3000, self._verify_timeout)
self.after(900, lambda: self._cmd("log get"))

def _verify_timeout(self):
    if self._verify_active:
        self._verify_active = False
        messagebox.showwarning(
            "Write & Verify",
            "MCU did not confirm settings within 3 s.\n"
            "Check connection and try again.")
```
And when verify completes normally, cancel the deadline:
```python
self.after_cancel(self._verify_deadline_id)
```

---

## BUG 5: latin-1 decoding
Fixed in BUG 2 (use utf-8 with errors="replace").
Reason: `hwdesc comment` may contain UTF-8 text (e.g. Cyrillic) —
latin-1 turns it into mojibake.

---

## BUG 6: Hardcoded 768 in _parse_log_line

### Problem
```python
self._log_sv_used.set(f"{used}/768 ({pct}%)")
...
self._log_sv_free.set("768")   # after clear
```
If firmware log area size changes, GUI shows wrong totals.

### Fix
Add `self._log_total = 768` (default) as instance variable.
Update it whenever a line with total is parsed:
```python
m = re.search(r"\[LOG\]\s+hdr=pg\d+.*?(\d+)/(\d+)\s+entries", s)
if m:
    used, total = int(m.group(1)), int(m.group(2))
    self._log_total = total          # ← remember
```
Replace every literal 768 in display strings and in "cleared OK"
handler with `self._log_total`. Also update calculator default
total from this value on import.

---

## BUG 7: Schedule checkbox → instant command per click

### Problem
`_on_hour_change()` fires a UART command on EVERY checkbox click.
Clicking 10 hours = 10 commands (and 10 chances to hit the 2-slot
buffer limit).

### Fix
Debounce: collect changes, send once after 500 ms of silence:
```python
def _on_hour_change(self):
    if getattr(self, "_sched_debounce_id", None):
        self.after_cancel(self._sched_debounce_id)
    self._sched_debounce_id = self.after(500, self._send_sched_hours)

def _send_sched_hours(self):
    self._sched_debounce_id = None
    hours = [h for h in range(24) if self._hour_vars[h].get()]
    if hours: self._cmd("sched hours " + " ".join(map(str, hours)))
    else:     self._cmd("sched hours")
```
Same for days and months.

Also add helper `_schedule_cmds()` returning list of
(cmd, desc) tuples for use in the _do_save() queue:
```python
def _schedule_cmds(self):
    if not self._sched_enabled.get():
        return [("sched off", "Schedule off")]
    out = []
    hours = [h for h in range(24) if self._hour_vars[h].get()]
    days  = [d+1 for d in range(7) if self._day_vars[d].get()]
    months= [m+1 for m in range(12) if self._month_vars[m].get()]
    out.append(("sched hours " + " ".join(map(str, hours))
                if hours else "sched hours", "Sched hours"))
    out.append(("sched days " + " ".join(map(str, days))
                if days else "sched days", "Sched days"))
    out.append(("sched months " + " ".join(map(str, months))
                if months else "sched months", "Sched months"))
    return out
```

---

## BUG 8: Profile apply pops error dialogs when disconnected

### Problem
`_apply_profile_to_gui()` calls `_cmd()` 9 times → if not connected,
9 messagebox warnings in a row.

### Fix
Split into two functions:
```python
def _profile_to_gui(self, p):
    """Set GUI variables only. No UART."""
    ...

def _profile_to_beacon(self, p):
    """Send via CmdQueue. Only if connected."""
    if not (self.serial and self.serial.is_open):
        self._log("[PROFILE] not connected — GUI updated only\n", "sys")
        return
    cmds = [...]
    self._cmd_queue.run(cmds, self._show_apply_report)

def _load_selected_profile(self):
    p = ...
    self._profile_to_gui(p)
    self._profile_to_beacon(p)
```

---

## BUG 9: Silent settings/profiles load failure

### Fix
```python
def _load_settings(self):
    try:
        if os.path.exists(SETTINGS_FILE):
            with open(SETTINGS_FILE, "r", encoding="utf-8") as f:
                return json.load(f)
    except Exception as e:
        print(f"[WARN] settings file corrupt: {e}")  # console
        # after GUI is up, also show in terminal log
    return {}
```
Same for profiles. If the GUI log widget exists, write a line there.

---

## Calculator improvements (from analysis)

### 10a. Reverse calc: efficiency only for onchange/adaptive
```python
eff_used = eff if mode != "always" else 1.0
sugg_s = (3600.0 * eff_used) / budget_rph
```

### 10b. Adaptive: checkpoint floor
```python
if mode == "adaptive":
    ckp_n = max(1, int(self._log_ckp_var.get() or 16))
    rph = max(rph_raw * eff, rph_raw / ckp_n)
```

### 10c. CIRCULAR mode: show history depth
When overflow mode is circular, add one more result label:
```python
if self._log_oflow_var.get() == "circular":
    hist_h = total / rph if rph > 0 else float('inf')
    self._calc_hist_var.set(f"History depth: {_fmt_time(hist_h)}")
else:
    self._calc_hist_var.set("")
```
Add the label to the calc panel UI (row below "Full in").

### 10d. Warn on non-multiple intervals
```python
if len(sensors) > 1:
    base = min(sensors)
    if any(iv % base != 0 for iv in sensors):
        self._calc_warn_var.set(
            "⚠ intervals not multiples of fastest — actual rate higher")
    else:
        self._calc_warn_var.set("")
```

---

## Testing checklist after all fixes

1. `python tx_beacon_gui.py` opens without console errors
2. Connect → disconnect → connect again: no thread errors in console
3. "Save to flash": commands go one-by-one in terminal
   (visible sequential >>> cmd / <<< OK pairs)
4. Apply Report window appears: all green when MCU connected
5. Unplug UART mid-save → report shows TIMEOUT rows in red
6. Profile apply when disconnected: single log line, no popup storm
7. Click 5 hour checkboxes fast → only ONE "sched hours ..." sent
   (after 500ms pause)
8. Calculator: import from config → intervals match log settings
9. Calculator: circular mode shows "History depth" line
10. log clear → used shows "0/<actual total>", not hardcoded 768
