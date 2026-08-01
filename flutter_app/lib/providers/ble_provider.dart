import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
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

  bool   get isScanning   => _isScanning;
  bool   get isConnecting => _isConnecting;
  bool   get isConnected  => _isConnected;
  int    get rssi         => _rssi;
  String get statusMsg    => _statusMsg;
  String? get connectedName => _connectedName;
  String get connectedMac => _connectedDevice?.remoteId.str ?? '';

  List<ScannedDevice> get scanResults => List.unmodifiable(_scanResults);
  NusTransport? get transport => _transport;

  // ── Scan ─────────────────────────────────────────────────────────────────

  Future<void> startScan({int timeoutSec = 8}) async {
    if (_isScanning) return;
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
  }

  // ── Connect ───────────────────────────────────────────────────────────────

  Future<bool> connect(BluetoothDevice device, String name) async {
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

    _transport      = t;
    _connectedDevice = device;
    _connectedName  = name;
    _isConnected    = true;
    _statusMsg      = 'Connected: $name';
    notifyListeners();

    // Watch for disconnect
    _connSub = t.connectionState.listen((connected) {
      if (!connected && _isConnected) {
        _onDisconnected();
      }
    });

    // RSSI polling
    _rssiTimer = Timer.periodic(const Duration(seconds: 3), (_) async {
      try {
        _rssi = await device.readRssi();
        notifyListeners();
      } catch (_) {}
    });

    return true;
  }

  void _onDisconnected() {
    _rssiTimer?.cancel();
    _rssiTimer = null;
    _connSub?.cancel();
    _connSub = null;
    _transport = null;
    _connectedDevice = null;
    _isConnected = false;
    _rssi = 0;
    _statusMsg = 'Disconnected';
    notifyListeners();
  }

  Future<void> disconnect() async {
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
    notifyListeners();
  }

  // ── Connect by MAC (from saved list) ──────────────────────────────────────

  Future<bool> connectByMac(String mac, String name) async {
    final device = BluetoothDevice.fromId(mac);
    return connect(device, name);
  }

  @override
  void dispose() {
    _rssiTimer?.cancel();
    _connSub?.cancel();
    _scanSub?.cancel();
    _transport?.dispose();
    super.dispose();
  }
}
