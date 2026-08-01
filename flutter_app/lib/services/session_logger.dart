import '../models/activity_log_entry.dart';
import '../models/session_log_entry.dart';
import 'database_service.dart';
import 'location_service.dart';

class SessionLogger {
  static final SessionLogger _instance = SessionLogger._();
  factory SessionLogger() => _instance;
  SessionLogger._();

  final _db = DatabaseService();
  final _loc = LocationService();

  int? _activeSessionId;
  DateTime? _sessionStart;
  String? _activeBeaconId;

  Future<void> onConnect({
    required String beaconId,
    required String beaconName,
    int? rssi,
    int? batteryMv,
    int? batteryPercent,
    double? temperatureC,
    int? uptimeHours,
    int? txSessions,
    String? fwVersion,
  }) async {
    final loc = await _loc.getCurrentLocation();
    _sessionStart = DateTime.now();
    _activeBeaconId = beaconId;

    final entry = SessionLogEntry(
      timestamp: _sessionStart!,
      beaconId: beaconId,
      beaconName: beaconName,
      gpsLat: loc?.lat,
      gpsLon: loc?.lon,
      gpsAccuracy: loc?.accuracy,
      rssi: rssi,
      batteryMv: batteryMv,
      batteryPercent: batteryPercent,
      temperatureC: temperatureC,
      uptimeHours: uptimeHours,
      txSessions: txSessions,
      fwVersion: fwVersion,
    );
    _activeSessionId = await _db.insertSession(entry);

    await _db.insertActivity(ActivityLogEntry(
      timestamp: _sessionStart!,
      beaconId: beaconId,
      eventType: 'connect',
      description: 'Connected to $beaconName',
      gpsLat: loc?.lat,
      gpsLon: loc?.lon,
      rssi: rssi,
    ));
  }

  Future<void> onDisconnect() async {
    if (_activeSessionId == null || _sessionStart == null) return;
    final now = DateTime.now();
    final dur = now.difference(_sessionStart!).inSeconds;
    final loc = await _loc.getCurrentLocation();

    final sessions = await _db.getSessions(beaconId: _activeBeaconId, limit: 1);
    if (sessions.isNotEmpty) {
      final original = sessions.first;
      final updated = SessionLogEntry(
        id: _activeSessionId,
        timestamp: original.timestamp,
        beaconId: original.beaconId,
        beaconName: original.beaconName,
        gpsLat: original.gpsLat,
        gpsLon: original.gpsLon,
        gpsAccuracy: original.gpsAccuracy,
        gpsAddress: original.gpsAddress,
        rssi: original.rssi,
        batteryMv: original.batteryMv,
        batteryPercent: original.batteryPercent,
        temperatureC: original.temperatureC,
        uptimeHours: original.uptimeHours,
        txSessions: original.txSessions,
        fwVersion: original.fwVersion,
        sessionDurationSec: dur,
        notes: original.notes,
        disconnectedAt: now,
      );
      await _db.updateSession(updated);
    }

    if (_activeBeaconId != null) {
      await _db.insertActivity(ActivityLogEntry(
        timestamp: now,
        beaconId: _activeBeaconId!,
        eventType: 'disconnect',
        description: 'Disconnected (${dur}s session)',
        gpsLat: loc?.lat,
        gpsLon: loc?.lon,
      ));
    }

    _activeSessionId = null;
    _sessionStart = null;
    _activeBeaconId = null;
  }

  Future<void> logWrite({
    required String beaconId,
    required String fieldName,
    required String oldValue,
    required String newValue,
    required bool success,
  }) async {
    final loc = _loc.last;
    final entry = SettingsChangeEntry(
      timestamp: DateTime.now(),
      beaconId: beaconId,
      gpsLat: loc?.lat,
      gpsLon: loc?.lon,
      fieldName: fieldName,
      oldValue: oldValue,
      newValue: newValue,
      writeSuccess: success,
    );
    await _db.insertSettingsChange(entry);

    await _db.insertActivity(ActivityLogEntry(
      timestamp: DateTime.now(),
      beaconId: beaconId,
      eventType: success ? 'write' : 'error',
      description: '${success ? 'Wrote' : 'Failed'} $fieldName: $newValue',
      gpsLat: loc?.lat,
      gpsLon: loc?.lon,
    ));
  }

  Future<void> logActivity({
    required String beaconId,
    required String eventType,
    required String description,
    int? rssi,
  }) async {
    final loc = _loc.last;
    await _db.insertActivity(ActivityLogEntry(
      timestamp: DateTime.now(),
      beaconId: beaconId,
      eventType: eventType,
      description: description,
      gpsLat: loc?.lat,
      gpsLon: loc?.lon,
      rssi: rssi,
    ));
  }
}
