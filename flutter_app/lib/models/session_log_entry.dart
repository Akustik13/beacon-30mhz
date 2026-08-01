class SessionLogEntry {
  final int? id;
  final DateTime timestamp;
  final String beaconId;
  final String beaconName;
  final double? gpsLat;
  final double? gpsLon;
  final double? gpsAccuracy;
  final String? gpsAddress;
  final int? rssi;
  final int? batteryMv;
  final int? batteryPercent;
  final double? temperatureC;
  final int? uptimeHours;
  final int? txSessions;
  final String? fwVersion;
  final Map<String, dynamic> settingsSent;
  final Map<String, dynamic> settingsReceived;
  final int sessionDurationSec;
  final String notes;
  final DateTime? disconnectedAt;

  const SessionLogEntry({
    this.id,
    required this.timestamp,
    required this.beaconId,
    required this.beaconName,
    this.gpsLat,
    this.gpsLon,
    this.gpsAccuracy,
    this.gpsAddress,
    this.rssi,
    this.batteryMv,
    this.batteryPercent,
    this.temperatureC,
    this.uptimeHours,
    this.txSessions,
    this.fwVersion,
    this.settingsSent = const {},
    this.settingsReceived = const {},
    this.sessionDurationSec = 0,
    this.notes = '',
    this.disconnectedAt,
  });

  Map<String, dynamic> toMap() => {
    'id': id, 'timestamp': timestamp.toIso8601String(),
    'beaconId': beaconId, 'beaconName': beaconName,
    'gpsLat': gpsLat, 'gpsLon': gpsLon, 'gpsAccuracy': gpsAccuracy,
    'gpsAddress': gpsAddress, 'rssi': rssi,
    'batteryMv': batteryMv, 'batteryPercent': batteryPercent,
    'temperatureC': temperatureC, 'uptimeHours': uptimeHours,
    'txSessions': txSessions, 'fwVersion': fwVersion,
    'sessionDurationSec': sessionDurationSec, 'notes': notes,
    'disconnectedAt': disconnectedAt?.toIso8601String(),
  };

  factory SessionLogEntry.fromMap(Map<String, dynamic> m) => SessionLogEntry(
    id: m['id'],
    timestamp: DateTime.parse(m['timestamp']),
    beaconId: m['beaconId'] ?? '',
    beaconName: m['beaconName'] ?? '',
    gpsLat: m['gpsLat']?.toDouble(),
    gpsLon: m['gpsLon']?.toDouble(),
    gpsAccuracy: m['gpsAccuracy']?.toDouble(),
    gpsAddress: m['gpsAddress'],
    rssi: m['rssi'],
    batteryMv: m['batteryMv'],
    batteryPercent: m['batteryPercent'],
    temperatureC: m['temperatureC']?.toDouble(),
    uptimeHours: m['uptimeHours'],
    txSessions: m['txSessions'],
    fwVersion: m['fwVersion'],
    sessionDurationSec: m['sessionDurationSec'] ?? 0,
    notes: m['notes'] ?? '',
    disconnectedAt: m['disconnectedAt'] != null ? DateTime.tryParse(m['disconnectedAt']) : null,
  );
}

class SettingsChangeEntry {
  final int? id;
  final DateTime timestamp;
  final String beaconId;
  final double? gpsLat;
  final double? gpsLon;
  final String fieldName;
  final String oldValue;
  final String newValue;
  final bool writeSuccess;

  const SettingsChangeEntry({
    this.id,
    required this.timestamp,
    required this.beaconId,
    this.gpsLat,
    this.gpsLon,
    required this.fieldName,
    required this.oldValue,
    required this.newValue,
    this.writeSuccess = true,
  });

  Map<String, dynamic> toMap() => {
    'id': id, 'timestamp': timestamp.toIso8601String(),
    'beaconId': beaconId, 'gpsLat': gpsLat, 'gpsLon': gpsLon,
    'fieldName': fieldName, 'oldValue': oldValue,
    'newValue': newValue, 'writeSuccess': writeSuccess ? 1 : 0,
  };

  factory SettingsChangeEntry.fromMap(Map<String, dynamic> m) =>
      SettingsChangeEntry(
        id: m['id'],
        timestamp: DateTime.parse(m['timestamp']),
        beaconId: m['beaconId'] ?? '',
        gpsLat: m['gpsLat']?.toDouble(),
        gpsLon: m['gpsLon']?.toDouble(),
        fieldName: m['fieldName'] ?? '',
        oldValue: m['oldValue'] ?? '',
        newValue: m['newValue'] ?? '',
        writeSuccess: (m['writeSuccess'] ?? 1) == 1,
      );
}
