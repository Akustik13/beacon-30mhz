import 'dart:typed_data';

// ── Condition types ────────────────────────────────────────────────────────────
const int condDisabled       = 0x00;
const int condBatteryBelow   = 0x01;
const int condBatteryAbove   = 0x02;
const int condTempAbove      = 0x03;
const int condTempBelow      = 0x04;
const int condNoMotion       = 0x05;
const int condMotion         = 0x06;
const int condLightBelow     = 0x07;
const int condLightAbove     = 0x08;
const int condEveryNcycles   = 0x09;
const int condAlways         = 0x0A;
const int condBeforeBle      = 0x0B;
const int condEveryNhours    = 0x0C;

// ── Action types ───────────────────────────────────────────────────────────────
const int actNone       = 0x00;
const int actSetPower   = 0x01;
const int actTxPulses   = 0x02;
const int actTxPattern  = 0x03;
const int actBleStart   = 0x04;
const int actSetChannel = 0x05;
const int actSetPeriod  = 0x06;
const int actLogMarker  = 0x07;

// ── Flags ─────────────────────────────────────────────────────────────────────
const int evFlagOneShot    = 0x01;
const int evFlagInvertCond = 0x02;

const int maxEvents = 7;
const int eventSize = 12; // bytes per event in wire format

// ── Human labels ──────────────────────────────────────────────────────────────
const Map<int, String> condLabel = {
  condDisabled:     'Disabled',
  condBatteryBelow: 'Battery below',
  condBatteryAbove: 'Battery above',
  condTempAbove:    'Temp above',
  condTempBelow:    'Temp below',
  condNoMotion:     'No motion for N cycles',
  condMotion:       'Motion detected',
  condLightBelow:   'Light below',
  condLightAbove:   'Light above',
  condEveryNcycles: 'Every N TX cycles',
  condAlways:       'Always (every cycle)',
  condBeforeBle:    'Before BLE start',
  condEveryNhours:  'Every N hours (RTC)',
};

const Map<int, String> actLabel = {
  actNone:       'No action',
  actSetPower:   'Set TX power',
  actTxPulses:   'Send pulses',
  actTxPattern:  'TX pattern (on/off)',
  actBleStart:   'Start BLE advertising',
  actSetChannel: 'Set channel',
  actSetPeriod:  'Set TX period',
  actLogMarker:  'Write log marker',
};

// Conditions that don't need a numeric value
const Set<int> condNoValue = {condAlways, condBeforeBle, condMotion};

// ── Event model ───────────────────────────────────────────────────────────────
class BeaconEvent {
  bool   enabled;
  int    condType;
  int    condVal;   // signed 16-bit
  int    actType;
  int    cooldown;  // TX periods
  int    actParam1; // signed 16-bit
  int    actParam2; // signed 16-bit
  int    flags;     // evFlagOneShot | evFlagInvertCond
  // pad byte (ignored)

  BeaconEvent({
    this.enabled   = false,
    this.condType  = condDisabled,
    this.condVal   = 0,
    this.actType   = actNone,
    this.cooldown  = 0,
    this.actParam1 = 0,
    this.actParam2 = 0,
    this.flags     = 0,
  });

  bool get oneShot    => (flags & evFlagOneShot)    != 0;
  bool get invertCond => (flags & evFlagInvertCond) != 0;
  set oneShot(bool v)    => flags = v ? (flags | evFlagOneShot)    : (flags & ~evFlagOneShot);
  set invertCond(bool v) => flags = v ? (flags | evFlagInvertCond) : (flags & ~evFlagInvertCond);

  // ── Serialise / deserialise (12 bytes) ───────────────────────────────────────
  Uint8List toBytes() {
    final b = ByteData(eventSize);
    b.setUint8(0,  enabled ? 1 : 0);
    b.setUint8(1,  condType & 0xFF);
    b.setInt16(2,  condVal,   Endian.little);
    b.setUint8(4,  actType & 0xFF);
    b.setUint8(5,  cooldown & 0xFF);
    b.setInt16(6,  actParam1, Endian.little);
    b.setInt16(8,  actParam2, Endian.little);
    b.setUint8(10, flags & 0xFF);
    b.setUint8(11, 0); // pad
    return b.buffer.asUint8List();
  }

  static BeaconEvent fromBytes(Uint8List data, [int offset = 0]) {
    final b = ByteData.sublistView(data, offset, offset + eventSize);
    return BeaconEvent(
      enabled:   b.getUint8(0) != 0,
      condType:  b.getUint8(1),
      condVal:   b.getInt16(2, Endian.little),
      actType:   b.getUint8(4),
      cooldown:  b.getUint8(5),
      actParam1: b.getInt16(6, Endian.little),
      actParam2: b.getInt16(8, Endian.little),
      flags:     b.getUint8(10),
    );
  }

  BeaconEvent copy() => BeaconEvent(
    enabled:   enabled,
    condType:  condType,
    condVal:   condVal,
    actType:   actType,
    cooldown:  cooldown,
    actParam1: actParam1,
    actParam2: actParam2,
    flags:     flags,
  );

  // ── Human-readable summary ────────────────────────────────────────────────────
  String get condSummary {
    final label = condLabel[condType] ?? '?';
    if (condNoValue.contains(condType)) return label;
    switch (condType) {
      case condBatteryBelow:
      case condBatteryAbove:  return '$label $condVal%';
      case condTempAbove:
      case condTempBelow:     return '$label $condVal°C';
      case condNoMotion:      return '$label $condVal cycles';
      case condLightBelow:
      case condLightAbove:    return '$label $condVal%';
      case condEveryNcycles:  return 'Every $condVal TX cycles';
      case condEveryNhours:   return 'Every ${condVal}h (RTC)';
      default:                return '$label $condVal';
    }
  }

  String get actSummary {
    switch (actType) {
      case actNone:       return 'No action';
      case actSetPower:   return 'Power → ${_powerLabel(actParam1)}';
      case actTxPulses:   return '$actParam1 pulse${actParam1 != 1 ? "s" : ""}, gap ${actParam2}ms';
      case actTxPattern:  return 'TX on ${actParam1}ms / off ${actParam2}ms';
      case actBleStart:   return 'Start BLE';
      case actSetChannel: return 'Channel → $actParam1';
      case actSetPeriod:  return 'Period → ${actParam1}s';
      case actLogMarker:  return 'Log marker #$actParam1';
      default:            return actLabel[actType] ?? '?';
    }
  }

  static String _powerLabel(int v) {
    switch (v) {
      case 0: return '0 (Low)';
      case 1: return '1 (Mid)';
      case 2: return '2 (High)';
      default: return v.toString();
    }
  }

  bool get isEmpty => !enabled && condType == condDisabled && actType == actNone;
}

// ── Wire blob: 7 × 12 = 84 bytes ─────────────────────────────────────────────
Uint8List eventsToBlob(List<BeaconEvent> events) {
  final out = Uint8List(maxEvents * eventSize);
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
