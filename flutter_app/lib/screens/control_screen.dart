import 'dart:async';
import 'dart:math';

import 'package:flutter/material.dart';

import '../ble_service.dart';
import '../models/beacon_data.dart';

class ControlScreen extends StatefulWidget {
  const ControlScreen({super.key, required this.bleService});

  final BleService bleService;

  @override
  State<ControlScreen> createState() => _ControlScreenState();
}

class _ControlScreenState extends State<ControlScreen> {
  late bool _connected;
  StreamSubscription<bool>? _connSub;

  bool _ledOn = false;
  String? _message;

  // demo
  double _demoTemp = 36.5;
  int _demoTs = 0;
  Timer? _demoTimer;
  final _rng = Random();

  @override
  void initState() {
    super.initState();
    _connected = widget.bleService.isConnected;
    _connSub = widget.bleService.connectionStateStream.listen((v) {
      if (mounted) setState(() { _connected = v; _message = null; });
    });
    _demoTimer = Timer.periodic(const Duration(seconds: 2), (_) {
      if (mounted && !_connected) {
        setState(() {
          _demoTemp = (_demoTemp + (_rng.nextDouble() - 0.5) * 0.4)
              .clamp(35.0, 38.5);
          _demoTs += 2;
        });
      }
    });
  }

  @override
  void dispose() {
    _connSub?.cancel();
    _demoTimer?.cancel();
    super.dispose();
  }

  Future<void> _setLed(bool value) async {
    if (!_connected) {
      setState(() { _ledOn = value; _message = 'Demo: LED ${value ? "ON" : "OFF"}'; });
      return;
    }
    try {
      await widget.bleService.writeLed(value);
      setState(() { _ledOn = value; _message = 'LED ${value ? "ON" : "OFF"} sent'; });
    } catch (e) {
      setState(() => _message = e.toString());
    }
  }

  Future<void> _sendTest() async {
    if (!_connected) {
      setState(() => _message = 'Demo: sent AA 55 (simulated)');
      return;
    }
    try {
      await widget.bleService.sendTestData();
      setState(() => _message = 'Sent: AA 55');
    } catch (e) {
      setState(() => _message = e.toString());
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Row(
          children: [
            const Text('Control'),
            if (!_connected) ...[
              const SizedBox(width: 8),
              _DemoChip(),
            ],
          ],
        ),
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            if (!_connected)
              _InfoBox(
                'Demo mode — no device connected.\nGo to Scan tab to connect to BCN_TEST.',
              ),
            const SizedBox(height: 12),

            // Temperature
            _TempCard(
              connected: _connected,
              bleService: widget.bleService,
              demoTemp: _demoTemp,
              demoTs: _demoTs,
            ),
            const SizedBox(height: 12),

            // Battery (VDD)
            _BatteryCard(
              connected: _connected,
              bleService: widget.bleService,
            ),
            const SizedBox(height: 12),

            // Accelerometer
            _AccelCard(
              connected: _connected,
              bleService: widget.bleService,
            ),
            const SizedBox(height: 12),

            // LED
            Card(
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                child: SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  secondary: Icon(
                    _ledOn ? Icons.lightbulb : Icons.lightbulb_outline,
                    color: _ledOn ? Colors.lightBlue : Colors.grey,
                    size: 32,
                  ),
                  title: Text(_ledOn ? 'LED  ON' : 'LED  OFF'),
                  subtitle: Text(
                    _connected
                        ? 'Writes [01 01] / [01 00] without response'
                        : 'Demo — no real write',
                    style: const TextStyle(fontSize: 12),
                  ),
                  value: _ledOn,
                  onChanged: _setLed,
                ),
              ),
            ),
            const SizedBox(height: 12),

            // Test data
            FilledButton.icon(
              onPressed: _sendTest,
              icon: const Icon(Icons.send),
              label: const Padding(
                padding: EdgeInsets.symmetric(vertical: 12),
                child: Text('Send Test Data  [AA 55]'),
              ),
            ),

            if (_message != null) ...[
              const SizedBox(height: 12),
              Text(
                _message!,
                style: TextStyle(
                  color: _message!.toLowerCase().contains('error') ||
                          _message!.toLowerCase().contains('not found')
                      ? Colors.redAccent
                      : Colors.lightGreen,
                  fontSize: 13,
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

// ── Sub-widgets ───────────────────────────────────────────────────────────────

class _TempCard extends StatelessWidget {
  const _TempCard({
    required this.connected,
    required this.bleService,
    required this.demoTemp,
    required this.demoTs,
  });

  final bool connected;
  final BleService bleService;
  final double demoTemp;
  final int demoTs;

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<BeaconData>(
      stream: bleService.beaconData,
      builder: (context, snapshot) {
        final live = snapshot.data;
        final tempStr = connected
            ? (live != null
                ? '${live.temperatureC.toStringAsFixed(1)} °C'
                : '-- °C')
            : '${demoTemp.toStringAsFixed(1)} °C';
        final subtitle = connected
            ? (live != null
                ? 'STTS22H  ts=${live.timestamp}  ${live.rawHex}'
                : 'Waiting for ENV notifications...')
            : 'STTS22H  simulated  ts=$demoTs';

        return Card(
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    const Icon(Icons.thermostat, color: Colors.orange, size: 20),
                    const SizedBox(width: 8),
                    const Text('Temperature'),
                    const Spacer(),
                    if (!connected) _DemoChip(),
                  ],
                ),
                const SizedBox(height: 12),
                Text(
                  tempStr,
                  style: Theme.of(context).textTheme.displaySmall,
                ),
                const SizedBox(height: 6),
                Text(
                  subtitle,
                  style: const TextStyle(color: Colors.grey, fontSize: 12),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _BatteryCard extends StatelessWidget {
  const _BatteryCard({required this.connected, required this.bleService});
  final bool connected;
  final BleService bleService;

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<BeaconData>(
      stream: bleService.beaconData,
      builder: (context, snapshot) {
        final batt = snapshot.data?.batteryMv;
        final String value;
        final String sub;
        if (!connected) {
          value = '3300 mV';
          sub = 'VDD via VREFINT ADC — demo';
        } else if (batt == null) {
          value = '-- mV';
          sub = 'Waiting for ENV notification…';
        } else {
          value = '$batt mV';
          sub = 'VDD via VREFINT ADC  (eval LDO ≈3300 mV)';
        }
        return Card(
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Icon(Icons.battery_charging_full, color: Colors.green[400], size: 20),
                    const SizedBox(width: 8),
                    const Text('Battery / VDD'),
                    const Spacer(),
                    if (!connected) _DemoChip(),
                  ],
                ),
                const SizedBox(height: 12),
                Text(value, style: Theme.of(context).textTheme.headlineMedium),
                const SizedBox(height: 6),
                Text(sub, style: const TextStyle(color: Colors.grey, fontSize: 12)),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _AccelCard extends StatelessWidget {
  const _AccelCard({required this.connected, required this.bleService});
  final bool connected;
  final BleService bleService;

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<AccelData>(
      stream: bleService.accelData,
      builder: (context, snapshot) {
        final a = snapshot.data;
        final String xStr, yStr, zStr, sub;
        if (!connected) {
          xStr = '0.00'; yStr = '0.00'; zStr = '1.00';
          sub = 'ISM330DHCX — demo';
        } else if (a == null) {
          xStr = '--'; yStr = '--'; zStr = '--';
          sub = 'Waiting for Motion notification…';
        } else {
          xStr = a.x.toStringAsFixed(3);
          yStr = a.y.toStringAsFixed(3);
          zStr = a.z.toStringAsFixed(3);
          sub = 'ISM330DHCX  ±4g  12.5Hz';
        }
        return Card(
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    const Icon(Icons.vibration, color: Colors.blueAccent, size: 20),
                    const SizedBox(width: 8),
                    const Text('Accelerometer'),
                    const Spacer(),
                    if (!connected) _DemoChip(),
                  ],
                ),
                const SizedBox(height: 12),
                _AxisRow('X', xStr),
                const SizedBox(height: 4),
                _AxisRow('Y', yStr),
                const SizedBox(height: 4),
                _AxisRow('Z', zStr),
                const SizedBox(height: 6),
                Text(sub, style: const TextStyle(color: Colors.grey, fontSize: 12)),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _AxisRow extends StatelessWidget {
  const _AxisRow(this.axis, this.value);
  final String axis;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        SizedBox(
          width: 20,
          child: Text(axis, style: const TextStyle(color: Colors.grey, fontWeight: FontWeight.bold)),
        ),
        const SizedBox(width: 8),
        Text('$value g', style: Theme.of(context).textTheme.titleLarge),
      ],
    );
  }
}

class _DemoChip extends StatelessWidget {
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

class _InfoBox extends StatelessWidget {
  const _InfoBox(this.text);
  final String text;

  @override
  Widget build(BuildContext context) {
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
          Expanded(
            child: Text(text, style: const TextStyle(height: 1.5)),
          ),
        ],
      ),
    );
  }
}
