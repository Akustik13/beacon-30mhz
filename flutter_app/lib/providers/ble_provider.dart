import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:vibration/vibration.dart';
import '../transport/nus_ble_transport.dart';

class ScannedDevice {
  final BluetoothDevice device;
  final String name;
  final int rssi;
  ScannedDevice(this.device, this.name, this.rssi);
  String get mac => device.remoteId.str;
}

class BleProvider extends ChangeNotifier {
  NusTransport? _transport;
  BluetoothDevice? _connectedDevice;

  bool _isScanning   = false;
  bool _isConnecting = false;
  bool _isConnected  = false;
  int  _rssi         = 0;
  String _statusMsg  = '';
  String? _connectedName;

  final List<ScannedDevice> _scanResults = [];
  StreamSubscription? _scanSub;
  StreamSubscription? _connSub;
  Timer? _rssiTimer;

  // ── Callback invoked before every scan to refresh known-device MACs ───────
  VoidCallback? onBeforeScan;

  // ── Auto-connect on scan ─────────────────────────────────────────────────
  Set<String> _autoConnectMacs  = {};
  bool        _autoConnectFired = false;

  void setAutoConnectMacs(Set<String> macs) {
    _autoConnectMacs  = Set.of(macs);
    _autoConnectFired = false;
  }

  // ── Auto-scan (continuous scanning with restart) ──────────────────────────
  bool   _autoScanEnabled     = false;
  Timer? _autoScanRestartTimer;

  bool get autoScanEnabled => _autoScanEnabled;

  Future<void> loadPrefs() async {
    final prefs = await SharedPreferences.getInstance();
    _autoScanEnabled = prefs.getBool('auto_scan') ?? false;
    notifyListeners();
  }

  Future<void> setAutoScanEnabled(bool v) async {
    _autoScanEnabled = v;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool('auto_scan', v);
    _autoScanRestartTimer?.cancel();
    _autoScanRestartTimer = null;
    if (v && !_isConnected && !_isScanning) startScan(timeoutSec: 12);
    notifyListeners();
  }

  Future<void> _kickAutoScan() async {
    if (!_autoScanEnabled || _isConnected || _isScanning) return;
    await startScan(timeoutSec: 5);
  }

  // ── Auto-disconnect ───────────────────────────────────────────────────────
  static const int _kAutoDisconnectSec = 60;
  static const int _kWarningSec        = 10;

  Timer? _inactivityTicker;
  int    _inactivityCountdown    = _kAutoDisconnectSec;
  bool   _disconnectWarning      = false;
  bool   _autoDisconnectEnabled  = true;

  bool get disconnectWarning     => _disconnectWarning && _isConnected;
  int  get inactivityCountdown   => _inactivityCountdown;
  bool get autoDisconnectEnabled => _autoDisconnectEnabled;

  // ── Getters ───────────────────────────────────────────────────────────────
  bool    get isScanning   => _isScanning;
  bool    get isConnecting => _isConnecting;
  bool    get isConnected  => _isConnected;
  int     get rssi         => _rssi;
  String  get statusMsg    => _statusMsg;
  String? get connectedName => _connectedName;
  String  get connectedMac => _connectedDevice?.remoteId.str ?? '';

  List<ScannedDevice> get scanResults => List.unmodifiable(_scanResults);
  NusTransport? get transport => _transport;

  // ── Scan ──────────────────────────────────────────────────────────────────

  Future<void> startScan({int timeoutSec = 8}) async {
    if (_isScanning) return;
    onBeforeScan?.call(); // refresh known MACs before each scan
    _scanResults.clear();
    _isScanning = true;
    _statusMsg = 'Scanning…';
    notifyListeners();

    try {
      await FlutterBluePlus.startScan(timeout: Duration(seconds: timeoutSec));
      _scanSub = FlutterBluePlus.scanResults.listen((results) {
        _scanResults.clear();
        for (final r in results) {
          final name = r.device.platformName.isNotEmpty
              ? r.device.platformName : r.advertisementData.advName;
          _scanResults.add(ScannedDevice(r.device, name, r.rssi));
        }
        notifyListeners();

        // Real-time auto-connect: stop scan and connect when a known device appears
        if (_autoConnectMacs.isNotEmpty && !_autoConnectFired &&
            !_isConnected && !_isConnecting) {
          for (final d in _scanResults) {
            if (_autoConnectMacs.contains(d.mac)) {
              _autoConnectFired = true;
              Timer.run(() async {
                await stopScan();
                final ok = await connect(d.device, d.name);
                if (!ok) _autoConnectFired = false; // allow retry on next scan
              });
              break;
            }
          }
        }
      });
      await Future.delayed(Duration(seconds: timeoutSec));
    } catch (_) {}

    await stopScan();
  }

  Future<void> stopScan() async {
    _scanSub?.cancel();
    _scanSub = null;
    try { await FlutterBluePlus.stopScan(); } catch (_) {}
    _isScanning = false;
    _statusMsg = 'Found ${_scanResults.length} device(s)';
    notifyListeners();
    if (_autoScanEnabled && !_isConnected) {
      _autoScanRestartTimer?.cancel();
      _autoScanRestartTimer = Timer(const Duration(milliseconds: 300), _kickAutoScan);
    }
  }

  // ── Connect ───────────────────────────────────────────────────────────────

  Future<bool> connect(BluetoothDevice device, String name) async {
    if (_isScanning) await stopScan();
    if (_isConnected) await disconnect();
    _isConnecting = true;
    _statusMsg = 'Connecting to $name…';
    notifyListeners();

    final t = NusTransport();
    final ok = await t.connect(device);
    _isConnecting = false;

    if (!ok) {
      _statusMsg = 'Connection failed';
      notifyListeners();
      return false;
    }

    // Wire inactivity callback
    t.onActivity = resetInactivity;

    _transport      = t;
    _connectedDevice = device;
    _connectedName  = name;
    _isConnected    = true;
    _statusMsg      = 'Connected: $name';
    Vibration.vibrate(duration: 350).catchError((_) {}); // single pulse on connect
    notifyListeners();

    // Watch for remote disconnect
    _connSub = t.connectionState.listen((connected) {
      if (!connected && _isConnected) _onDisconnected();
    });

    // RSSI polling
    _rssiTimer = Timer.periodic(const Duration(seconds: 3), (_) async {
      try { _rssi = await device.readRssi(); notifyListeners(); } catch (_) {}
    });

    // Start inactivity timer (reads saved preference)
    final prefs = await SharedPreferences.getInstance();
    _autoDisconnectEnabled = prefs.getBool('auto_disconnect') ?? true;
    if (_autoDisconnectEnabled) _startInactivityTimer();

    return true;
  }

  void _onDisconnected() {
    _stopInactivityTimer();
    _rssiTimer?.cancel();
    _rssiTimer = null;
    _connSub?.cancel();
    _connSub = null;
    _transport = null;
    _connectedDevice = null;
    _isConnected = false;
    _rssi = 0;
    _statusMsg = 'Disconnected';
    _vibrateDisconnect();
    notifyListeners();
    if (_autoScanEnabled) {
      _autoScanRestartTimer?.cancel();
      _autoScanRestartTimer = Timer(const Duration(seconds: 1), _kickAutoScan);
    }
  }

  Future<void> disconnect() async {
    _stopInactivityTimer();
    _rssiTimer?.cancel();
    _rssiTimer = null;
    _connSub?.cancel();
    _connSub = null;
    await _transport?.disconnect();
    _transport = null;
    _connectedDevice = null;
    _isConnected = false;
    _rssi = 0;
    _statusMsg = 'Disconnected';
    _vibrateDisconnect();
    notifyListeners();
    if (_autoScanEnabled) {
      _autoScanRestartTimer?.cancel();
      _autoScanRestartTimer = Timer(const Duration(seconds: 1), _kickAutoScan);
    }
  }

  // ── Connect by MAC ────────────────────────────────────────────────────────

  Future<bool> connectByMac(String mac, String name) async {
    final device = BluetoothDevice.fromId(mac);
    return connect(device, name);
  }

  // ── Auto-disconnect timer ─────────────────────────────────────────────────

  void _startInactivityTimer() {
    _inactivityTicker?.cancel();
    _inactivityCountdown = _kAutoDisconnectSec;
    _disconnectWarning   = false;
    _inactivityTicker = Timer.periodic(const Duration(seconds: 1), (_) {
      if (!_isConnected) { _stopInactivityTimer(); return; }
      _inactivityCountdown--;
      if (_inactivityCountdown <= _kWarningSec && !_disconnectWarning) {
        _disconnectWarning = true;
      }
      notifyListeners();
      if (_inactivityCountdown <= 0) {
        _stopInactivityTimer();
        disconnect();
      }
    });
  }

  void _stopInactivityTimer() {
    _inactivityTicker?.cancel();
    _inactivityTicker = null;
    _disconnectWarning   = false;
    _inactivityCountdown = _kAutoDisconnectSec;
  }

  /// Resets the inactivity countdown. Called automatically on every
  /// successful BLE command via NusTransport.onActivity.
  void resetInactivity() {
    if (!_autoDisconnectEnabled || !_isConnected) return;
    _inactivityCountdown = _kAutoDisconnectSec;
    if (_disconnectWarning) {
      _disconnectWarning = false;
      notifyListeners();
    }
  }

  /// Enable or disable auto-disconnect. Also saves to SharedPreferences.
  Future<void> setAutoDisconnectEnabled(bool v) async {
    _autoDisconnectEnabled = v;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool('auto_disconnect', v);
    if (!v) {
      _stopInactivityTimer();
      notifyListeners();
    } else if (_isConnected) {
      _startInactivityTimer();
    }
  }

  void _vibrateDisconnect() {
    Vibration.vibrate(pattern: [0, 120, 100, 120]).catchError((_) {});
  }

  @override
  void dispose() {
    _autoScanRestartTimer?.cancel();
    _stopInactivityTimer();
    _rssiTimer?.cancel();
    _connSub?.cancel();
    _scanSub?.cancel();
    _transport?.dispose();
    super.dispose();
  }
}
