import 'dart:convert';
import 'dart:typed_data';
import 'opcodes.dart';

/// BleSettings — 22 bytes.
/// Format: op(u8) pwr(u8) iv_s(u16) dur_s(u16) adv_ms(u16) nm(u8) name(12s) led(u8)
class BleSettings {
  int    opMode;        // BLE_OP_* bitmask
  int    txPower;       // 0..31; 24 ≈ 0 dBm
  int    intervalS;     // SCHEDULE: seconds between adv windows
  int    durationSec;   // session window seconds
  int    advIntervalMs; // advertising interval ms
  int    nameMode;      // 0=auto "BCN_XXXX", 1=manual
  String name;          // custom name, max 11 chars
  int    ledMode;       // 0=normal, 1=off, 2=triple blink

  BleSettings({
    this.opMode        = bleOpContinuous,
    this.txPower       = 24,
    this.intervalS     = 1800,
    this.durationSec   = 60,
    this.advIntervalMs = 1000,
    this.nameMode      = 0,
    this.name          = '',
    this.ledMode       = 0,
  });

  Uint8List toBytes() {
    final bd = ByteData(22);
    bd.setUint8( 0, opMode        & 0xFF);
    bd.setUint8( 1, txPower       & 0xFF);
    bd.setUint16(2, intervalS     & 0xFFFF, Endian.little);
    bd.setUint16(4, durationSec   & 0xFFFF, Endian.little);
    bd.setUint16(6, advIntervalMs & 0xFFFF, Endian.little);
    bd.setUint8( 8, nameMode      & 0xFF);
    // name: bytes 9..20 (12 bytes, NUL-padded)
    final nameEnc = ascii.encode(name.length > 11 ? name.substring(0, 11) : name);
    final raw = bd.buffer.asUint8List();
    for (int i = 0; i < 12; i++) {
      raw[9 + i] = i < nameEnc.length ? nameEnc[i] : 0;
    }
    raw[21] = ledMode & 0xFF;
    return raw;
  }

  static BleSettings fromBytes(Uint8List data) {
    if (data.length < 21) return BleSettings();
    final bd = ByteData.sublistView(data, 0, data.length);
    final nameBytes = data.sublist(9, 21);
    final nameEnd   = nameBytes.indexOf(0);
    final nameStr   = ascii.decode(
        nameEnd >= 0 ? nameBytes.sublist(0, nameEnd) : nameBytes,
        allowInvalid: true);
    return BleSettings(
      opMode:        bd.getUint8(0),
      txPower:       bd.getUint8(1),
      intervalS:     bd.getUint16(2, Endian.little),
      durationSec:   bd.getUint16(4, Endian.little),
      advIntervalMs: bd.getUint16(6, Endian.little),
      nameMode:      bd.getUint8(8),
      name:          nameStr,
      ledMode:       data.length >= 22 ? bd.getUint8(21) : 0,
    );
  }

  BleSettings copy() => BleSettings(
    opMode:        opMode,
    txPower:       txPower,
    intervalS:     intervalS,
    durationSec:   durationSec,
    advIntervalMs: advIntervalMs,
    nameMode:      nameMode,
    name:          name,
    ledMode:       ledMode,
  );
}
