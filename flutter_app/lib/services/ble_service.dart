import 'dart:async';
import 'dart:typed_data';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

enum FirmwareType { unknown, testFirmware, beacFirmware, demo }

class BleUuids {
  // Test firmware (P2P)
  static final p2pService   = Guid('0000fe40-cc7a-482a-984a-7f2ed5b3e58f');
  static final led          = Guid('0000fe41-8e22-4541-9d4c-21edae82ed19');
  static final switchNotify = Guid('0000fe42-8e22-4541-9d4c-21edae82ed19');
  static final longNotify   = Guid('0000fe43-8e22-4541-9d4c-21edae82ed19');
  // HRS
  static final hrsService   = Guid('0000180d-0000-1000-8000-00805f9b34fb');
  static final hrsMeasure   = Guid('00002a37-0000-1000-8000-00805f9b34fb');
  static final bodySensor   = Guid('00002a38-0000-1000-8000-00805f9b34fb');
  static final hrsCtrl      = Guid('00002a39-0000-1000-8000-00805f9b34fb');
  // Sensor
  static final sensorService = Guid('00000000-0001-11e1-9ab4-0002a5d5c51b');
  static final motion        = Guid('00c00000-0001-11e1-ac36-0002a5d5c51b');
  static final environment   = Guid('00040000-0001-11e1-ac36-0002a5d5c51b');

  // V2 BEAC firmware — 4 bulk characteristics (0000BEAC service)
  static final beacService      = Guid('0000beac-0000-1000-8000-00805f9b34fb');
  static final beacBoardInfo    = Guid('0000bea1-0000-1000-8000-00805f9b34fb');
  static final beacConfig       = Guid('0000bea2-0000-1000-8000-00805f9b34fb');
  static final beacSensorData   = Guid('0000bea3-0000-1000-8000-00805f9b34fb');
  static final beacFlashLog     = Guid('0000bea4-0000-1000-8000-00805f9b34fb');

  // Legacy individual-field UUIDs (beac????-... base) — v1 firmware only
  static Guid beac(int sub) =>
      Guid('beac${sub.toRadixString(16).padLeft(4, '0')}-0000-1000-8000-00805f9b34fb');
  static final beacLegacyService = beac(0x0001);
  static final beacChannel      = beac(0x0010);
  static final beacPowerLevel   = beac(0x0011);
  static final beacTxPeriod     = beac(0x0012);
  static final beacTxDuration   = beac(0x0013);
  static final beacTxPulseOn    = beac(0x0014);
  static final beacTxPulseOff   = beac(0x0015);
  static final beacActiveHours  = beac(0x0016);
  static final beacActiveDays   = beac(0x0017);
  static final beacActiveMonths = beac(0x0018);
  static final beacBleInterval  = beac(0x0019);
  static final beacBleDuration  = beac(0x001A);
  static final beacAnimalTag    = beac(0x001B);
  static final beacTempThresh   = beac(0x001C);
  static final beacSensorEnable = beac(0x001D); // 1B: bit0=temp, bit1=accel
  static final beacBleTxPower   = beac(0x001E); // 1B: index into power table
  static final beacBatteryMv    = beac(0x0020);
  static final beacTemperature  = beac(0x0021);
  static final beacUptime       = beac(0x0022);
  static final beacTxSessions   = beac(0x0023);
  static final beacUniqueId     = beac(0x0024);
  static final beacActivityLog  = beac(0x0025);
  static final beacFwVersion    = beac(0x0026);
  // Accelerometer data notify (6B: x,y,z int16 LE from LIS2DW12 / ISM330DHCX)
  static final beacAccelData    = beac(0x0027);
  // Available sensors bitmask (READ): bit0=temp, bit1=accel, bit2=batt ADC
  static final beacAvailSensors = beac(0x0028);
  // Time sync (WRITE): uint32 LE Unix timestamp (phone → MC)
  static final beacTimeSync     = beac(0x0029);
  // Current RTC time (READ): uint32 LE Unix timestamp from MC
  static final beacCurrentTime  = beac(0x002A);
  // Temp read period (WRITE): uint32 LE seconds; 0=read on TX only
  static final beacTempPeriod   = beac(0x001F);
  // Accel mode (WRITE): 0=off, 1=interrupt, 2=periodic, 3=both
  static final beacAccelMode    = beac(0x002B);
  // Accel read period (WRITE): uint32 LE seconds (when mode 2 or 3)
  static final beacAccelPeriod  = beac(0x002C);
  // BLE TX power index (WRITE): 1B index into blePowerDbm table
  static final beacBattCap      = beac(0x002D);
}

class BleService {
  static final BleService _instance = BleService._();
  factory BleService() => _instance;
  BleService._();

  BluetoothDevice? _device;
  FirmwareType _fwType = FirmwareType.unknown;
  final Map<Guid, BluetoothCharacteristic> _chars = {};
  final _connectionCtrl = StreamController<bool>.broadcast();
  final _rssiCtrl = StreamController<int>.broadcast();

  StreamSubscription? _connectionSub;
  Timer? _rssiTimer;

  Stream<bool> get connectionStream => _connectionCtrl.stream;
  Stream<int> get rssiStream => _rssiCtrl.stream;
  bool get isConnected => _device != null;
  BluetoothDevice? get device => _device;
  FirmwareType get firmwareType => _fwType;
  String get deviceName => _device?.platformName ?? '';

  static bool isLikelyBeacon(ScanResult r) {
    final name = r.device.platformName.toUpperCase();
    final adName = String.fromCharCodes(r.advertisementData.advName.codeUnits).toUpperCase();
    return name.contains('BCN') || name.contains('WB1M') || name.contains('BEACON') ||
           adName.contains('BCN') || adName.contains('BEACON');
  }

  Future<void> connect(BluetoothDevice device) async {
    await disconnect();
    _device = device;
    await device.connect(timeout: const Duration(seconds: 15));

    _connectionSub = device.connectionState.listen((state) {
      final connected = state == BluetoothConnectionState.connected;
      if (!connected) _onDisconnected();
      _connectionCtrl.add(connected);
    });

    // Request larger MTU so 64-byte Config writes and 36-byte BoardInfo reads fit
    try { await device.requestMtu(67); } catch (_) {}

    await _discoverServices();
    _connectionCtrl.add(true);

    _rssiTimer = Timer.periodic(const Duration(seconds: 3), (_) async {
      if (_device != null) {
        try {
          final rssi = await _device!.readRssi();
          _rssiCtrl.add(rssi);
        } catch (_) {}
      }
    });
  }

  Future<void> disconnect() async {
    _rssiTimer?.cancel();
    _connectionSub?.cancel();
    _chars.clear();
    _fwType = FirmwareType.unknown;
    final d = _device;
    _device = null;
    if (d != null) {
      try { await d.disconnect(); } catch (_) {}
    }
    _connectionCtrl.add(false);
  }

  void _onDisconnected() {
    _rssiTimer?.cancel();
    _connectionSub?.cancel();
    _chars.clear();
    _fwType = FirmwareType.unknown;
    _device = null;
  }

  Future<void> _discoverServices() async {
    if (_device == null) return;
    final services = await _device!.discoverServices();
    _chars.clear();

    bool hasBeac = false;
    bool hasP2p  = false;

    for (final svc in services) {
      if (svc.uuid == BleUuids.beacService) hasBeac = true;
      if (svc.uuid == BleUuids.p2pService)  hasP2p = true;
      for (final c in svc.characteristics) {
        _chars[c.uuid] = c;
      }
    }

    if (hasBeac) {
      _fwType = FirmwareType.beacFirmware;
    } else if (hasP2p) {
      _fwType = FirmwareType.testFirmware;
    } else {
      _fwType = FirmwareType.unknown;
    }
  }

  BluetoothCharacteristic? _char(Guid uuid) => _chars[uuid];

  Future<List<int>?> readChar(Guid uuid) async {
    final c = _char(uuid);
    if (c == null) return null;
    try { return await c.read(); } catch (_) { return null; }
  }

  Future<bool> writeChar(Guid uuid, List<int> data, {bool withResponse = false}) async {
    final c = _char(uuid);
    if (c == null) return false;
    try {
      await c.write(data, withoutResponse: !withResponse);
      return true;
    } catch (_) { return false; }
  }

  Future<bool> subscribeChar(Guid uuid, void Function(List<int>) onData) async {
    final c = _char(uuid);
    if (c == null) return false;
    try {
      await c.setNotifyValue(true);
      c.onValueReceived.listen(onData);
      return true;
    } catch (_) { return false; }
  }

  // ---- V2 BEAC firmware bulk characteristic helpers ----

  Future<List<int>?> readBoardInfoRaw() => readChar(BleUuids.beacBoardInfo);
  Future<List<int>?> readConfigRaw()     => readChar(BleUuids.beacConfig);
  Future<List<int>?> readSensorDataRaw() => readChar(BleUuids.beacSensorData);

  Future<bool> writeBeaconConfigBytes(List<int> bytes64) =>
      writeChar(BleUuids.beacConfig, bytes64, withResponse: true);

  Future<bool> writeFlashLogCmd(int cmd) =>
      writeChar(BleUuids.beacFlashLog, [cmd], withResponse: true);

  Future<List<int>?> readFlashLogEntry() => readChar(BleUuids.beacFlashLog);

  Future<bool> subscribeSensorDataV2(void Function(List<int>) onData) =>
      subscribeChar(BleUuids.beacSensorData, onData);

  // Download all log entries; app writes 0x01 then reads 20B entries until all-zero sentinel
  Future<List<List<int>>> downloadFlashLog() async {
    final entries = <List<int>>[];
    await writeFlashLogCmd(0x01);
    for (int i = 0; i < 2100; i++) {
      final bytes = await readFlashLogEntry();
      if (bytes == null || bytes.length < 20) break;
      if (bytes.every((b) => b == 0)) break;  // sentinel = end of log
      entries.add(bytes);
    }
    return entries;
  }

  // Test firmware helpers
  Future<bool> setLed(bool on) =>
      writeChar(BleUuids.led, on ? [0x01, 0x01] : [0x01, 0x00]);

  // BEAC firmware helpers
  Future<int?> readBatteryMv() async {
    final d = await readChar(BleUuids.beacBatteryMv);
    if (d == null || d.length < 2) return null;
    return ByteData.sublistView(Uint8List.fromList(d)).getUint16(0, Endian.little);
  }

  Future<double?> readTemperature() async {
    final d = await readChar(BleUuids.beacTemperature);
    if (d == null || d.isEmpty) return null;
    return d[0].toSigned(8).toDouble();
  }

  Future<int?> readUptime() async {
    final d = await readChar(BleUuids.beacUptime);
    if (d == null || d.length < 4) return null;
    return ByteData.sublistView(Uint8List.fromList(d)).getUint32(0, Endian.little);
  }

  Future<int?> readTxSessions() async {
    final d = await readChar(BleUuids.beacTxSessions);
    if (d == null || d.length < 4) return null;
    return ByteData.sublistView(Uint8List.fromList(d)).getUint32(0, Endian.little);
  }

  Future<String?> readUniqueId() async {
    final d = await readChar(BleUuids.beacUniqueId);
    if (d == null || d.length < 4) return null;
    return d.map((b) => b.toRadixString(16).padLeft(2, '0')).join().toUpperCase();
  }

  Future<String?> readFwVersion() async {
    final d = await readChar(BleUuids.beacFwVersion);
    if (d == null || d.length < 4) return null;
    return '${d[3]}.${d[2]}.${d[1]}.${d[0]}';
  }

  Future<bool> writeChannel(int ch) =>
      writeChar(BleUuids.beacChannel, [ch], withResponse: true);

  Future<bool> writePowerLevel(int level) =>
      writeChar(BleUuids.beacPowerLevel, [level], withResponse: true);

  Future<bool> writeTxPeriod(int ms) =>
      writeChar(BleUuids.beacTxPeriod, _uint32LE(ms), withResponse: true);

  Future<bool> writeTxDuration(int ms) =>
      writeChar(BleUuids.beacTxDuration, _uint32LE(ms), withResponse: true);

  Future<bool> writeTxPulseOn(int ms) =>
      writeChar(BleUuids.beacTxPulseOn, _uint32LE(ms), withResponse: true);

  Future<bool> writeTxPulseOff(int ms) =>
      writeChar(BleUuids.beacTxPulseOff, _uint32LE(ms), withResponse: true);

  Future<bool> writeActiveHours(int mask) =>
      writeChar(BleUuids.beacActiveHours, _uint32LE(mask), withResponse: true);

  Future<bool> writeActiveDays(int mask) =>
      writeChar(BleUuids.beacActiveDays, [mask], withResponse: true);

  Future<bool> writeActiveMonths(int mask) =>
      writeChar(BleUuids.beacActiveMonths, _uint16LE(mask), withResponse: true);

  Future<bool> writeBleInterval(int min) =>
      writeChar(BleUuids.beacBleInterval, _uint16LE(min), withResponse: true);

  Future<bool> writeBleDuration(int sec) =>
      writeChar(BleUuids.beacBleDuration, _uint16LE(sec), withResponse: true);

  Future<bool> writeAnimalTag(String tag) {
    final bytes = List<int>.filled(16, 0);
    final src = tag.codeUnits;
    for (int i = 0; i < src.length && i < 16; i++) bytes[i] = src[i];
    return writeChar(BleUuids.beacAnimalTag, bytes, withResponse: true);
  }

  Future<bool> writeTempThreshold(int c) =>
      writeChar(BleUuids.beacTempThresh, [c & 0xFF], withResponse: true);

  Future<bool> writeSensorEnable(bool tempOn, bool accelOn) {
    int mask = 0;
    if (tempOn) mask |= 0x01;
    if (accelOn) mask |= 0x02;
    return writeChar(BleUuids.beacSensorEnable, [mask], withResponse: true);
  }

  Future<bool> writeBleTxPower(int idx) =>
      writeChar(BleUuids.beacBleTxPower, [idx & 0xFF], withResponse: true);

  // Returns raw bytes from activity log (up to 512 bytes, 8 bytes/record)
  Future<List<int>?> readActivityLog() => readChar(BleUuids.beacActivityLog);

  Future<int?> readAvailSensors() async {
    final d = await readChar(BleUuids.beacAvailSensors);
    if (d == null || d.isEmpty) return null;
    return d[0];
  }

  Future<bool> writeTimeSync(int unixTs) =>
      writeChar(BleUuids.beacTimeSync, _uint32LE(unixTs), withResponse: true);

  Future<int?> readCurrentTime() async {
    final d = await readChar(BleUuids.beacCurrentTime);
    if (d == null || d.length < 4) return null;
    return ByteData.sublistView(Uint8List.fromList(d)).getUint32(0, Endian.little);
  }

  Future<bool> writeTempPeriod(int sec) =>
      writeChar(BleUuids.beacTempPeriod, _uint32LE(sec), withResponse: true);

  Future<bool> writeAccelMode(int mode) =>
      writeChar(BleUuids.beacAccelMode, [mode & 0xFF], withResponse: true);

  Future<bool> writeAccelPeriod(int sec) =>
      writeChar(BleUuids.beacAccelPeriod, _uint32LE(sec), withResponse: true);

  Future<bool> subscribeAccel(void Function(double x, double y, double z) onData) async {
    // Try BEAC firmware first
    var ok = await subscribeChar(BleUuids.beacAccelData, (bytes) {
      if (bytes.length >= 6) {
        final bd = ByteData.sublistView(Uint8List.fromList(bytes));
        onData(
          bd.getInt16(0, Endian.little) / 1000.0,
          bd.getInt16(2, Endian.little) / 1000.0,
          bd.getInt16(4, Endian.little) / 1000.0,
        );
      }
    });
    if (!ok) {
      // Fallback: test firmware motion characteristic (ISM330DHCX 14 bytes)
      // Packet layout: [ts:2B][ax:2B][ay:2B][az:2B][gx:2B][gy:2B][gz:2B]
      ok = await subscribeChar(BleUuids.motion, (bytes) {
        if (bytes.length >= 8) {
          final bd = ByteData.sublistView(Uint8List.fromList(bytes));
          onData(
            bd.getInt16(2, Endian.little) / 1000.0,
            bd.getInt16(4, Endian.little) / 1000.0,
            bd.getInt16(6, Endian.little) / 1000.0,
          );
        }
      });
    }
    return ok;
  }

  static List<int> _uint32LE(int v) {
    final b = ByteData(4)..setUint32(0, v, Endian.little);
    return b.buffer.asUint8List().toList();
  }

  static List<int> _uint16LE(int v) {
    final b = ByteData(2)..setUint16(0, v, Endian.little);
    return b.buffer.asUint8List().toList();
  }

  void dispose() {
    disconnect();
    _connectionCtrl.close();
    _rssiCtrl.close();
  }
}
