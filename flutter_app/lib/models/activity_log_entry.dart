class ActivityLogEntry {
  final int? id;
  final DateTime timestamp;
  final String beaconId;
  final String eventType; // 'connect','disconnect','write','read','notify','error','gps'
  final String description;
  final double? gpsLat;
  final double? gpsLon;
  final int? rssi;
  final Map<String, dynamic> data;

  const ActivityLogEntry({
    this.id,
    required this.timestamp,
    required this.beaconId,
    required this.eventType,
    required this.description,
    this.gpsLat,
    this.gpsLon,
    this.rssi,
    this.data = const {},
  });

  Map<String, dynamic> toMap() => {
    'id': id, 'timestamp': timestamp.toIso8601String(),
    'beaconId': beaconId, 'eventType': eventType,
    'description': description, 'gpsLat': gpsLat,
    'gpsLon': gpsLon, 'rssi': rssi,
    'data': data.isNotEmpty ? data.toString() : null,
  };

  factory ActivityLogEntry.fromMap(Map<String, dynamic> m) => ActivityLogEntry(
    id: m['id'],
    timestamp: DateTime.parse(m['timestamp']),
    beaconId: m['beaconId'] ?? '',
    eventType: m['eventType'] ?? 'unknown',
    description: m['description'] ?? '',
    gpsLat: m['gpsLat']?.toDouble(),
    gpsLon: m['gpsLon']?.toDouble(),
    rssi: m['rssi'],
  );
}
