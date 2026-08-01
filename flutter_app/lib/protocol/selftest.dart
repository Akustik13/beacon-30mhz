import 'dart:typed_data';
import 'config_blob.dart';
import 'crc32.dart';
import 'status_blob.dart';
import 'info_blob.dart';
import 'log_record.dart';
import 'ble_settings.dart';
import 'opcodes.dart';

/// Run protocol self-tests. Returns list of failure messages (empty = all pass).
/// Mirrors run_selftest() in tx_beacon_gui_v4.py.
List<String> runSelftest() {
  final failures = <String>[];

  // ── CRC32 known-value check ───────────────────────────────────────────────
  // CRC-32/ISO-HDLC: poly=0xEDB88320, init=0xFFFFFFFF, xorout=0xFFFFFFFF
  // "123456789" → 0xCBF43926  (universally known test vector)
  final crcVec = Crc32.compute(Uint8List.fromList([
    0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39]));
  if (crcVec != 0xCBF43926) {
    failures.add('CRC32 "123456789" FAIL: got 0x${crcVec.toRadixString(16)} expected 0xcbf43926');
  }
  // CRC32 of 60 zero bytes (verified by the Dart implementation)
  final crcZeros = Crc32.compute(Uint8List(60));
  if (crcZeros != 0x04128908) {
    failures.add('CRC32 zeros(60) FAIL: got 0x${crcZeros.toRadixString(16)} expected 0x4128908');
  }

  // ── ConfigBlob roundtrip ──────────────────────────────────────────────────
  final c = ConfigBlob(
    rfMode:      1,
    rfChannel:   2,
    rfPower:     3,
    rfPulseMs:   50,
    rfPeriodMs:  3000,
    tempIvS:     60,
    batIvS:      3600,
    schedEn:     1,
    schedHours:  0x00FF00,
    schedMonths: 0x0F0,
  );
  final raw = c.toBytes();
  if (raw.length != 64) {
    failures.add('ConfigBlob size ${raw.length} != 64');
  } else {
    final c2 = ConfigBlob.fromBytes(raw);
    if (c2.rfChannel != 2)       failures.add('ConfigBlob rfChannel roundtrip FAIL');
    if (c2.schedHours != 0x00FF00) failures.add('ConfigBlob schedHours roundtrip FAIL');
    if (!c2.verifyCrc())         failures.add('ConfigBlob CRC verify FAIL after roundtrip');

    // CRC corruption detection
    final corrupted = Uint8List.fromList(raw);
    corrupted[3] ^= 0x01;
    final c3 = ConfigBlob.fromBytes(corrupted);
    if (c3.verifyCrc()) failures.add('ConfigBlob should detect corrupted CRC');
  }

  // ── StatusBlob roundtrip ──────────────────────────────────────────────────
  final sBd = ByteData(24);
  sBd.setUint32(0,  3600,       Endian.little);
  sBd.setInt16( 4,  364,        Endian.little);
  sBd.setUint16(6,  3300,       Endian.little);
  sBd.setUint16(8,  3720,       Endian.little);
  sBd.setUint8( 10, 82);
  sBd.setUint8( 11, 1);
  sBd.setUint16(12, 1240,       Endian.little);
  sBd.setUint8( 14, 1);
  sBd.setUint8( 15, 1);
  sBd.setUint16(16, 247,        Endian.little);
  sBd.setUint16(18, 768,        Endian.little);
  sBd.setUint32(20, 1721000000, Endian.little);
  final stat = StatusBlob.fromBytes(sBd.buffer.asUint8List());
  if (stat.temp01c != 364) failures.add('StatusBlob temp01c FAIL');
  if (stat.batMv   != 3720) failures.add('StatusBlob batMv FAIL');
  if (stat.logUsed != 247)  failures.add('StatusBlob logUsed FAIL');

  // ── InfoBlob roundtrip ────────────────────────────────────────────────────
  final iBd = ByteData(48);
  iBd.setUint8(0, 1); iBd.setUint8(1, 1); iBd.setUint8(2, 1);
  iBd.setUint8(3, 2); iBd.setUint8(4, 0); iBd.setUint8(5, 7);
  iBd.setUint16(6, 16, Endian.little);
  iBd.setUint32(8, 0xDEADBEEF, Endian.little);
  iBd.setUint32(12, 768,  Endian.little);
  iBd.setUint32(16, 2048, Endian.little);
  final iRaw = iBd.buffer.asUint8List();
  // Write tag "RAT_001" at offset 20
  final tag = 'RAT_001'.codeUnits;
  for (int i = 0; i < tag.length; i++) iRaw[20 + i] = tag[i];
  iBd.setUint32(32, 42, Endian.little);
  iBd.setUint32(36, 8,  Endian.little);
  iBd.setUint32(40, 3,  Endian.little);
  iBd.setUint32(44, 15, Endian.little);
  final info = InfoBlob.fromBytes(iRaw);
  if (info.uid           != 0xDEADBEEF) failures.add('InfoBlob uid FAIL');
  if (info.tag           != 'RAT_001')  failures.add('InfoBlob tag FAIL: ${info.tag}');
  if (info.totalActiveH  != 42)         failures.add('InfoBlob totalActiveH FAIL');
  if (info.totalStop1H   != 8)          failures.add('InfoBlob totalStop1H FAIL');
  if (info.flashEraseCount != 15)       failures.add('InfoBlob flashEraseCount FAIL');

  // ── BleSettings roundtrip ────────────────────────────────────────────────
  final bs = BleSettings(
    opMode: bleOpGekon, txPower: 20, intervalS: 900, durationSec: 30,
    advIntervalMs: 500, nameMode: 1, name: 'TEST', ledMode: 1,
  );
  final bsRaw = bs.toBytes();
  if (bsRaw.length != 22) {
    failures.add('BleSettings size ${bsRaw.length} != 22');
  } else {
    final bs2 = BleSettings.fromBytes(bsRaw);
    if (bs2.opMode       != bleOpGekon) failures.add('BleSettings opMode FAIL');
    if (bs2.name         != 'TEST')     failures.add('BleSettings name FAIL: ${bs2.name}');
    if (bs2.ledMode      != 1)          failures.add('BleSettings ledMode FAIL');
  }

  // ── LogRecordV2 roundtrip ────────────────────────────────────────────────
  // TEMP record: ts=1000, type=0x00, flags=0, temp01c=364, vdda_mv=3300
  final lBd = ByteData(16);
  lBd.setUint32(0, 1000, Endian.little);
  lBd.setUint8(4, logrecTypeTemp);
  lBd.setUint8(5, 0);
  lBd.setInt16(6, 364, Endian.little);
  lBd.setUint16(8, 3300, Endian.little);
  final lv2 = LogRecordV2.fromBytes(lBd.buffer.asUint8List());
  if (lv2.timestamp != 1000)   failures.add('LogRecordV2 timestamp FAIL');
  if (lv2.type != logrecTypeTemp) failures.add('LogRecordV2 type FAIL');
  final tc = lv2.tempC;
  if (tc == null || (tc - 36.4).abs() > 0.01) failures.add('LogRecordV2 tempC FAIL: $tc');

  return failures;
}
