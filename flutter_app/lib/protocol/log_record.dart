import 'dart:typed_data';
import 'opcodes.dart';

/// LogRecord v1 — 16 bytes.
/// Format: <IBBhHBBB3s>
class LogRecord {
  final int timestamp;
  final int mask;
  final int recFlags;
  final int temp01c;
  final int lightRaw;
  final int batteryPct;
  final int batMvHi;
  final int batMvLo;

  const LogRecord({
    this.timestamp   = 0,
    this.mask        = 0,
    this.recFlags    = 0,
    this.temp01c     = 0,
    this.lightRaw    = 0,
    this.batteryPct  = 0,
    this.batMvHi     = 0,
    this.batMvLo     = 0,
  });

  static LogRecord fromBytes(Uint8List data) {
    if (data.length < 16) return const LogRecord();
    final bd = ByteData.sublistView(data, 0, 16);
    return LogRecord(
      timestamp:   bd.getUint32(0, Endian.little),
      mask:        bd.getUint8(4),
      recFlags:    bd.getUint8(5),
      temp01c:     bd.getInt16(6, Endian.little),
      lightRaw:    bd.getUint16(8, Endian.little),
      batteryPct:  bd.getUint8(10),
      batMvHi:     bd.getUint8(11),
      batMvLo:     bd.getUint8(12),
      // bytes 13..15 = pad[3], ignored
    );
  }

  int  get batMv     => (batMvHi << 8) | batMvLo;
  bool get hasTemp   => (mask & logMaskTemp)    != 0;
  bool get hasLight  => (mask & logMaskLight)   != 0;
  bool get hasBatt   => (mask & logMaskBattPct) != 0 || (mask & logMaskBattMv) != 0;
  bool get isEmpty   => recFlags == recFlagEmpty;

  /// Convert to the same dict-style map used for charting
  Map<String, dynamic> toMap() => {
    'ts':        timestamp,
    'temp_c':    hasTemp  ? temp01c / 10.0 : null,
    'light_raw': hasLight ? lightRaw       : null,
    'bat_pct':   hasBatt  ? batteryPct     : null,
    'bat_mv':    hasBatt  ? batMv          : null,
  };
}

/// LogRecord v2 — 16 bytes, typed.
/// Layout: timestamp(u32) type(u8) flags(u8) payload[10]
class LogRecordV2 {
  final int   timestamp;
  final int   type;
  final int   flags;
  final Uint8List payload;

  const LogRecordV2({
    this.timestamp = 0,
    this.type      = logrecTypeEmpty,
    this.flags     = 0,
    required this.payload,
  });

  static LogRecordV2 fromBytes(Uint8List data) {
    if (data.length < 16) {
      return LogRecordV2(timestamp: 0, type: logrecTypeEmpty, flags: 0,
          payload: Uint8List(10));
    }
    final bd      = ByteData.sublistView(data, 0, 16);
    final payload = Uint8List.fromList(data.sublist(6, 16));
    return LogRecordV2(
      timestamp: bd.getUint32(0, Endian.little),
      type:      data[4],
      flags:     data[5],
      payload:   payload,
    );
  }

  bool get isEmpty => type == logrecTypeEmpty;

  /// Returns {ts, temp_c} or null
  double? get tempC {
    if (type != logrecTypeTemp || payload.length < 2) return null;
    final bd = ByteData.sublistView(payload);
    return bd.getInt16(0, Endian.little) / 10.0;
  }

  /// Returns {bat_mv, bat_pct} or null
  ({int mv, int pct})? get battData {
    if (type != logrecTypeBatt || payload.length < 3) return null;
    final bd = ByteData.sublistView(payload);
    return (mv: bd.getUint16(0, Endian.little), pct: payload[2]);
  }

  /// Returns light_raw or null
  int? get lightRaw {
    if (type != logrecTypeLight || payload.length < 2) return null;
    return ByteData.sublistView(payload).getUint16(0, Endian.little);
  }

  /// Returns (x, y, z) in raw i16 or null
  ({int x, int y, int z})? get accelXyz {
    if (type != logrecTypeAccel || payload.length < 6) return null;
    final bd = ByteData.sublistView(payload);
    return (
      x: bd.getInt16(0, Endian.little),
      y: bd.getInt16(2, Endian.little),
      z: bd.getInt16(4, Endian.little),
    );
  }

  /// Convert to the same map format as LogRecord.toMap()
  Map<String, dynamic> toMap() {
    final m = <String, dynamic>{'ts': timestamp};
    switch (type) {
      case logrecTypeTemp:
        final t = tempC; if (t != null) m['temp_c'] = t;
      case logrecTypeBatt:
        final b = battData;
        if (b != null) { m['bat_mv'] = b.mv; m['bat_pct'] = b.pct; }
      case logrecTypeLight:
        final l = lightRaw; if (l != null) m['light_raw'] = l;
      case logrecTypeAccel:
        final a = accelXyz;
        if (a != null) { m['accel_x'] = a.x; m['accel_y'] = a.y; m['accel_z'] = a.z; }
    }
    return m;
  }
}

/// Dispatch raw 16-byte record to v1 or v2 based on fmt_ver from OP_LOG_INFO.
Map<String, dynamic>? parseLogRecord(Uint8List raw16, int fmtVer) {
  if (raw16.length < 16) return null;
  if (fmtVer >= 2) {
    final r = LogRecordV2.fromBytes(raw16);
    if (r.isEmpty) return null;
    return r.toMap();
  } else {
    final r = LogRecord.fromBytes(raw16);
    if (r.isEmpty) return null;
    return r.toMap();
  }
}
