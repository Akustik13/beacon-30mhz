import 'dart:typed_data';

// ── Condition types ───────────────────────────────────────────────────────────
const int condDisabled      = 0x00;
const int condBattBelow     = 0x01;
const int condBattAbove     = 0x02;
const int condTempAbove     = 0x03;
const int condTempBelow     = 0x04;
const int condNoMotion      = 0x05;  // val1 = N cycles
const int condMotion        = 0x06;
const int condLightBelow    = 0x07;  // val1 = % 0-100
const int condLightAbove    = 0x08;
const int condEveryNcycles  = 0x09;  // val1 = N
const int condAlways        = 0x0A;
const int condBeforeBle     = 0x0B;
const int condEveryNhrs     = 0x0C;  // val1=total_min (h×60+min), val2=extra_sec

// ── Action types ──────────────────────────────────────────────────────────────
const int actNone       = 0x00;
const int actSetPower   = 0x01;  // p1=0/1/2
const int actTxPulses   = 0x02;  // p1=count p2=gap_ms
const int actTxPattern  = 0x03;  // p1=on_ms p2=off_ms
const int actBleStart   = 0x04;
const int actSetChannel = 0x05;  // p1=1/2/3
const int actSetPeriod  = 0x06;  // p1=period_s
const int actLogMarker  = 0x07;  // p1=tag
const int actLedOn      = 0x08;
const int actLedOff     = 0x09;
const int actLedBlink   = 0x0A;  // p1=count p2=period_ms

// ── Event flags ───────────────────────────────────────────────────────────────
const int evFlagEnabled = 0x01;
const int evFlagOneShot = 0x02;

// ── Wire format constants ─────────────────────────────────────────────────────
const int maxEvents  = 4;
const int maxConds   = 3;
const int condSize   = 5;   // cond_type(1) + val1(i16le) + val2(i16le)
const int eventSize  = 28;  // firmware: 28 bytes per event
const int eventBlob  = maxEvents * eventSize;  // 112 bytes

// ── Human labels ──────────────────────────────────────────────────────────────
const Map<int, String> condLabel = {
  condDisabled:     'Disabled',
  condBattBelow:    'Battery below %',
  condBattAbove:    'Battery above %',
  condTempAbove:    'Temp above °C',
  condTempBelow:    'Temp below °C',
  condNoMotion:     'No motion N cycles',
  condMotion:       'Motion detected',
  condLightBelow:   'Light below %',
  condLightAbove:   'Light above %',
  condEveryNcycles: 'Every N TX cycles',
  condAlways:       'Always (every cycle)',
  condBeforeBle:    'Before BLE start',
  condEveryNhrs:    'Every H h M m S s',
};

const Map<int, String> actLabel = {
  actNone:       'No action',
  actSetPower:   'Set TX power',
  actTxPulses:   'Send extra pulses',
  actTxPattern:  'TX on/off pattern',
  actBleStart:   'Start BLE advertising',
  actSetChannel: 'Set channel',
  actSetPeriod:  'Set TX period',
  actLogMarker:  'Write log marker',
  actLedOn:      'LED on',
  actLedOff:     'LED off',
  actLedBlink:   'LED blink',
};

// Conditions with no numeric val1 input needed
const Set<int> condNoVal = {condAlways, condBeforeBle, condMotion};

// ── Single condition ──────────────────────────────────────────────────────────
class EvCond {
  int type;   // condXxx
  int val1;   // signed 16-bit
  int val2;   // signed 16-bit

  EvCond({this.type = condDisabled, this.val1 = 0, this.val2 = 0});

  Uint8List toBytes() {
    final b = ByteData(condSize);
    b.setUint8(0,  type & 0xFF);
    b.setInt16(1,  val1.clamp(-32768, 32767), Endian.little);
    b.setInt16(3,  val2.clamp(-32768, 32767), Endian.little);
    return b.buffer.asUint8List();
  }

  static EvCond fromBytes(Uint8List data, int offset) {
    final b = ByteData.sublistView(data, offset, offset + condSize);
    return EvCond(
      type: b.getUint8(0),
      val1: b.getInt16(1, Endian.little),
      val2: b.getInt16(3, Endian.little),
    );
  }

  EvCond copy() => EvCond(type: type, val1: val1, val2: val2);

  bool get isEmpty => type == condDisabled;

  String get summary {
    switch (type) {
      case condDisabled:     return '—';
      case condAlways:       return 'Always';
      case condBeforeBle:    return 'Before BLE';
      case condMotion:       return 'Motion';
      case condBattBelow:    return 'Batt < $val1%';
      case condBattAbove:    return 'Batt > $val1%';
      case condTempAbove:    return 'Temp > $val1°C';
      case condTempBelow:    return 'Temp < $val1°C';
      case condNoMotion:     return 'No motion $val1 cycles';
      case condLightBelow:   return 'Light < $val1%';
      case condLightAbove:   return 'Light > $val1%';
      case condEveryNcycles: return 'Every $val1 cycles';
      case condEveryNhrs:    return _timeSummary();
      default:               return '?';
    }
  }

  String _timeSummary() {
    final h = val1 ~/ 60;
    final m = val1 % 60;
    final s = val2;
    final parts = <String>[];
    if (h > 0) parts.add('${h}h');
    if (m > 0) parts.add('${m}m');
    if (s > 0) parts.add('${s}s');
    return 'Every ${parts.isEmpty ? '0s' : parts.join(' ')}';
  }
}

// ── Full event (28 bytes) ─────────────────────────────────────────────────────
class BeaconEvent {
  bool        enabled;
  bool        oneShot;
  List<EvCond> conds;   // 1..maxConds active conditions (AND logic)
  int          actType;
  int          actP1;
  int          actP2;
  int          cooldown; // TX cycles skip after fire

  BeaconEvent({
    this.enabled   = false,
    this.oneShot   = false,
    List<EvCond>? conds,
    this.actType   = actNone,
    this.actP1     = 0,
    this.actP2     = 0,
    this.cooldown  = 0,
  }) : conds = conds ?? [EvCond()];

  // ── Serialise to 28 bytes ─────────────────────────────────────────────────
  Uint8List toBytes() {
    final b = Uint8List(eventSize);
    final bv = ByteData.sublistView(b);

    int flags = 0;
    if (enabled) flags |= evFlagEnabled;
    if (oneShot) flags |= evFlagOneShot;
    bv.setUint8(0, flags);

    final n = conds.length.clamp(0, maxConds);
    bv.setUint8(1, n);

    for (int i = 0; i < maxConds; i++) {
      final cBytes = i < n ? conds[i].toBytes() : Uint8List(condSize);
      b.setRange(2 + i * condSize, 2 + (i + 1) * condSize, cBytes);
    }

    bv.setUint8(17, actType & 0xFF);
    bv.setInt16(18, actP1.clamp(-32768, 32767), Endian.little);
    bv.setInt16(20, actP2.clamp(-32768, 32767), Endian.little);
    bv.setUint8(22, cooldown & 0xFF);
    // bytes 23-27 = pad (0, already zero from Uint8List)
    return b;
  }

  static BeaconEvent fromBytes(Uint8List data, [int offset = 0]) {
    final bv = ByteData.sublistView(data, offset, offset + eventSize);
    final flags   = bv.getUint8(0);
    final nConds  = bv.getUint8(1).clamp(0, maxConds);
    final condList = <EvCond>[];
    for (int i = 0; i < nConds; i++) {
      condList.add(EvCond.fromBytes(data, offset + 2 + i * condSize));
    }
    if (condList.isEmpty) condList.add(EvCond());
    return BeaconEvent(
      enabled:  (flags & evFlagEnabled) != 0,
      oneShot:  (flags & evFlagOneShot)  != 0,
      conds:    condList,
      actType:  bv.getUint8(17),
      actP1:    bv.getInt16(18, Endian.little),
      actP2:    bv.getInt16(20, Endian.little),
      cooldown: bv.getUint8(22),
    );
  }

  BeaconEvent copy() => BeaconEvent(
    enabled:  enabled,
    oneShot:  oneShot,
    conds:    conds.map((c) => c.copy()).toList(),
    actType:  actType,
    actP1:    actP1,
    actP2:    actP2,
    cooldown: cooldown,
  );

  // ── Human summaries ───────────────────────────────────────────────────────
  String get condSummary {
    final active = conds.where((c) => c.type != condDisabled).toList();
    if (active.isEmpty) return '(no condition)';
    return active.map((c) => c.summary).join(' AND ');
  }

  String get actSummary {
    switch (actType) {
      case actNone:       return 'No action';
      case actSetPower:   return 'Power → ${_pwrLabel(actP1)}';
      case actTxPulses:   return '$actP1 pulse${actP1 != 1 ? "s" : ""}, gap ${actP2}ms';
      case actTxPattern:  return 'TX on ${actP1}ms / off ${actP2}ms';
      case actBleStart:   return 'Start BLE';
      case actSetChannel: return 'Channel → $actP1';
      case actSetPeriod:  return 'Period → ${actP1}s';
      case actLogMarker:  return 'Log marker #$actP1';
      case actLedOn:      return 'LED on';
      case actLedOff:     return 'LED off';
      case actLedBlink:   return 'LED blink ×$actP1 ${actP2 > 0 ? "${actP2}ms" : "200ms"}';
      default:            return actLabel[actType] ?? '?';
    }
  }

  static String _pwrLabel(int v) => ['Low(0)', 'Mid(1)', 'High(2)'].elementAtOrNull(v) ?? v.toString();

  bool get isEmpty => !enabled && actType == actNone &&
      conds.every((c) => c.type == condDisabled);
}

// ── Wire blob helpers ─────────────────────────────────────────────────────────
Uint8List eventsToBlob(List<BeaconEvent> events) {
  final out = Uint8List(eventBlob);
  for (int i = 0; i < maxEvents; i++) {
    final ev = i < events.length ? events[i] : BeaconEvent();
    out.setRange(i * eventSize, (i + 1) * eventSize, ev.toBytes());
  }
  return out;
}

List<BeaconEvent> eventsFromBlob(Uint8List data) {
  final list = <BeaconEvent>[];
  for (int i = 0; i < maxEvents; i++) {
    final off = i * eventSize;
    if (off + eventSize > data.length) break;
    list.add(BeaconEvent.fromBytes(data, off));
  }
  while (list.length < maxEvents) list.add(BeaconEvent());
  return list;
}
