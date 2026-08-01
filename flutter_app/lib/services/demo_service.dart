import 'dart:async';
import 'dart:math';

class DemoData {
  final double temperatureC;
  final int batteryMv;
  final int batteryPercent;
  final int rssi;
  final int uptimeHours;
  final int txSessions;
  final String fwVersion;
  final bool ledOn;

  const DemoData({
    required this.temperatureC,
    required this.batteryMv,
    required this.batteryPercent,
    required this.rssi,
    required this.uptimeHours,
    required this.txSessions,
    required this.fwVersion,
    required this.ledOn,
  });
}

class DemoService {
  static final DemoService _instance = DemoService._();
  factory DemoService() => _instance;
  DemoService._();

  final _rnd = Random();
  final _dataCtrl = StreamController<DemoData>.broadcast();
  Timer? _timer;

  double _temp = 36.8;
  int _battMv = 3750;
  int _rssi = -65;
  int _uptime = 1234;
  int _txSessions = 567;
  bool _ledOn = false;

  Stream<DemoData> get stream => _dataCtrl.stream;
  bool get isRunning => _timer != null;
  bool get ledOn => _ledOn;

  void start() {
    _timer?.cancel();
    _timer = Timer.periodic(const Duration(seconds: 2), (_) => _tick());
    _tick();
  }

  void stop() {
    _timer?.cancel();
    _timer = null;
  }

  void setLed(bool on) {
    _ledOn = on;
    _tick();
  }

  void _tick() {
    _temp += (_rnd.nextDouble() - 0.5) * 0.4;
    _temp = _temp.clamp(35.0, 38.5);
    _battMv += _rnd.nextInt(3) - 1;
    _battMv = _battMv.clamp(3200, 4200);
    _rssi += _rnd.nextInt(5) - 2;
    _rssi = _rssi.clamp(-90, -40);

    final pct = ((_battMv - 3000) / (4200 - 3000) * 100).clamp(0, 100).round();
    _dataCtrl.add(DemoData(
      temperatureC: double.parse(_temp.toStringAsFixed(1)),
      batteryMv: _battMv,
      batteryPercent: pct,
      rssi: _rssi,
      uptimeHours: _uptime,
      txSessions: _txSessions,
      fwVersion: '1.0.0.0',
      ledOn: _ledOn,
    ));
  }

  // Demo scan results — return fake beacon names
  List<Map<String, dynamic>> fakeScanResults() => [
    {'name': 'BCN_TEST_DEMO1', 'mac': 'AA:BB:CC:DD:EE:01', 'rssi': -62},
    {'name': 'BCN_TEST_DEMO2', 'mac': 'AA:BB:CC:DD:EE:02', 'rssi': -74},
  ];

  void dispose() {
    stop();
    _dataCtrl.close();
  }
}
