import 'dart:async';
import 'dart:io';

import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

import 'models/beacon_data.dart';
export 'models/beacon_data.dart' show AccelData;

class BleUuids {
  static final Guid p2pService          = Guid('0000fe40-cc7a-482a-984a-7f2ed5b3e58f');
  static final Guid led                 = Guid('0000fe41-8e22-4541-9d4c-21edae82ed19');
  static final Guid switchNotify        = Guid('0000fe42-8e22-4541-9d4c-21edae82ed19');
  static final Guid longNotify          = Guid('0000fe43-8e22-4541-9d4c-21edae82ed19');

  static final Guid heartRateService    = Guid('180d');
  static final Guid heartRateMeasurement = Guid('2a37');
  static final Guid bodySensorLocation  = Guid('2a38');
  static final Guid heartRateControlPoint = Guid('2a39');

  static final Guid sensorService       = Guid('00000000-0001-11e1-9ab4-0002a5d5c51b');
  static final Guid motion              = Guid('00c00000-0001-11e1-ac36-0002a5d5c51b');
  static final Guid environment         = Guid('00040000-0001-11e1-ac36-0002a5d5c51b');
}

class BleService {
  BluetoothDevice? connectedDevice;
  List<BluetoothService> services = [];
  BluetoothCharacteristic? environmentCharacteristic;
  BluetoothCharacteristic? ledCharacteristic;
  BluetoothCharacteristic? motionCharacteristic;

  final _beaconDataController = StreamController<BeaconData>.broadcast();
  final _connectionController = StreamController<bool>.broadcast();
  final _accelDataController  = StreamController<AccelData>.broadcast();

  Stream<BeaconData> get beaconData => _beaconDataController.stream;
  Stream<bool> get connectionStateStream => _connectionController.stream;
  Stream<AccelData> get accelData => _accelDataController.stream;
  bool get isConnected => connectedDevice != null;

  Future<void> requestPermissions() async {
    if (!Platform.isAndroid) return;
    await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.locationWhenInUse,
    ].request();
  }

  Stream<List<ScanResult>> get scanResults => FlutterBluePlus.scanResults;

  Future<void> startScan() async {
    await requestPermissions();
    if (await FlutterBluePlus.isSupported == false) {
      throw Exception('Bluetooth LE not supported on this device');
    }
    final state = await FlutterBluePlus.adapterState.first;
    if (state != BluetoothAdapterState.on) {
      throw Exception('Bluetooth is off — enable it in settings');
    }
    await FlutterBluePlus.startScan(
      timeout: const Duration(seconds: 10),
      androidUsesFineLocation: true,
    );
  }

  Future<void> stopScan() async => FlutterBluePlus.stopScan();

  bool isLikelyBeacon(ScanResult result) {
    final name =
        '${result.device.platformName} ${result.advertisementData.advName}'
            .toUpperCase();
    return name.contains('BCN') || name.contains('WB1M');
  }

  Future<void> connect(BluetoothDevice device) async {
    await stopScan();
    await device.connect(timeout: const Duration(seconds: 12), autoConnect: false);
    connectedDevice = device;
    services = await device.discoverServices(timeout: 10);
    _mapKnownCharacteristics();
    _connectionController.add(true);
    await subscribeEnvironment();
    await subscribeMotion();
  }

  Future<void> disconnect() async {
    final device = connectedDevice;
    if (device != null) await device.disconnect();
    connectedDevice = null;
    services = [];
    environmentCharacteristic = null;
    ledCharacteristic = null;
    motionCharacteristic = null;
    _connectionController.add(false);
  }

  Future<List<int>> read(BluetoothCharacteristic c) => c.read(timeout: 8);

  Future<void> writeHex(
    BluetoothCharacteristic c,
    String hex, {
    bool withoutResponse = false,
  }) async {
    final data = parseHex(hex);
    await c.write(data, withoutResponse: withoutResponse, timeout: 8);
  }

  Future<void> subscribe(BluetoothCharacteristic c, bool enabled) =>
      c.setNotifyValue(enabled, timeout: 8);

  Future<void> subscribeEnvironment() async {
    final c = environmentCharacteristic;
    if (c == null) return;
    await c.setNotifyValue(true, timeout: 8);
    c.onValueReceived.listen((v) {
      final data = BeaconData.fromEnvPacket(v);
      if (data != null) _beaconDataController.add(data);
    });
    final current = await c.read(timeout: 8);
    final data = BeaconData.fromEnvPacket(current);
    if (data != null) _beaconDataController.add(data);
  }

  Future<void> subscribeMotion() async {
    final c = motionCharacteristic;
    if (c == null) return;
    await c.setNotifyValue(true, timeout: 8);
    c.onValueReceived.listen((v) {
      final data = AccelData.fromMotionPacket(v);
      if (data != null) _accelDataController.add(data);
    });
  }

  Future<void> writeLed(bool on) async {
    final c = ledCharacteristic;
    if (c == null) throw Exception('LED characteristic not found');
    await c.write([0x01, on ? 0x01 : 0x00], withoutResponse: true);
  }

  Future<void> sendTestData() async {
    final c = ledCharacteristic;
    if (c != null) await c.write([0xAA, 0x55], withoutResponse: true);
  }

  void _mapKnownCharacteristics() {
    environmentCharacteristic = null;
    ledCharacteristic = null;
    motionCharacteristic = null;
    for (final s in services) {
      for (final c in s.characteristics) {
        if (c.uuid == BleUuids.environment) environmentCharacteristic = c;
        if (c.uuid == BleUuids.led) ledCharacteristic = c;
        if (c.uuid == BleUuids.motion) motionCharacteristic = c;
      }
    }
  }

  static List<int> parseHex(String input) {
    final clean = input.replaceAll(RegExp(r'[^0-9a-fA-F]'), '');
    if (clean.isEmpty) return [];
    if (clean.length.isOdd) throw const FormatException('HEX must have even digit count');
    return [
      for (var i = 0; i < clean.length; i += 2)
        int.parse(clean.substring(i, i + 2), radix: 16)
    ];
  }

  void dispose() {
    _beaconDataController.close();
    _connectionController.close();
    _accelDataController.close();
  }
}
