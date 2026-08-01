import 'dart:typed_data';

class BeaconData {
  const BeaconData({
    required this.timestamp,
    required this.temperatureC,
    required this.rawHex,
    this.batteryMv,
  });

  final int timestamp;
  final double temperatureC;
  final String rawHex;
  final int? batteryMv;

  static BeaconData? fromEnvPacket(List<int> bytes) {
    if (bytes.length < 4) return null;
    final data = ByteData.sublistView(Uint8List.fromList(bytes));
    final timestamp = data.getUint16(0, Endian.little);
    final rawTemperature = data.getInt16(2, Endian.little);
    final batteryMv = bytes.length >= 6
        ? data.getUint16(4, Endian.little)
        : null;
    return BeaconData(
      timestamp: timestamp,
      temperatureC: rawTemperature / 10.0,
      rawHex: bytesToHex(bytes),
      batteryMv: batteryMv,
    );
  }

  static String bytesToHex(List<int> bytes) {
    return bytes
        .map((b) => b.toRadixString(16).padLeft(2, '0').toUpperCase())
        .join(' ');
  }
}

class AccelData {
  const AccelData({
    required this.x,
    required this.y,
    required this.z,
  });

  final double x; // g
  final double y;
  final double z;

  static AccelData? fromMotionPacket(List<int> bytes) {
    if (bytes.length < 8) return null;
    final data = ByteData.sublistView(Uint8List.fromList(bytes));
    // bytes[0:2] = timestamp, bytes[2:8] = accel X,Y,Z in mg (int16 LE)
    return AccelData(
      x: data.getInt16(2, Endian.little) / 1000.0,
      y: data.getInt16(4, Endian.little) / 1000.0,
      z: data.getInt16(6, Endian.little) / 1000.0,
    );
  }
}
