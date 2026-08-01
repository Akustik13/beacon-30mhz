import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import '../transport/nus_ble_transport.dart';
import '../protocol/config_blob.dart';
import '../protocol/status_blob.dart';
import '../protocol/info_blob.dart';
import '../protocol/ble_settings.dart';
import '../protocol/log_record.dart';
import '../protocol/opcodes.dart';

class ChartPoint {
  final DateTime ts;
  final double value;
  ChartPoint(this.ts, this.value);
}

class BeaconProvider extends ChangeNotifier {
  NusTransport? _t;

  // ── Current state ─────────────────────────────────────────────────────────
  ConfigBlob? config;
  StatusBlob? status;
  InfoBlob?   info;
  BleSettings? bleSettings;
  Map<String, dynamic>? wakeCfg;
  List<Map<String, dynamic>> sensors = [];

  bool  isBusy   = false;
  bool  isRefreshing = false;
  String? lastError;
  String? fwVersion;

  // ── Log ───────────────────────────────────────────────────────────────────
  List<Map<String, dynamic>> logEntries = [];
  int  logUsed  = 0;
  int  logTotal = 0;
  int  logFmtVer = 1;
  bool isDownloadingLog = false;
  double downloadProgress = 0.0;

  // ── Chart history ─────────────────────────────────────────────────────────
  final List<ChartPoint> tempHistory   = [];
  final List<ChartPoint> batHistory    = [];
  final List<ChartPoint> lightHistory  = [];
  final List<ChartPoint> accelXHistory = [];
  final List<ChartPoint> accelYHistory = [];
  final List<ChartPoint> accelZHistory = [];

  static const _histMax = 512;

  Timer? _refreshTimer;

  // ── Connect / Disconnect ──────────────────────────────────────────────────

  void attach(NusTransport transport) {
    _t = transport;
    config     = null;
    status     = null;
    info       = null;
    bleSettings = null;
    wakeCfg    = null;
    sensors    = [];
    logEntries = [];
    lastError  = null;
    notifyListeners();
    _startAutoRefresh();
  }

  void detach() {
    _refreshTimer?.cancel();
    _refreshTimer = null;
    _t = null;
    config  = null;
    status  = null;
    info    = null;
    bleSettings = null;
    logEntries = [];
    notifyListeners();
  }

  void _startAutoRefresh() {
    _refreshTimer?.cancel();
    _refreshTimer = Timer.periodic(const Duration(seconds: 5), (_) => refreshStatus());
  }

  // ── Read operations ───────────────────────────────────────────────────────

  Future<void> refreshAll() async {
    if (_t == null) return;
    isRefreshing = true;
    notifyListeners();
    await Future.wait([
      _readConfig(),
      _readStatus(),
      _readInfo(),
      _readBleSettings(),
      _readSensors(),
    ]);
    isRefreshing = false;
    notifyListeners();
  }

  Future<void> refreshStatus() async {
    if (_t == null || isBusy) return;
    await _readStatus();
    notifyListeners();
  }

  Future<void> _readConfig() async {
    try {
      config = await _t!.readConfig();
    } catch (e) { lastError = e.toString(); }
  }

  Future<void> _readStatus() async {
    try {
      status = await _t!.readStatus();
      if (status != null) {
        final now = DateTime.now();
        _addPoint(tempHistory,  now, status!.tempC);
        _addPoint(batHistory,   now, status!.batMv.toDouble());
        _addPoint(lightHistory, now, status!.lightRaw.toDouble());
      }
    } catch (e) { lastError = e.toString(); }
  }

  Future<void> _readInfo() async {
    try {
      info = await _t!.readInfo();
      if (info != null) {
        fwVersion = info!.fwVersion;
      }
    } catch (e) { lastError = e.toString(); }
  }

  Future<void> _readBleSettings() async {
    try {
      bleSettings = await _t!.cmdBleGet();
    } catch (e) { lastError = e.toString(); }
  }

  Future<void> _readSensors() async {
    try {
      sensors = await _t!.cmdSensorList();
    } catch (e) { lastError = e.toString(); }
  }

  void _addPoint(List<ChartPoint> list, DateTime ts, double v) {
    list.add(ChartPoint(ts, v));
    if (list.length > _histMax) list.removeAt(0);
  }

  // ── Write operations ──────────────────────────────────────────────────────

  Future<bool> writeConfig(ConfigBlob cfg) async {
    if (_t == null) return false;
    isBusy = true; notifyListeners();
    final ok = await _t!.writeConfig(cfg);
    if (ok) config = cfg;
    lastError = ok ? null : 'Config write failed';
    isBusy = false; notifyListeners();
    return ok;
  }

  Future<bool> writeBleSettings(BleSettings s) async {
    if (_t == null) return false;
    isBusy = true; notifyListeners();
    final rc = await _t!.cmdBleSet(s);
    final ok = rc == cmdOk;
    if (ok) bleSettings = s;
    lastError = ok ? null : 'BLE settings write failed';
    isBusy = false; notifyListeners();
    return ok;
  }

  Future<bool> setSensorEnabled(int id, bool en) async {
    if (_t == null) return false;
    final rc = await _t!.cmdSensorEnable(id, en);
    if (rc == cmdOk) await _readSensors();
    notifyListeners();
    return rc == cmdOk;
  }

  Future<bool> setSensorInterval(int id, int intervalS) async {
    if (_t == null) return false;
    final rc = await _t!.cmdSensorInterval(id, intervalS);
    if (rc == cmdOk) await _readSensors();
    notifyListeners();
    return rc == cmdOk;
  }

  Future<bool> writeWakeCfg(Map<String, dynamic> cfg) async {
    if (_t == null) return false;
    isBusy = true; notifyListeners();
    final rc = await _t!.cmdWakeCfgSet(
      enable: cfg['enable'] as int,
      thresholdMg: cfg['threshold_mg'] as int,
      durationMs: cfg['duration_ms'] as int,
      action: cfg['action'] as int,
    );
    final ok = rc == cmdOk;
    if (ok) wakeCfg = cfg;
    isBusy = false; notifyListeners();
    return ok;
  }

  Future<bool> clearWakeCount() async {
    if (_t == null) return false;
    final rc = await _t!.cmdWakeClear();
    return rc == cmdOk;
  }

  Future<bool> syncTime() async {
    if (_t == null) return false;
    final unix = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    final rc = await _t!.cmdTimeSet(unix);
    return rc == cmdOk;
  }

  Future<bool> reboot() async {
    if (_t == null) return false;
    await _t!.cmdReboot();
    return true;
  }

  // ── Log download ──────────────────────────────────────────────────────────

  Future<void> downloadLog() async {
    if (_t == null) return;
    isDownloadingLog = true;
    downloadProgress = 0.0;
    logEntries = [];
    notifyListeners();

    try {
      final info = await _t!.cmdLogInfo();
      if (info == null) { isDownloadingLog = false; notifyListeners(); return; }

      logUsed    = info['used'] as int;
      logTotal   = info['total'] as int;
      logFmtVer  = info['fmt_ver'] as int;
      final recSize = info['rec_size'] as int;
      if (logUsed == 0 || recSize == 0) {
        isDownloadingLog = false; notifyListeners(); return;
      }

      const batchSize = 32;
      int offset = 0;
      final entries = <Map<String, dynamic>>[];

      while (offset < logUsed) {
        final count = (logUsed - offset).clamp(0, batchSize);
        final raw = await _t!.cmdLogRead(offset, count);
        if (raw.isEmpty) break;

        final recs = raw.length ~/ recSize;
        for (int i = 0; i < recs; i++) {
          final slice = Uint8List.fromList(raw.sublist(i * recSize, (i + 1) * recSize));
          if (slice.length < 16) continue;
          final m = parseLogRecord(slice, logFmtVer);
          if (m == null) continue;
          entries.add(m);
          _dispatchToChart(m);
        }

        offset += recs;
        downloadProgress = logUsed > 0 ? offset / logUsed : 1.0;
        notifyListeners();
      }

      logEntries = entries;
    } catch (e) {
      lastError = e.toString();
    }

    isDownloadingLog = false;
    downloadProgress = 1.0;
    notifyListeners();
  }

  void _dispatchToChart(Map<String, dynamic> m) {
    final ts = m['ts'] != null
        ? DateTime.fromMillisecondsSinceEpoch((m['ts'] as int) * 1000)
        : DateTime.now();
    final tc = m['temp_c'];
    if (tc != null) _addPoint(tempHistory, ts, (tc as num).toDouble());
    final bmv = m['bat_mv'];
    if (bmv != null) _addPoint(batHistory, ts, (bmv as num).toDouble());
    final lr = m['light_raw'];
    if (lr != null) _addPoint(lightHistory, ts, (lr as num).toDouble());
    final ax = m['accel_x'];
    final ay = m['accel_y'];
    final az = m['accel_z'];
    if (ax != null) _addPoint(accelXHistory, ts, (ax as num).toDouble());
    if (ay != null) _addPoint(accelYHistory, ts, (ay as num).toDouble());
    if (az != null) _addPoint(accelZHistory, ts, (az as num).toDouble());
  }

  Future<bool> eraseLog() async {
    if (_t == null) return false;
    isBusy = true; notifyListeners();
    final rc = await _t!.cmdLogErase();
    final ok = rc == cmdOk;
    if (ok) { logEntries = []; logUsed = 0; }
    isBusy = false; notifyListeners();
    return ok;
  }

  // ── Helpers ───────────────────────────────────────────────────────────────

  double? get tempC => status?.tempC;
  int?    get batMv => status?.batMv;
  int?    get batPct => status?.batPct;
  int?    get uptimeS => status?.uptimeS;
  int?    get lightRaw => status?.lightRaw;
  bool    get txActive => status?.txActive == 1;

  Map<String, int>? sensorById(int id) {
    for (final s in sensors) {
      if (s['id'] == id) return Map<String, int>.from(s);
    }
    return null;
  }

  bool isSensorEnabled(int id) => sensorById(id)?['enabled'] == 1;
  int  sensorInterval(int id)  => sensorById(id)?['interval_s'] ?? 60;

  double memoryWritesPerS() {
    double w = 0;
    for (final s in sensors) {
      if (s['enabled'] == 1 && (s['interval_s'] as int) > 0) {
        w += 1.0 / (s['interval_s'] as int);
      }
    }
    return w;
  }

  @override
  void dispose() {
    _refreshTimer?.cancel();
    super.dispose();
  }
}
