import 'dart:typed_data';

/// Parsed from 20-byte FlashLog characteristic read (0x0000BEA4)
class FlashLogEntry {
  final int timestampSec;     // seconds since boot
  final double extTempC;      // STTS22H temperature
  final double intTempC;      // MCU ADC temperature (0 on eval board)
  final int accelX;           // mg
  final int accelY;           // mg
  final int accelZ;           // mg
  final int batteryMv;
  final bool motion;

  const FlashLogEntry({
    required this.timestampSec,
    required this.extTempC,
    required this.intTempC,
    required this.accelX,
    required this.accelY,
    required this.accelZ,
    required this.batteryMv,
    required this.motion,
  });

  bool get isSentinel => timestampSec == 0 && extTempC == 0 &&
      accelX == 0 && accelY == 0 && accelZ == 0;

  factory FlashLogEntry.fromBytes(List<int> raw) {
    if (raw.length < 20) raw = [...raw, ...List.filled(20 - raw.length, 0)];
    final bytes = Uint8List.fromList(raw);
    final bd = ByteData.sublistView(bytes);
    return FlashLogEntry(
      timestampSec: bd.getUint32(0,  Endian.little),
      extTempC:     bd.getInt16(4,   Endian.little) / 100.0,
      intTempC:     bd.getInt16(6,   Endian.little) / 100.0,
      accelX:       bd.getInt16(8,   Endian.little),
      accelY:       bd.getInt16(10,  Endian.little),
      accelZ:       bd.getInt16(12,  Endian.little),
      batteryMv:    bd.getUint16(14, Endian.little),
      motion:       bytes[16] != 0,
    );
  }

  String toCsvRow() =>
      '$timestampSec,${extTempC.toStringAsFixed(2)},${intTempC.toStringAsFixed(2)},'
      '$accelX,$accelY,$accelZ,$batteryMv,${motion ? 1 : 0}';

  static String csvHeader() =>
      'timestamp_sec,ext_temp_c,int_temp_c,accel_x_mg,accel_y_mg,accel_z_mg,battery_mv,motion';
}
