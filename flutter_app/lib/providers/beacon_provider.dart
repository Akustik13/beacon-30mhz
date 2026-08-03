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
  ConfigBlob?  config;
  StatusBlob?  status;
  InfoBlob?    info;
  BleSettings? bleSettings;
  Map<String, dynamic>? wakeCfg;
  List<Map<String, dynamic>> sensors = [];

  bool    isBusy            = false;
  bool    isRefreshing       = false;
  bool    _statusRefreshing  = false; // guards against race with writeConfig
  String? lastError;
  String? fwVersion;

  // ── Live chart history (status polling only — never touched by log download)
  final List<ChartPoint> liveTempHistory  = [];
  final List<ChartPoint> liveBatHistory   = [];
  final List<ChartPoint> liveLightHistory = [];

  // ── Downloaded log chart history (log download only — never touched by live)
  final List<ChartPoint> logTempHistory   = [];
  final List<ChartPoint> logBatHistory    = [];
  final List<ChartPoint> logLightHistory  = [];
  final List<ChartPoint> logAccelXHistory = [];
  final List<ChartPoint> logAccelYHistory = [];
  final List<ChartPoint> logAccelZHistory = [];

  static const _histMax = 512;

  // ── Log ──────────────────────────────────────────────────────────────────
  List<Map<String, dynamic>> logEntries = [];
  int  logUsed  = 0;
  int  logTotal = 0;
  int  logFmtVer = 1;
  bool isDownloadingLog  = false;
  double downloadProgress = 0.0;

  // ── RTC time ─────────────────────────────────────────────────────────────
  int?      beaconTimeS;         // unix timestamp from beacon RTC
  DateTime? _beaconTimeReadAt;   // wall clock when beaconTimeS was last set
  DateTime? get beaconTimeReadAt => _beaconTimeReadAt;
  bool  timeSyncedOnConnect = false;

  // ── Derived state ─────────────────────────────────────────────────────────
  bool get logCircular => config?.logOverflow == 1;

  Timer? _refreshTimer;

  // ── Connect / Disconnect ─────────────────────────────────────────────────

  void attach(NusTransport transport) {
    _t = transport;
    config      = null;
    status      = null;
    info        = null;
    bleSettings = null;
    wakeCfg     = null;
    sensors     = [];
    logEntries  = [];
    logUsed     = 0;
    logTotal    = 0;
    beaconTimeS = null;
    _beaconTimeReadAt = null;
    timeSyncedOnConnect = false;
    lastError   = null;
    liveTempHistory.clear();
    liveBatHistory.clear();
    liveLightHistory.clear();
    logTempHistory.clear();
    logBatHistory.clear();
    logLightHistory.clear();
    logAccelXHistory.clear();
    logAccelYHistory.clear();
    logAccelZHistory.clear();
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
    logEntries  = [];
    notifyListeners();
  }

  void _startAutoRefresh() {
    _refreshTimer?.cancel();
    _refreshTimer = Timer.periodic(const Duration(seconds: 5), (_) => refreshStatus());
  }

  // ── Read operations ──────────────────────────────────────────────────────

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
      _readLogInfo(),
      _readBeaconTime(),
    ]);
    isRefreshing = false;
    notifyListeners();
  }

  Future<void> refreshStatus() async {
    if (_t == null || isBusy || _statusRefreshing) return;
    _statusRefreshing = true;
    try {
      await _readStatus();
      notifyListeners();
    } finally {
      _statusRefreshing = false;
    }
  }

  Future<void> _readConfig() async {
    try { config = await _t!.readConfig(); }
    catch (e) { lastError = e.toString(); }
  }

  Future<void> _readStatus() async {
    try {
      // Suppress inactivity reset so background polls don't block auto-disconnect
      _t!.suppressActivity = true;
      status = await _t!.readStatus();
      _t!.suppressActivity = false;
      if (status != null) {
        final now = DateTime.now();
        _addLiveTempFiltered(now, status!.tempC);
        _addLivePoint(liveBatHistory,   now, status!.batMv.toDouble());
        _addLivePoint(liveLightHistory, now, status!.lightRaw.toDouble());
      }
    } catch (e) {
      _t?.suppressActivity = false;
      lastError = e.toString();
    }
  }

  Future<void> _readInfo() async {
    try {
      info = await _t!.readInfo();
      if (info != null) fwVersion = info!.fwVersion;
    } catch (e) { lastError = e.toString(); }
  }

  Future<void> _readBleSettings() async {
    try { bleSettings = await _t!.cmdBleGet(); }
    catch (e) { lastError = e.toString(); }
  }

  Future<void> _readSensors() async {
    try { sensors = await _t!.cmdSensorList(); }
    catch (e) { lastError = e.toString(); }
  }

  Future<void> _readLogInfo() async {
    try {
      final inf = await _t!.cmdLogInfo();
      if (inf != null) {
        logUsed   = inf['used']  as int;
        logTotal  = inf['total'] as int;
        logFmtVer = inf['fmt_ver'] as int;
      }
    } catch (e) { lastError = e.toString(); }
  }

  Future<void> _readBeaconTime() async {
    try {
      final ts = await _t!.cmdTimeGet();
      if (ts != null) {
        beaconTimeS = ts;
        _beaconTimeReadAt = DateTime.now();
        if (!timeSyncedOnConnect) {
          final phoneNow = DateTime.now().millisecondsSinceEpoch ~/ 1000;
          if ((phoneNow - ts).abs() > 60) {
            await _t!.cmdTimeSet(phoneNow);
            beaconTimeS = phoneNow;
            _beaconTimeReadAt = DateTime.now();
            timeSyncedOnConnect = true;
            _autoSyncDone = true;
          }
          timeSyncedOnConnect = true;
        }
      }
    } catch (e) { lastError = e.toString(); }
  }

  // Whether we auto-synced time this session (for the one-time banner)
  bool _autoSyncDone = false;
  bool get autoSyncedTime => _autoSyncDone;
  void clearAutoSyncFlag() { _autoSyncDone = false; }

  void _addLivePoint(List<ChartPoint> list, DateTime ts, double v) {
    list.add(ChartPoint(ts, v));
    if (list.length > _histMax) list.removeAt(0);
  }

  void _addLiveTempFiltered(DateTime ts, double v) {
    // Drop values outside plausible biological range
    if (v < -20.0 || v > 80.0) return;
    // Drop spikes > 8°C from the last reading
    if (liveTempHistory.isNotEmpty &&
        (v - liveTempHistory.last.value).abs() > 8.0) return;
    _addLivePoint(liveTempHistory, ts, v);
  }

  void _addLogPoint(List<ChartPoint> list, DateTime ts, double v) {
    list.add(ChartPoint(ts, v));
    if (list.length > _histMax) list.removeAt(0);
  }

  // ── Write operations ─────────────────────────────────────────────────────

  Future<bool> writeConfig(ConfigBlob cfg) async {
    if (_t == null) return false;
    // Wait for any in-progress background status read to avoid BLE command collision
    int waited = 0;
    while (_statusRefreshing && waited < 500) {
      await Future.delayed(const Duration(milliseconds: 20));
      waited += 20;
    }
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
    if (rc == cmdOk) {
      beaconTimeS = unix;
      _beaconTimeReadAt = DateTime.now();
      notifyListeners();
    }
    return rc == cmdOk;
  }

  Future<bool> reboot() async {
    if (_t == null) return false;
    await _t!.cmdReboot();
    return true;
  }

  // ── Log download ─────────────────────────────────────────────────────────

  Future<void> downloadLog() async {
    if (_t == null) return;
    isDownloadingLog   = true;
    downloadProgress   = 0.0;
    logEntries         = [];
    // Clear log-only chart history — do NOT touch live* history
    logTempHistory.clear();
    logBatHistory.clear();
    logLightHistory.clear();
    logAccelXHistory.clear();
    logAccelYHistory.clear();
    logAccelZHistory.clear();
    notifyListeners();

    try {
      final inf = await _t!.cmdLogInfo();
      if (inf == null) { isDownloadingLog = false; notifyListeners(); return; }

      logUsed    = inf['used']     as int;
      logTotal   = inf['total']   as int;
      logFmtVer  = inf['fmt_ver'] as int;
      final recSize = inf['rec_size'] as int;
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
          _dispatchToLogChart(m);
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

  // Write log record data ONLY to log* chart lists (never touches live*)
  void _dispatchToLogChart(Map<String, dynamic> m) {
    final ts = m['ts'] != null
        ? DateTime.fromMillisecondsSinceEpoch((m['ts'] as int) * 1000)
        : DateTime.now();
    final tc = m['temp_c'];
    if (tc != null) _addLogPoint(logTempHistory, ts, (tc as num).toDouble());
    final bmv = m['bat_mv'];
    if (bmv != null) _addLogPoint(logBatHistory, ts, (bmv as num).toDouble());
    final lr = m['light_raw'];
    if (lr != null) _addLogPoint(logLightHistory, ts, (lr as num).toDouble());
    final ax = m['accel_x'];
    final ay = m['accel_y'];
    final az = m['accel_z'];
    if (ax != null) _addLogPoint(logAccelXHistory, ts, (ax as num).toDouble());
    if (ay != null) _addLogPoint(logAccelYHistory, ts, (ay as num).toDouble());
    if (az != null) _addLogPoint(logAccelZHistory, ts, (az as num).toDouble());
  }

  Future<bool> eraseLog() async {
    if (_t == null) return false;
    isBusy = true; notifyListeners();
    final rc = await _t!.cmdLogErase();
    final ok = rc == cmdOk;
    if (ok) {
      logEntries = [];
      logUsed    = 0;
      logTempHistory.clear();
      logBatHistory.clear();
      logLightHistory.clear();
      logAccelXHistory.clear();
      logAccelYHistory.clear();
      logAccelZHistory.clear();
    }
    isBusy = false; notifyListeners();
    return ok;
  }

  // ── Helpers ──────────────────────────────────────────────────────────────

  double? get tempC    => status?.tempC;
  int?    get batMv    => status?.batMv;
  int?    get batPct   => status?.batPct;
  int?    get uptimeS  => status?.uptimeS;
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
