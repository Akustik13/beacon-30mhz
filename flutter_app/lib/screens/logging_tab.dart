import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import '../protocol/opcodes.dart';

class LoggingTab extends StatefulWidget {
  const LoggingTab({super.key});
  @override
  State<LoggingTab> createState() => _LoggingTabState();
}

class _LoggingTabState extends State<LoggingTab> {
  // Sensor interval form (seconds)
  final Map<int, int> _intervals = {
    sensorIdTemp:  60,
    sensorIdLight: 60,
    sensorIdBatt:  3600,
    sensorIdAccel: 60,
  };
  final Map<int, bool> _enabled = {
    sensorIdTemp:  true,
    sensorIdLight: false,
    sensorIdBatt:  true,
    sensorIdAccel: false,
  };
  bool _localDirty = false;

  static const _sensorNames = {
    sensorIdTemp:  'Temperature',
    sensorIdLight: 'Light',
    sensorIdBatt:  'Battery',
    sensorIdAccel: 'Accelerometer',
  };

  static const _sensorIcons = {
    sensorIdTemp:  Icons.thermostat,
    sensorIdLight: Icons.light_mode_outlined,
    sensorIdBatt:  Icons.battery_3_bar,
    sensorIdAccel: Icons.vibration,
  };

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final beacon = context.watch<BeaconProvider>();
    if (beacon.sensors.isNotEmpty && !_localDirty) {
      _loadFromBeacon(beacon);
    }
    if (!context.watch<BleProvider>().isConnected) _localDirty = false;
  }

  void _loadFromBeacon(BeaconProvider beacon) {
    for (final id in _intervals.keys) {
      final s = beacon.sensorById(id);
      if (s != null) {
        _intervals[id] = (s['interval_s'] ?? _intervals[id]) as int;
        _enabled[id]   = ((s['enabled'] ?? 0) as int) == 1;
      }
    }
    setState(() {});
  }

  double _memoryDepthS(BeaconProvider beacon) {
    double writesPerS = 0;
    for (final id in _intervals.keys) {
      if (_enabled[id] == true) {
        final iv = _intervals[id]!;
        if (iv > 0) writesPerS += 1.0 / iv;
      }
    }
    if (writesPerS == 0) return double.infinity;
    final total = beacon.logTotal > 0 ? beacon.logTotal : 4096;
    final used  = beacon.logUsed;
    final free  = (total - used).clamp(0, total);
    return free / writesPerS;
  }

  Future<void> _applyAll() async {
    final beacon = context.read<BeaconProvider>();
    bool anyFail = false;
    for (final id in _intervals.keys) {
      final en  = _enabled[id] == true;
      final okE = await beacon.setSensorEnabled(id, en);
      if (!okE) { anyFail = true; continue; }
      if (en) {
        final okI = await beacon.setSensorInterval(id, _intervals[id]!);
        if (!okI) anyFail = true;
      }
    }
    if (mounted) {
      setState(() => _localDirty = false);
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text(anyFail ? 'Some settings failed to apply' : 'Logging config applied'),
        backgroundColor: anyFail ? Colors.orange : Colors.green,
      ));
    }
  }

  @override
  Widget build(BuildContext context) {
    final ble    = context.watch<BleProvider>();
    final beacon = context.watch<BeaconProvider>();

    if (!ble.isConnected) {
      return const Scaffold(body: Center(child: Text('Connect to a beacon first')));
    }

    final depthS = _memoryDepthS(beacon);
    final depthStr = depthS.isInfinite ? '∞'
        : depthS < 3600 ? '${depthS.round()}s'
        : depthS < 86400 ? '${(depthS / 3600).toStringAsFixed(1)}h'
        : '${(depthS / 86400).toStringAsFixed(1)}d';

    return Scaffold(
      appBar: AppBar(
        title: const Text('Logging'),
        actions: [
          if (beacon.isBusy)
            const Padding(padding: EdgeInsets.all(16),
              child: SizedBox(width: 20, height: 20,
                child: CircularProgressIndicator(strokeWidth: 2))),
        ],
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(12),
        child: Column(children: [
          // Memory depth indicator
          Card(
            child: Padding(
              padding: const EdgeInsets.all(14),
              child: Row(children: [
                const Icon(Icons.memory, size: 32, color: Colors.indigo),
                const SizedBox(width: 12),
                Expanded(child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text('Memory Depth', style: TextStyle(fontWeight: FontWeight.w600)),
                    Text('Approx. $depthStr of free storage with current settings',
                        style: Theme.of(context).textTheme.bodySmall),
                    Text('${beacon.logUsed} / ${beacon.logTotal} records used',
                        style: Theme.of(context).textTheme.labelSmall),
                  ],
                )),
              ]),
            ),
          ),
          const SizedBox(height: 8),

          // Per-sensor settings
          ..._intervals.keys.map((id) => _SensorCard(
            id:       id,
            name:     _sensorNames[id]!,
            icon:     _sensorIcons[id]!,
            enabled:  _enabled[id]!,
            interval: _intervals[id]!,
            onEnabledChanged: (v) => setState(() { _enabled[id] = v; _localDirty = true; }),
            onIntervalChanged: (v) => setState(() { _intervals[id] = v; _localDirty = true; }),
          )),

          const SizedBox(height: 12),
          FilledButton.icon(
            onPressed: (!beacon.isBusy && _localDirty) ? _applyAll : null,
            icon: const Icon(Icons.check),
            label: Text(_localDirty ? 'Apply' : 'No changes'),
          ),
          const SizedBox(height: 8),

          // Erase log button
          OutlinedButton.icon(
            style: OutlinedButton.styleFrom(foregroundColor: Colors.red),
            onPressed: beacon.isBusy ? null : () => _confirmErase(context, beacon),
            icon: const Icon(Icons.delete_sweep),
            label: const Text('Erase Log'),
          ),
        ]),
      ),
    );
  }

  void _confirmErase(BuildContext ctx, BeaconProvider beacon) {
    showDialog(
      context: ctx,
      builder: (_) => AlertDialog(
        title: const Text('Erase log?'),
        content: const Text('This will permanently delete all logged data.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
          FilledButton(
            style: FilledButton.styleFrom(backgroundColor: Colors.red),
            onPressed: () async {
              Navigator.pop(ctx);
              final ok = await beacon.eraseLog();
              if (mounted) {
                ScaffoldMessenger.of(context).showSnackBar(SnackBar(
                  content: Text(ok ? 'Log erased' : 'Erase failed'),
                  backgroundColor: ok ? Colors.green : Colors.red,
                ));
              }
            },
            child: const Text('Erase'),
          ),
        ],
      ),
    );
  }
}

class _SensorCard extends StatelessWidget {
  final int id;
  final String name;
  final IconData icon;
  final bool enabled;
  final int interval;
  final ValueChanged<bool> onEnabledChanged;
  final ValueChanged<int> onIntervalChanged;

  const _SensorCard({
    required this.id, required this.name, required this.icon,
    required this.enabled, required this.interval,
    required this.onEnabledChanged, required this.onIntervalChanged,
  });

  // Preset intervals for the dropdown
  static const _presets = [
    (10, '10 s'), (30, '30 s'), (60, '1 min'), (120, '2 min'),
    (300, '5 min'), (600, '10 min'), (1800, '30 min'), (3600, '1 h'),
    (7200, '2 h'), (21600, '6 h'), (86400, '24 h'),
  ];

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.symmetric(vertical: 4),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
        child: Column(
          children: [
            Row(children: [
              Icon(icon, size: 22),
              const SizedBox(width: 10),
              Expanded(child: Text(name,
                  style: const TextStyle(fontWeight: FontWeight.w600))),
              Switch(value: enabled, onChanged: onEnabledChanged),
            ]),
            if (enabled) ...[
              const SizedBox(height: 6),
              Row(children: [
                const SizedBox(width: 32),
                const Text('Interval: ', style: TextStyle(fontSize: 13)),
                DropdownButton<int>(
                  value: _nearestPreset(interval),
                  onChanged: (v) => onIntervalChanged(v!),
                  isDense: true,
                  items: _presets.map((p) => DropdownMenuItem(
                    value: p.$1, child: Text(p.$2))).toList(),
                ),
              ]),
            ],
          ],
        ),
      ),
    );
  }

  int _nearestPreset(int v) {
    int best = _presets.first.$1;
    int bestDiff = (v - best).abs();
    for (final p in _presets) {
      final d = (v - p.$1).abs();
      if (d < bestDiff) { best = p.$1; bestDiff = d; }
    }
    return best;
  }
}
