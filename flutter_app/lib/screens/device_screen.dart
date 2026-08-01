import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../ble_service.dart';
import '../models/beacon_data.dart';

class DeviceScreen extends StatefulWidget {
  const DeviceScreen({super.key, required this.bleService});

  final BleService bleService;

  @override
  State<DeviceScreen> createState() => _DeviceScreenState();
}

class _DeviceScreenState extends State<DeviceScreen> {
  late bool _connected;
  StreamSubscription<bool>? _connSub;

  final Map<Guid, String> _values = {};
  final Map<Guid, TextEditingController> _writeControllers = {};
  final Map<Guid, StreamSubscription<List<int>>> _notifySubs = {};
  String? _error;

  @override
  void initState() {
    super.initState();
    _connected = widget.bleService.isConnected;
    _connSub = widget.bleService.connectionStateStream.listen((v) {
      if (mounted) {
        setState(() {
          _connected = v;
          if (!v) {
            _values.clear();
            _error = null;
            for (final s in _notifySubs.values) s.cancel();
            _notifySubs.clear();
          }
        });
      }
    });
  }

  @override
  void dispose() {
    _connSub?.cancel();
    for (final c in _writeControllers.values) c.dispose();
    for (final s in _notifySubs.values) s.cancel();
    super.dispose();
  }

  Future<void> _disconnect() async {
    await widget.bleService.disconnect();
  }

  Future<void> _read(BluetoothCharacteristic c) async {
    try {
      final v = await widget.bleService.read(c);
      setState(() {
        _values[c.uuid] = BeaconData.bytesToHex(v);
        _error = null;
      });
    } catch (e) {
      setState(() => _error = e.toString());
    }
  }

  Future<void> _write(BluetoothCharacteristic c) async {
    try {
      final text = _writeControllers[c.uuid]?.text ?? '';
      await widget.bleService.writeHex(
        c,
        text,
        withoutResponse: c.properties.writeWithoutResponse,
      );
      setState(() => _error = null);
    } catch (e) {
      setState(() => _error = e.toString());
    }
  }

  Future<void> _toggleNotify(BluetoothCharacteristic c, bool enabled) async {
    try {
      await widget.bleService.subscribe(c, enabled);
      if (enabled) {
        _notifySubs[c.uuid]?.cancel();
        _notifySubs[c.uuid] = c.onValueReceived.listen((v) {
          if (mounted) {
            setState(() => _values[c.uuid] = BeaconData.bytesToHex(v));
          }
        });
      } else {
        await _notifySubs[c.uuid]?.cancel();
        _notifySubs.remove(c.uuid);
      }
      setState(() => _error = null);
    } catch (e) {
      setState(() => _error = e.toString());
    }
  }

  @override
  Widget build(BuildContext context) {
    if (!_connected) return const _DemoDeviceView();

    final device = widget.bleService.connectedDevice!;
    final name =
        device.platformName.isNotEmpty ? device.platformName : 'BCN_TEST';

    return Scaffold(
      appBar: AppBar(
        title: Text(name),
        actions: [
          TextButton.icon(
            onPressed: _disconnect,
            icon: const Icon(Icons.bluetooth_disabled, size: 18),
            label: const Text('Disconnect'),
          ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Row(
            children: [
              const Icon(Icons.circle, color: Colors.green, size: 12),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  device.remoteId.str,
                  style: const TextStyle(fontSize: 13, color: Colors.grey),
                ),
              ),
              const Text(
                'Connected',
                style: TextStyle(color: Colors.green, fontSize: 13),
              ),
            ],
          ),
          const SizedBox(height: 4),
          const Divider(),
          if (_error != null) ...[
            const SizedBox(height: 8),
            Text(_error!, style: const TextStyle(color: Colors.redAccent)),
          ],
          for (final service in widget.bleService.services) ...[
            const SizedBox(height: 20),
            _ServiceHeader(uuid: service.uuid),
            const SizedBox(height: 8),
            for (final c in service.characteristics)
              _CharacteristicTile(
                characteristic: c,
                value: _values[c.uuid],
                controller: _writeControllers.putIfAbsent(
                  c.uuid,
                  TextEditingController.new,
                ),
                subscribed: _notifySubs.containsKey(c.uuid),
                onRead: () => _read(c),
                onWrite: () => _write(c),
                onSubscribe: (v) => _toggleNotify(c, v),
              ),
          ],
          const SizedBox(height: 24),
        ],
      ),
    );
  }
}

// ── Demo view ────────────────────────────────────────────────────────────────

class _DemoDeviceView extends StatelessWidget {
  const _DemoDeviceView();

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Row(
          children: [
            Text('Device'),
            SizedBox(width: 8),
            _DemoChip(),
          ],
        ),
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _infoBox(
            'Go to Scan tab → connect to BCN_TEST.\n'
            'This tab shows the live GATT structure.',
          ),
          const SizedBox(height: 20),
          const Text(
            'Expected GATT services:',
            style: TextStyle(color: Colors.grey, fontSize: 13),
          ),
          const SizedBox(height: 12),
          ..._buildDemoServices(context),
        ],
      ),
    );
  }

  static Widget _infoBox(String text) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: Colors.amber.withOpacity(0.12),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Colors.amber.withOpacity(0.35)),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Icon(Icons.info_outline, color: Colors.amber, size: 20),
          const SizedBox(width: 10),
          Expanded(child: Text(text, style: const TextStyle(height: 1.5))),
        ],
      ),
    );
  }

  static List<Widget> _buildDemoServices(BuildContext context) {
    const services = [
      (
        name: 'P2P Service',
        uuid: '0000FE40-CC7A-482A-984A-7F2ED5B3E58F',
        chars: [
          (name: 'LED', props: 'Read + Write Without Response', demo: '01 00 → OFF  |  01 01 → ON'),
          (name: 'Switch Notify', props: 'Notify', demo: '00 01 = pressed, 00 00 = released'),
          (name: 'Long Notify', props: 'Notify', demo: 'up to 300 bytes'),
        ],
      ),
      (
        name: 'Heart Rate Service  (0x180D)',
        uuid: 'Standard BLE HRS',
        chars: [
          (name: 'HR Measurement', props: 'Notify', demo: '1F 41 00 0A 00 00 04 — fake HR ~65 bpm'),
          (name: 'Body Sensor Location', props: 'Read', demo: '04 = Hand'),
          (name: 'HR Control Point', props: 'Write', demo: '01 = reset energy expended'),
        ],
      ),
      (
        name: 'BLE Sensor Service',
        uuid: '00000000-0001-11E1-9AB4-0002A5D5C51B',
        chars: [
          (name: 'Motion (Acc+Gyro)', props: 'Notify  14 bytes', demo: 'ISM330DHCX  @20 Hz'),
          (name: 'Environment (Temp)', props: 'Read + Notify  4 bytes', demo: '[ts:2B][temp×10:2B]  STTS22H'),
        ],
      ),
    ];

    return services.expand((s) sync* {
      yield Padding(
        padding: const EdgeInsets.only(bottom: 6),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              s.name,
              style: const TextStyle(
                fontWeight: FontWeight.bold,
                color: Color(0xff00a39b),
              ),
            ),
            Text(
              s.uuid,
              style: const TextStyle(color: Colors.grey, fontSize: 11),
            ),
          ],
        ),
      );
      for (final c in s.chars) {
        yield Card(
          margin: const EdgeInsets.only(bottom: 8),
          child: ListTile(
            dense: true,
            title: Text(c.name),
            subtitle: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  c.props,
                  style: const TextStyle(color: Colors.grey, fontSize: 11),
                ),
                Text(
                  c.demo,
                  style: const TextStyle(
                    color: Color(0xff00a39b),
                    fontSize: 11,
                  ),
                ),
              ],
            ),
          ),
        );
      }
      yield const SizedBox(height: 12);
    }).toList();
  }
}

// ── Supporting widgets ────────────────────────────────────────────────────────

class _DemoChip extends StatelessWidget {
  const _DemoChip();

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 2),
      decoration: BoxDecoration(
        color: Colors.amber.withOpacity(0.2),
        borderRadius: BorderRadius.circular(4),
      ),
      child: const Text(
        'DEMO',
        style: TextStyle(color: Colors.amber, fontSize: 11),
      ),
    );
  }
}

class _ServiceHeader extends StatelessWidget {
  const _ServiceHeader({required this.uuid});
  final Guid uuid;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          _nameFor(uuid),
          style: Theme.of(context)
              .textTheme
              .titleMedium
              ?.copyWith(color: const Color(0xff00a39b)),
        ),
        Text(
          uuid.str,
          style: const TextStyle(color: Colors.grey, fontSize: 11),
        ),
      ],
    );
  }

  String _nameFor(Guid uuid) {
    if (uuid == BleUuids.p2pService) return 'P2P Service';
    if (uuid == BleUuids.heartRateService) return 'Heart Rate Service';
    if (uuid == BleUuids.sensorService) return 'BLE Sensor Service';
    return 'Service';
  }
}

class _CharacteristicTile extends StatelessWidget {
  const _CharacteristicTile({
    required this.characteristic,
    required this.value,
    required this.controller,
    required this.subscribed,
    required this.onRead,
    required this.onWrite,
    required this.onSubscribe,
  });

  final BluetoothCharacteristic characteristic;
  final String? value;
  final TextEditingController controller;
  final bool subscribed;
  final VoidCallback onRead;
  final VoidCallback onWrite;
  final ValueChanged<bool> onSubscribe;

  @override
  Widget build(BuildContext context) {
    final props = characteristic.properties;

    return Card(
      margin: const EdgeInsets.only(bottom: 10),
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text(
              _nameFor(characteristic.uuid),
              style: const TextStyle(fontWeight: FontWeight.w600),
            ),
            Text(
              characteristic.uuid.str,
              style: const TextStyle(color: Colors.grey, fontSize: 11),
            ),
            const SizedBox(height: 8),
            Wrap(
              spacing: 8,
              runSpacing: 4,
              children: [
                if (props.read)
                  OutlinedButton(onPressed: onRead, child: const Text('Read')),
                if (props.notify || props.indicate)
                  FilterChip(
                    label: const Text('Notify'),
                    selected: subscribed,
                    onSelected: onSubscribe,
                    visualDensity: VisualDensity.compact,
                  ),
              ],
            ),
            if (props.write || props.writeWithoutResponse) ...[
              const SizedBox(height: 8),
              TextField(
                controller: controller,
                decoration: const InputDecoration(
                  labelText: 'HEX bytes',
                  hintText: '01 01',
                  border: OutlineInputBorder(),
                  contentPadding:
                      EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                ),
              ),
              const SizedBox(height: 6),
              OutlinedButton(onPressed: onWrite, child: const Text('Write')),
            ],
            if (value != null) ...[
              const SizedBox(height: 8),
              SelectableText(
                value!,
                style:
                    const TextStyle(fontFamily: 'monospace', fontSize: 13),
              ),
            ],
          ],
        ),
      ),
    );
  }

  String _nameFor(Guid uuid) {
    if (uuid == BleUuids.led) return 'LED';
    if (uuid == BleUuids.switchNotify) return 'Switch Notify';
    if (uuid == BleUuids.longNotify) return 'Long Notify';
    if (uuid == BleUuids.heartRateMeasurement) return 'Heart Rate Measurement';
    if (uuid == BleUuids.bodySensorLocation) return 'Body Sensor Location';
    if (uuid == BleUuids.heartRateControlPoint) return 'Heart Rate Control Point';
    if (uuid == BleUuids.motion) return 'Motion (Acc+Gyro)';
    if (uuid == BleUuids.environment) return 'Environment (Temp)';
    return 'Characteristic';
  }
}
