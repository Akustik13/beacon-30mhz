import 'package:sqflite/sqflite.dart';
import 'package:path/path.dart';
import '../models/session_log_entry.dart';
import '../models/activity_log_entry.dart';

class DatabaseService {
  static final DatabaseService _instance = DatabaseService._();
  factory DatabaseService() => _instance;
  DatabaseService._();

  Database? _db;

  Future<Database> get db async {
    _db ??= await _open();
    return _db!;
  }

  Future<Database> _open() async {
    final path = join(await getDatabasesPath(), 'beacon_manager.db');
    return openDatabase(
      path,
      version: 1,
      onCreate: (db, version) async {
        await db.execute('''
          CREATE TABLE sessions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            beaconId TEXT NOT NULL,
            beaconName TEXT NOT NULL,
            gpsLat REAL, gpsLon REAL, gpsAccuracy REAL,
            gpsAddress TEXT, rssi INTEGER,
            batteryMv INTEGER, batteryPercent INTEGER,
            temperatureC REAL, uptimeHours INTEGER, txSessions INTEGER,
            fwVersion TEXT, sessionDurationSec INTEGER DEFAULT 0,
            notes TEXT DEFAULT '', disconnectedAt TEXT
          )
        ''');
        await db.execute('''
          CREATE TABLE settings_changes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            beaconId TEXT NOT NULL,
            gpsLat REAL, gpsLon REAL,
            fieldName TEXT NOT NULL,
            oldValue TEXT NOT NULL,
            newValue TEXT NOT NULL,
            writeSuccess INTEGER DEFAULT 1
          )
        ''');
        await db.execute('''
          CREATE TABLE activity_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            beaconId TEXT NOT NULL,
            eventType TEXT NOT NULL,
            description TEXT NOT NULL,
            gpsLat REAL, gpsLon REAL,
            rssi INTEGER, data TEXT
          )
        ''');
        await db.execute('CREATE INDEX idx_sessions_beacon ON sessions(beaconId)');
        await db.execute('CREATE INDEX idx_sessions_time ON sessions(timestamp)');
        await db.execute('CREATE INDEX idx_activity_beacon ON activity_log(beaconId)');
      },
    );
  }

  // Sessions
  Future<int> insertSession(SessionLogEntry e) async {
    final d = await db;
    return d.insert('sessions', e.toMap()..remove('id'));
  }

  Future<void> updateSession(SessionLogEntry e) async {
    if (e.id == null) return;
    final d = await db;
    await d.update('sessions', e.toMap(), where: 'id = ?', whereArgs: [e.id]);
  }

  Future<List<SessionLogEntry>> getSessions({String? beaconId, int limit = 100}) async {
    final d = await db;
    final rows = await d.query(
      'sessions',
      where: beaconId != null ? 'beaconId = ?' : null,
      whereArgs: beaconId != null ? [beaconId] : null,
      orderBy: 'timestamp DESC',
      limit: limit,
    );
    return rows.map(SessionLogEntry.fromMap).toList();
  }

  Future<int> getSessionCount(String beaconId) async {
    final d = await db;
    final r = await d.rawQuery(
        'SELECT COUNT(*) as c FROM sessions WHERE beaconId = ?', [beaconId]);
    return Sqflite.firstIntValue(r) ?? 0;
  }

  // Settings changes
  Future<void> insertSettingsChange(SettingsChangeEntry e) async {
    final d = await db;
    await d.insert('settings_changes', e.toMap()..remove('id'));
  }

  Future<List<SettingsChangeEntry>> getSettingsChanges(String beaconId, {int limit = 50}) async {
    final d = await db;
    final rows = await d.query(
      'settings_changes',
      where: 'beaconId = ?',
      whereArgs: [beaconId],
      orderBy: 'timestamp DESC',
      limit: limit,
    );
    return rows.map(SettingsChangeEntry.fromMap).toList();
  }

  // Activity log
  Future<void> insertActivity(ActivityLogEntry e) async {
    final d = await db;
    await d.insert('activity_log', e.toMap()..remove('id'));
  }

  Future<List<ActivityLogEntry>> getActivity({String? beaconId, int limit = 200}) async {
    final d = await db;
    final rows = await d.query(
      'activity_log',
      where: beaconId != null ? 'beaconId = ?' : null,
      whereArgs: beaconId != null ? [beaconId] : null,
      orderBy: 'timestamp DESC',
      limit: limit,
    );
    return rows.map(ActivityLogEntry.fromMap).toList();
  }

  Future<void> clearActivity() async {
    final d = await db;
    await d.delete('activity_log');
  }

  Future<void> close() async {
    await _db?.close();
    _db = null;
  }
}
