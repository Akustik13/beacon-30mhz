import 'dart:math';
import 'dart:typed_data';

/// Parsed from 22-byte SensorData characteristic (0x0000BEA3)
class SensorData {
  final double extTempC;      // STTS22H: ext_temp_raw / 100.0
  final double intTempC;      // MCU ADC: int_temp_raw / 100.0 (0 on eval board)
  final int accelX;           // mg
  final int accelY;           // mg
  final int accelZ;           // mg
  final int batteryMv;
  final bool motionDetected;
  final int timestampSec;     // seconds since boot
  final int txSessions;

  const SensorData({
    required this.extTempC,
    required this.intTempC,
    required this.accelX,
    required this.accelY,
    required this.accelZ,
    required this.batteryMv,
    required this.motionDetected,
    required this.timestampSec,
    required this.txSessions,
  });

  double get batteryPct =>
      ((batteryMv - 3000) / (4200 - 3000) * 100).clamp(0.0, 100.0);

  int get accelMagnitude =>
      sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ).round();

  double get displayTempC => extTempC != 0.0 ? extTempC : intTempC;

  factory SensorData.fromBytes(List<int> raw) {
    if (raw.length < 22) raw = [...raw, ...List.filled(22 - raw.length, 0)];
    final bytes = Uint8List.fromList(raw);
    final bd = ByteData.sublistView(bytes);
    return SensorData(
      extTempC:       bd.getInt16(0,  Endian.little) / 100.0,
      intTempC:       bd.getInt16(2,  Endian.little) / 100.0,
      accelX:         bd.getInt16(4,  Endian.little),
      accelY:         bd.getInt16(6,  Endian.little),
      accelZ:         bd.getInt16(8,  Endian.little),
      batteryMv:      bd.getUint16(10, Endian.little),
      motionDetected: bytes[12] != 0,
      timestampSec:   bd.getUint32(14, Endian.little),
      txSessions:     bd.getUint32(18, Endian.little),
    );
  }
}
