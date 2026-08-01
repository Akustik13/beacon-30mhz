import 'dart:convert';
import 'dart:io';
import 'package:path_provider/path_provider.dart';
import 'package:share_plus/share_plus.dart';
import '../models/session_log_entry.dart';
import 'database_service.dart';

class ExportService {
  static final ExportService _instance = ExportService._();
  factory ExportService() => _instance;
  ExportService._();

  final _db = DatabaseService();

  Future<void> exportSessionsCsv({String? beaconId}) async {
    final sessions = await _db.getSessions(beaconId: beaconId, limit: 10000);
    final csv = _sessionsToCsv(sessions);
    await _shareText(csv, 'sessions_${_ts()}.csv', 'text/csv');
  }

  Future<void> exportSessionsJson({String? beaconId}) async {
    final sessions = await _db.getSessions(beaconId: beaconId, limit: 10000);
    final json = jsonEncode(sessions.map((s) => s.toMap()).toList());
    await _shareText(json, 'sessions_${_ts()}.json', 'application/json');
  }

  String _sessionsToCsv(List<SessionLogEntry> sessions) {
    final buf = StringBuffer();
    buf.writeln('timestamp,beaconId,beaconName,gpsLat,gpsLon,gpsAccuracy,'
        'rssi,batteryMv,batteryPercent,temperatureC,uptimeHours,txSessions,'
        'fwVersion,sessionDurationSec,disconnectedAt');
    for (final s in sessions) {
      buf.writeln([
        s.timestamp.toIso8601String(),
        _csv(s.beaconId), _csv(s.beaconName),
        s.gpsLat ?? '', s.gpsLon ?? '', s.gpsAccuracy ?? '',
        s.rssi ?? '', s.batteryMv ?? '', s.batteryPercent ?? '',
        s.temperatureC ?? '', s.uptimeHours ?? '', s.txSessions ?? '',
        _csv(s.fwVersion ?? ''), s.sessionDurationSec,
        s.disconnectedAt?.toIso8601String() ?? '',
      ].join(','));
    }
    return buf.toString();
  }

  String _csv(String s) => '"${s.replaceAll('"', '""')}"';

  Future<void> _shareText(String text, String filename, String mime) async {
    final dir = await getTemporaryDirectory();
    final file = File('${dir.path}/$filename');
    await file.writeAsString(text);
    await Share.shareXFiles([XFile(file.path, mimeType: mime)], subject: filename);
  }

  String _ts() => DateTime.now().toIso8601String().replaceAll(':', '-').substring(0, 19);
}
