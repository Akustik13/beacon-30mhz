import 'dart:typed_data';

// Parsed record from 0xBEAC0025 activity log (8 bytes each):
// [0-3] uint32 LE: Unix timestamp (sec)
// [4]   int8:      temperature °C
// [5]   uint8:     battery percent 0-100
// [6]   uint8:     flags (bit0=motion, bit1=temp_alarm)
// [7]   uint8:     reserved
class ActivityRecord {
  final DateTime timestamp;
  final double temperatureC;
  final int batteryPercent;
  final bool motionDetected;
  final bool tempAlarm;

  const ActivityRecord({
    required this.timestamp,
    required this.temperatureC,
    required this.batteryPercent,
    required this.motionDetected,
    required this.tempAlarm,
  });

  static List<ActivityRecord> parse(List<int> bytes) {
    final records = <ActivityRecord>[];
    for (int i = 0; i + 7 < bytes.length; i += 8) {
      final sub = Uint8List.fromList(bytes.sublist(i, i + 8));
      final bd = ByteData.sublistView(sub);
      final tsRaw = bd.getUint32(0, Endian.little);
      if (tsRaw == 0 || tsRaw == 0xFFFFFFFF) continue; // blank entry
      final temp = sub[4].toSigned(8);
      final bat = sub[5].clamp(0, 100);
      final flags = sub[6];
      records.add(ActivityRecord(
        timestamp: DateTime.fromMillisecondsSinceEpoch(tsRaw * 1000),
        temperatureC: temp.toDouble(),
        batteryPercent: bat,
        motionDetected: (flags & 0x01) != 0,
        tempAlarm: (flags & 0x02) != 0,
      ));
    }
    return records;
  }

  // Generate demo data for testing
  static List<ActivityRecord> demo({int count = 48}) {
    final now = DateTime.now();
    final records = <ActivityRecord>[];
    double temp = 36.5;
    int bat = 85;
    for (int i = count - 1; i >= 0; i--) {
      temp += (0.5 - (i % 7) * 0.15);
      if (temp > 38.5) temp = 36.2;
      if (temp < 35.5) temp = 36.8;
      bat -= (i % 8 == 0 ? 1 : 0);
      records.add(ActivityRecord(
        timestamp: now.subtract(Duration(hours: i)),
        temperatureC: double.parse(temp.toStringAsFixed(1)),
        batteryPercent: bat.clamp(0, 100),
        motionDetected: (i % 3 == 0),
        tempAlarm: temp > 38.0,
      ));
    }
    return records;
  }
}
