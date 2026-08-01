import 'package:flutter/foundation.dart';
import 'package:sqflite/sqflite.dart';
import 'package:path/path.dart' as p;

/// Minimal saved-beacon record (persisted to sqflite).
class SavedBeacon {
  final String mac;
  String name;
  String notes;
  DateTime? lastSeen;
  int? lastBatMv;
  double? lastTempC;
  int rssi;

  SavedBeacon({
    required this.mac,
    this.name = '',
    this.notes = '',
    this.lastSeen,
    this.lastBatMv,
    this.lastTempC,
    this.rssi = 0,
  });

  String get displayName => name.isNotEmpty ? name : mac;

  Map<String, dynamic> toMap() => {
    'mac': mac,
    'name': name,
    'notes': notes,
    'last_seen': lastSeen?.millisecondsSinceEpoch,
    'last_bat_mv': lastBatMv,
    'last_temp_c': lastTempC,
    'rssi': rssi,
  };

  factory SavedBeacon.fromMap(Map<String, dynamic> m) => SavedBeacon(
    mac: m['mac'] as String,
    name: (m['name'] as String?) ?? '',
    notes: (m['notes'] as String?) ?? '',
    lastSeen: m['last_seen'] != null
        ? DateTime.fromMillisecondsSinceEpoch(m['last_seen'] as int)
        : null,
    lastBatMv: m['last_bat_mv'] as int?,
    lastTempC: m['last_temp_c'] as double?,
    rssi: (m['rssi'] as int?) ?? 0,
  );
}

class DevicesProvider extends ChangeNotifier {
  Database? _db;
  List<SavedBeacon> _devices = [];
  bool _loaded = false;

  List<SavedBeacon> get devices => List.unmodifiable(_devices);

  Future<void> load() async {
    if (_loaded) return;
    await _openDb();
    await _reload();
    _loaded = true;
  }

  Future<void> _openDb() async {
    final path = p.join(await getDatabasesPath(), 'beacon_devices.db');
    _db = await openDatabase(path, version: 1,
      onCreate: (db, ver) => db.execute('''
        CREATE TABLE devices(
          mac TEXT PRIMARY KEY,
          name TEXT,
          notes TEXT,
          last_seen INTEGER,
          last_bat_mv INTEGER,
          last_temp_c REAL,
          rssi INTEGER
        )
      '''),
    );
  }

  Future<void> _reload() async {
    if (_db == null) return;
    final rows = await _db!.query('devices', orderBy: 'last_seen DESC');
    _devices = rows.map(SavedBeacon.fromMap).toList();
    notifyListeners();
  }

  Future<void> add(SavedBeacon d) async {
    await _db?.insert('devices', d.toMap(),
        conflictAlgorithm: ConflictAlgorithm.replace);
    await _reload();
  }

  Future<void> update(SavedBeacon d) async {
    await _db?.update('devices', d.toMap(),
        where: 'mac = ?', whereArgs: [d.mac]);
    await _reload();
  }

  Future<void> remove(String mac) async {
    await _db?.delete('devices', where: 'mac = ?', whereArgs: [mac]);
    _devices.removeWhere((d) => d.mac == mac);
    notifyListeners();
  }

  /// Update last-seen info after a successful connect.
  Future<void> touchDevice(String mac, String name,
      {int? batMv, double? tempC, int rssi = 0}) async {
    final existing = _devices.where((d) => d.mac == mac).firstOrNull;
    if (existing != null) {
      existing.lastSeen  = DateTime.now();
      existing.lastBatMv = batMv ?? existing.lastBatMv;
      existing.lastTempC = tempC ?? existing.lastTempC;
      existing.rssi      = rssi;
      if (existing.name.isEmpty && name.isNotEmpty) existing.name = name;
      await update(existing);
    } else {
      await add(SavedBeacon(
        mac: mac, name: name, lastSeen: DateTime.now(),
        lastBatMv: batMv, lastTempC: tempC, rssi: rssi,
      ));
    }
  }

  SavedBeacon? byMac(String mac) =>
      _devices.where((d) => d.mac == mac).firstOrNull;
}
