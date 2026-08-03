import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../protocol/opcodes.dart';
import '../protocol/config_blob.dart';
import '../protocol/status_blob.dart';
import '../protocol/info_blob.dart';
import '../protocol/ble_settings.dart';

/// Result of a single command: result-code byte + optional payload
typedef CmdResult = ({int rc, Uint8List data});

/// Low-level NUS BLE transport.
/// Identical wire protocol to BleTransport in tx_beacon_gui_v4.py.
class NusTransport {
  static const _defaultTimeout = Duration(seconds: 5);
  static const _logTimeout     = Duration(seconds: 12);
  static const _eraseTimeout   = Duration(seconds: 12);
  static const _commitTimeout  = Duration(seconds: 10);

  BluetoothDevice?             _device;
  BluetoothCharacteristic?     _rxChar; // write: phone → MCU
  BluetoothCharacteristic?     _txChar; // notify: MCU → phone
  StreamSubscription<List<int>>? _notifySub;

  int  _mtu        = 23;
  int  _reqId      = 0;
  bool _connected  = false;

  // Accumulates incoming notification bytes across packets
  Uint8List _rxBuf = Uint8List(0);

  // Map from request-id → Completer<List<String>>
  final _pending = <int, _PendingCmd>{};

  // Stream of unsolicited telemetry lines (not tagged with #id)
  final _telemetryCtrl = StreamController<String>.broadcast();
  Stream<String> get telemetry => _telemetryCtrl.stream;

  final _connCtrl = StreamController<bool>.broadcast();
  Stream<bool> get connectionState => _connCtrl.stream;

  bool get isConnected => _connected;

  // ── Connect ────────────────────────────────────────────────────────────────

  Future<bool> connect(BluetoothDevice device) async {
    _device = device;
    try {
      await device.connect(timeout: const Duration(seconds: 15), autoConnect: false);
    } catch (e) {
      return false;
    }

    // MTU negotiation — request 247, gracefully fall back
    try {
      _mtu = await device.requestMtu(247);
    } catch (_) {
      _mtu = 23;
    }

    // Discover NUS service
    final services = await device.discoverServices();
    BluetoothService? nus;
    for (final svc in services) {
      if (svc.uuid.toString().toLowerCase() == nusServiceUuid) {
        nus = svc; break;
      }
    }
    if (nus == null) {
      await device.disconnect();
      return false;
    }

    for (final c in nus.characteristics) {
      final uuid = c.uuid.toString().toLowerCase();
      if (uuid == nusRxCharUuid) _rxChar = c;
      if (uuid == nusTxCharUuid) _txChar = c;
    }
    if (_rxChar == null || _txChar == null) {
      await device.disconnect();
      return false;
    }

    // Subscribe to TX notifications
    await _txChar!.setNotifyValue(true);
    _notifySub = _txChar!.lastValueStream.listen(_onNotify);

    // Handle remote disconnect
    device.connectionState.listen((state) {
      if (state == BluetoothConnectionState.disconnected && _connected) {
        _handleDisconnect();
      }
    });

    _connected = true;
    _connCtrl.add(true);
    return true;
  }

  void _handleDisconnect() {
    _connected = false;
    _connCtrl.add(false);
    // Fail all pending commands
    for (final p in _pending.values) {
      if (!p.completer.isCompleted) p.completer.completeError('disconnected');
    }
    _pending.clear();
  }

  Future<void> disconnect() async {
    _notifySub?.cancel();
    _notifySub = null;
    try { await _device?.disconnect(); } catch (_) {}
    _handleDisconnect();
  }

  // ── Incoming notifications ────────────────────────────────────────────────

  void _onNotify(List<int> data) {
    if (data.isEmpty) return;
    _rxBuf = Uint8List.fromList([..._rxBuf, ...data]);
    while (_rxBuf.contains(0x0A)) { // '\n'
      final idx = _rxBuf.indexOf(0x0A);
      var lineBytes = _rxBuf.sublist(0, idx);
      _rxBuf = _rxBuf.sublist(idx + 1);
      // Strip trailing '\r'
      if (lineBytes.isNotEmpty && lineBytes.last == 0x0D) {
        lineBytes = lineBytes.sublist(0, lineBytes.length - 1);
      }
      final line = utf8.decode(lineBytes, allowMalformed: true);
      _onLine(line);
    }
  }

  void _onLine(String line) {
    if (line.startsWith('#')) {
      final space = line.indexOf(' ');
      if (space < 0) return;
      final idStr = line.substring(1, space);
      final id = int.tryParse(idStr);
      if (id == null) return;
      final cmd = _pending[id];
      if (cmd != null) {
        cmd.lines.add(line);
        final rest = line.substring(space + 1);
        if (rest.startsWith('OK') || rest.startsWith('ERR')) {
          _pending.remove(id);
          if (!cmd.completer.isCompleted) cmd.completer.complete(cmd.lines);
        }
      }
    } else {
      _telemetryCtrl.add(line);
    }
  }

  // ── Request ───────────────────────────────────────────────────────────────

  Future<(bool, List<String>)> _request(String verb,
      {String payload = '', Duration? timeout}) async {
    if (!_connected || _rxChar == null) return (false, ['not connected']);

    _reqId = (_reqId % 65535) + 1;
    final rid = _reqId;

    final cmd = _PendingCmd(rid);
    _pending[rid] = cmd;

    final cmdStr = payload.isEmpty ? '#$rid $verb\r\n' : '#$rid $verb $payload\r\n';
    final data   = ascii.encode(cmdStr);
    final chunk  = (_mtu - 3).clamp(20, 244);

    try {
      for (int i = 0; i < data.length; i += chunk) {
        final end = (i + chunk).clamp(0, data.length);
        await _rxChar!.write(data.sublist(i, end), withoutResponse: true);
        if (data.length > chunk) {
          await Future.delayed(const Duration(milliseconds: 10));
        }
      }
    } catch (e) {
      _pending.remove(rid);
      return (false, [e.toString()]);
    }

    try {
      final lines = await cmd.completer.future
          .timeout(timeout ?? _defaultTimeout);
      // Parse lines for OK/ERR
      final prefix = '#$rid ';
      for (final l in lines) {
        if (l.startsWith(prefix)) {
          final rest = l.substring(prefix.length);
          if (rest.startsWith('OK'))  return (true,  lines);
          if (rest.startsWith('ERR')) return (false, lines);
        }
      }
      return (true, lines);
    } on TimeoutException {
      _pending.remove(rid);
      return (false, ['timeout']);
    } catch (e) {
      _pending.remove(rid);
      return (false, [e.toString()]);
    }
  }

  // ── Binary CMD layer ──────────────────────────────────────────────────────

  Future<CmdResult> sendCmd(int opcode, {Uint8List? payload,
      Duration? timeout}) async {
    final payloadHex = (payload == null || payload.isEmpty)
        ? '' : _bytesToHex(payload);
    final cmdHex  = opcode.toRadixString(16).padLeft(2, '0').toUpperCase() + payloadHex;
    final (ok, lines) = await _request('CMD', payload: cmdHex, timeout: timeout);
    if (!ok) return (rc: 0xFF, data: Uint8List(0));
    for (final l in lines) {
      if (l.contains(' RSP ')) {
        final hexPart = l.substring(l.indexOf(' RSP ') + 5).trim();
        try {
          final raw = _hexToBytes(hexPart);
          if (raw.isEmpty) return (rc: cmdOk, data: Uint8List(0));
          return (rc: raw[0], data: Uint8List.fromList(raw.sublist(1)));
        } catch (_) {}
      }
    }
    return (rc: cmdOk, data: Uint8List(0));
  }

  // Parse RSP line properly — find the line starting with '#<id> RSP '
  Future<CmdResult> _sendCmdParsed(int opcode, {Uint8List? payload,
      Duration? timeout}) async {
    if (!_connected || _rxChar == null) return (rc: 0xFF, data: Uint8List(0));

    _reqId = (_reqId % 65535) + 1;
    final rid = _reqId;
    final cmd = _PendingCmd(rid);
    _pending[rid] = cmd;

    final payloadHex = (payload == null || payload.isEmpty)
        ? '' : _bytesToHex(payload);
    final cmdHex   = opcode.toRadixString(16).padLeft(2, '0').toUpperCase() + payloadHex;
    final cmdStr   = '#$rid CMD $cmdHex\r\n';
    final data     = ascii.encode(cmdStr);
    final chunk    = (_mtu - 3).clamp(20, 244);

    try {
      for (int i = 0; i < data.length; i += chunk) {
        final end = (i + chunk).clamp(0, data.length);
        await _rxChar!.write(data.sublist(i, end), withoutResponse: true);
        if (data.length > chunk) await Future.delayed(const Duration(milliseconds: 10));
      }
    } catch (e) {
      _pending.remove(rid);
      return (rc: 0xFF, data: Uint8List(0));
    }

    try {
      final lines = await cmd.completer.future.timeout(timeout ?? _defaultTimeout);
      final prefix = '#$rid RSP ';
      for (final l in lines) {
        if (l.startsWith(prefix)) {
          final hexPart = l.substring(prefix.length).trim();
          try {
            final raw = _hexToBytes(hexPart);
            if (raw.isEmpty) return (rc: cmdOk, data: Uint8List(0));
            return (rc: raw[0], data: Uint8List.fromList(raw.sublist(1)));
          } catch (_) {}
        }
      }
      return (rc: cmdOk, data: Uint8List(0));
    } on TimeoutException {
      _pending.remove(rid);
      return (rc: 0xFF, data: Uint8List(0));
    } catch (_) {
      _pending.remove(rid);
      return (rc: 0xFF, data: Uint8List(0));
    }
  }

  /// Called after every successful command response (rc != 0xFF).
  /// BleProvider wires this to its inactivity-timer reset.
  void Function()? onActivity;

  /// When true, successful commands do NOT call onActivity.
  /// Used for background status polls so they don't prevent auto-disconnect.
  bool suppressActivity = false;

  // Use _sendCmdParsed as the canonical sendCmd
  Future<CmdResult> cmd(int opcode,
      {Uint8List? payload, Duration? timeout}) async {
    final r = await _sendCmdParsed(opcode, payload: payload, timeout: timeout);
    if (!suppressActivity && r.rc != 0xFF) onActivity?.call();
    return r;
  }

  // ── High-level commands ───────────────────────────────────────────────────

  Future<CmdResult> cmdPing() => cmd(opPing);

  Future<int?> cmdTimeGet() async {
    final r = await cmd(opTimeGet);
    if (r.rc == cmdOk && r.data.length >= 4) {
      return ByteData.sublistView(r.data).getUint32(0, Endian.little);
    }
    return null;
  }

  Future<int> cmdTimeSet(int unixTs) async {
    final bd = ByteData(4)..setUint32(0, unixTs & 0xFFFFFFFF, Endian.little);
    final r = await cmd(opTimeSet, payload: bd.buffer.asUint8List());
    return r.rc;
  }

  Future<int> cmdReboot({int mode = 0}) async {
    final r = await cmd(opReboot, payload: Uint8List.fromList([mode]));
    return r.rc;
  }

  Future<Uint8List> cmdConfigRead() async {
    // Read 64 bytes via OP_CONFIG_READ(offset=0, len=64)
    final bd = ByteData(3);
    bd.setUint16(0, 0, Endian.little);
    bd.setUint8(2, 64);
    final r = await cmd(opConfigRead, payload: bd.buffer.asUint8List());
    return r.rc == cmdOk ? r.data : Uint8List(0);
  }

  Future<int> cmdConfigWrite(int offset, Uint8List data) async {
    final hdr = ByteData(3);
    hdr.setUint16(0, offset & 0xFFFF, Endian.little);
    hdr.setUint8(2, data.length);
    final payload = Uint8List.fromList([...hdr.buffer.asUint8List(), ...data]);
    final r = await cmd(opConfigWrite, payload: payload);
    return r.rc;
  }

  Future<int> cmdConfigCommit() async {
    final bd = ByteData(2)..setUint16(0, 0, Endian.little);
    final r = await cmd(opConfigCommit,
        payload: bd.buffer.asUint8List(), timeout: _commitTimeout);
    return r.rc;
  }

  /// Write full ConfigBlob (64 bytes) then commit.
  Future<bool> writeConfig(ConfigBlob cfg) async {
    final raw = cfg.toBytes();
    final rc1 = await cmdConfigWrite(0, raw);
    if (rc1 != cmdOk) return false;
    final rc2 = await cmdConfigCommit();
    return rc2 == cmdOk;
  }

  Future<ConfigBlob?> readConfig() async {
    final raw = await cmdConfigRead();
    if (raw.length < 64) return null;
    final c = ConfigBlob.fromBytes(Uint8List.fromList(raw.sublist(0, 64)));
    return c.verifyCrc() ? c : null;
  }

  Future<StatusBlob?> readStatus() async {
    final r = await cmd(opMeasureAll);
    if (r.rc == cmdOk && r.data.length >= 24) return StatusBlob.fromBytes(r.data);
    return null;
  }

  Future<InfoBlob?> readInfo() async {
    final r = await cmd(opConfigInfo);
    if (r.rc != cmdOk) return null;
    // OP_CONFIG_INFO returns config size/version, not InfoBlob.
    // InfoBlob is in INFO? verb (UART) — over BLE we use OP_CONFIG_INFO result
    // and a separate OP_PING for fw version. For InfoBlob equivalent:
    // The firmware exposes it via the text verb "INFO?" (UART path).
    // Over NUS we send the text command directly.
    return await _readInfoVerb();
  }

  Future<InfoBlob?> _readInfoVerb() async {
    final (ok, lines) = await _request('INFO?');
    if (!ok || lines.isEmpty) return null;
    for (final l in lines) {
      if (l.contains(' ') && !l.startsWith('#')) continue;
      final prefix = '#${_reqId} ';
      if (!l.startsWith(prefix)) {
        // data line without prefix
        try {
          final hex = l.trim();
          final raw = _hexToBytes(hex);
          if (raw.length >= 48) return InfoBlob.fromBytes(Uint8List.fromList(raw));
        } catch (_) {}
      } else {
        final rest = l.substring(prefix.length);
        if (!rest.startsWith('OK') && !rest.startsWith('ERR')) {
          try {
            final hex = rest.contains(' ') ? rest.split(' ')[1] : rest;
            final raw = _hexToBytes(hex);
            if (raw.length >= 48) return InfoBlob.fromBytes(Uint8List.fromList(raw));
          } catch (_) {}
        }
      }
    }
    return null;
  }

  Future<Map<String, int>?> cmdLogInfo() async {
    final r = await cmd(opLogInfo);
    if (r.rc != cmdOk || r.data.length < 18) return null;
    final bd = ByteData.sublistView(r.data);
    return {
      'total':    bd.getUint32(0,  Endian.little),
      'used':     bd.getUint32(4,  Endian.little),
      'head':     bd.getUint32(8,  Endian.little),
      'tail':     bd.getUint32(12, Endian.little),
      'rec_size': r.data[16],
      'fmt_ver':  r.data[17],
    };
  }

  Future<Uint8List> cmdLogRead(int offset, int count) async {
    final bd = ByteData(8);
    bd.setUint32(0, offset & 0xFFFFFFFF, Endian.little);
    bd.setUint32(4, count  & 0xFFFFFFFF, Endian.little);
    final r = await cmd(opLogRead,
        payload: bd.buffer.asUint8List(), timeout: _logTimeout);
    return r.rc == cmdOk ? r.data : Uint8List(0);
  }

  Future<int> cmdLogErase() async {
    final bd = ByteData(4)..setUint32(0, 0xCA11AB1E, Endian.little);
    final r = await cmd(opLogErase,
        payload: bd.buffer.asUint8List(), timeout: _eraseTimeout);
    return r.rc;
  }

  Future<int> cmdLogMark({int tag = 0}) async {
    final r = await cmd(opLogMark, payload: Uint8List.fromList([tag]));
    return r.rc;
  }

  Future<BleSettings?> cmdBleGet() async {
    final r = await cmd(opBleGet);
    if (r.rc != cmdOk || r.data.length < bleCmdPayloadSz) return null;
    return BleSettings.fromBytes(r.data);
  }

  Future<int> cmdBleSet(BleSettings s) async {
    final r = await cmd(opBleSet,
        payload: s.toBytes(), timeout: const Duration(seconds: 8));
    return r.rc;
  }

  Future<int?> cmdBleRssi() async {
    final r = await cmd(opBleRssi, timeout: const Duration(seconds: 2));
    if (r.rc != cmdOk || r.data.isEmpty) return null;
    // int8 dBm
    return r.data[0] >= 128 ? r.data[0] - 256 : r.data[0];
  }

  Future<List<Map<String, dynamic>>> cmdSensorList() async {
    final r = await cmd(opSensorList);
    if (r.rc != cmdOk || r.data.isEmpty) return [];
    final n = r.data[0];
    final sensors = <Map<String, dynamic>>[];
    int off = 1;
    for (int i = 0; i < n; i++) {
      if (off + 7 > r.data.length) break;
      final bd = ByteData.sublistView(r.data, off + 3, off + 7);
      sensors.add({
        'id':       r.data[off],
        'present':  r.data[off + 1],
        'enabled':  r.data[off + 2],
        'interval_s': bd.getUint32(0, Endian.little),
        'has_ext':  r.data[off + 7],
      });
      off += 8;
    }
    return sensors;
  }

  Future<int> cmdSensorEnable(int sensorId, bool enable) async {
    final r = await cmd(opSensorEnable,
        payload: Uint8List.fromList([sensorId, enable ? 1 : 0]));
    return r.rc;
  }

  Future<int> cmdSensorInterval(int sensorId, int intervalS) async {
    final bd = ByteData(4)..setUint32(0, intervalS & 0xFFFFFFFF, Endian.little);
    final r = await cmd(opSensorInterval,
        payload: Uint8List.fromList([sensorId, ...bd.buffer.asUint8List()]));
    return r.rc;
  }

  Future<Map<String, dynamic>?> cmdWakeCfgGet() async {
    final r = await cmd(opWakeCfgGet);
    if (r.rc != cmdOk || r.data.length < 6) return null;
    final bd = ByteData.sublistView(r.data);
    return {
      'enable':       r.data[0],
      'threshold_mg': bd.getUint16(1, Endian.little),
      'duration_ms':  bd.getUint16(3, Endian.little),
      'action':       r.data[5],
    };
  }

  Future<int> cmdWakeCfgSet({required int enable, required int thresholdMg,
      required int durationMs, required int action}) async {
    final bd = ByteData(5);
    bd.setUint8(0, enable & 0xFF);
    bd.setUint16(1, thresholdMg & 0xFFFF, Endian.little);
    bd.setUint16(3, durationMs  & 0xFFFF, Endian.little);
    // action at byte 5 — need 6-byte payload
    final payload = Uint8List(6);
    payload.setAll(0, bd.buffer.asUint8List());
    payload[5] = action & 0xFF;
    final r = await cmd(opWakeCfgSet, payload: payload);
    return r.rc;
  }

  Future<Map<String, dynamic>?> cmdWakeStatus() async {
    final r = await cmd(opWakeStatus);
    if (r.rc != cmdOk || r.data.length < 9) return null;
    final bd = ByteData.sublistView(r.data);
    return {
      'armed':   r.data[0],
      'count':   bd.getUint32(1, Endian.little),
      'last_ts': bd.getUint32(5, Endian.little),
    };
  }

  Future<int> cmdWakeClear() async {
    final r = await cmd(opWakeClear);
    return r.rc;
  }

  // OP_UART_MODE is a no-op over BLE
  Future<int> cmdUartMode(int mode) async => cmdOk;

  // ── Helpers ───────────────────────────────────────────────────────────────

  static String _bytesToHex(Uint8List bytes) =>
      bytes.map((b) => b.toRadixString(16).padLeft(2, '0').toUpperCase()).join();

  static List<int> _hexToBytes(String hex) {
    final clean = hex.replaceAll(RegExp(r'\s'), '');
    if (clean.length.isOdd) return [];
    return List.generate(clean.length ~/ 2,
        (i) => int.parse(clean.substring(i * 2, i * 2 + 2), radix: 16));
  }

  void dispose() {
    _notifySub?.cancel();
    _telemetryCtrl.close();
    _connCtrl.close();
  }
}

class _PendingCmd {
  final int id;
  final Completer<List<String>> completer = Completer();
  final List<String> lines = [];
  _PendingCmd(this.id);
}
