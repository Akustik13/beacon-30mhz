import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import '../protocol/opcodes.dart';
import '../protocol/config_blob.dart';
import 'widgets/duration_input.dart';

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
  bool _localDirty    = false;
  bool _strategyDirty = false;

  // Write strategy (from config blob)
  int _logMode     = 0; // 0=always 1=on-change 2=adaptive
  int _logOverflow = 1; // 0=stop 1=circular
  int _logTsSource = 0; // 0=boot-seconds 1=RTC

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
    if (beacon.config != null && !_strategyDirty) {
      _loadStrategyFromConfig(beacon.config!);
    }
    if (!context.watch<BleProvider>().isConnected) {
      _localDirty    = false;
      _strategyDirty = false;
    }
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

  void _loadStrategyFromConfig(ConfigBlob cfg) {
    setState(() {
      _logMode     = cfg.logMode;
      _logOverflow = cfg.logOverflow;
      _logTsSource = cfg.logTsSource;
    });
  }

  static String _fmtDepth(double s) {
    if (s.isInfinite) return '∞';
    final sec = s.round();
    if (sec < 60)  return '$sec сек.';
    final m = sec ~/ 60;
    if (m < 60)    return '$m хв.';
    final h = m ~/ 60;
    final rm = m % 60;
    if (h < 24)    return rm > 0 ? '$h год. $rm хв.' : '$h год.';
    final d = h ~/ 24;
    final rh = h % 24;
    return rh > 0 ? '$d дн. $rh год.' : '$d дн.';
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
    final total    = beacon.logTotal > 0 ? beacon.logTotal : logEntriesMax;
    final used     = beacon.logUsed.clamp(0, total);
    final circular = _logOverflow == 1;
    // Circular: capacity = total (keeps last N seconds of history)
    // Stop:     capacity = remaining free slots only
    final capacity = circular ? total : (total - used).clamp(0, total);
    return capacity / writesPerS;
  }

  Future<void> _applyAll() async {
    final beacon = context.read<BeaconProvider>();
    bool anyFail = false;

    // Apply sensor intervals
    for (final id in _intervals.keys) {
      final en  = _enabled[id] == true;
      final okE = await beacon.setSensorEnabled(id, en);
      if (!okE) { anyFail = true; continue; }
      if (en) {
        final okI = await beacon.setSensorInterval(id, _intervals[id]!);
        if (!okI) anyFail = true;
      }
    }

    // Apply write strategy via config blob
    if (_strategyDirty) {
      final cfg = beacon.config;
      if (cfg != null) {
        cfg.logMode     = _logMode;
        cfg.logOverflow = _logOverflow;
        cfg.logTsSource = _logTsSource;
        final ok = await beacon.writeConfig(cfg);
        if (!ok) anyFail = true;
      }
    }

    if (mounted) {
      setState(() { _localDirty = false; _strategyDirty = false; });
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

    final depthS   = _memoryDepthS(beacon);
    final depthStr = _fmtDepth(depthS);
    final circular  = _logOverflow == 1;
    final total     = beacon.logTotal > 0 ? beacon.logTotal : logEntriesMax;
    final usedDisp  = beacon.logUsed.clamp(0, total);

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
        child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [

          // ── Offline banner ────────────────────────────────────────────────
          if (!ble.isConnected)
            Container(
              margin: const EdgeInsets.only(bottom: 10),
              padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
              decoration: BoxDecoration(
                color: Colors.orange.withOpacity(0.15),
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: Colors.orange.withOpacity(0.4)),
              ),
              child: const Row(children: [
                Icon(Icons.bluetooth_disabled, size: 16, color: Colors.orange),
                SizedBox(width: 8),
                Expanded(child: Text('Not connected — connect to apply changes',
                    style: TextStyle(fontSize: 13, color: Colors.orange))),
              ]),
            ),

          // Memory depth indicator
          Card(
            child: Padding(
              padding: const EdgeInsets.all(14),
              child: Column(children: [
                Row(children: [
                  Icon(circular ? Icons.loop : Icons.stop_circle_outlined,
                      size: 32,
                      color: circular ? Colors.orange : Colors.indigo),
                  const SizedBox(width: 12),
                  Expanded(child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(circular ? 'Memory Depth (circular)' : 'Memory Depth',
                          style: const TextStyle(fontWeight: FontWeight.w600)),
                      Text(
                        circular
                            ? 'Keeps last $depthStr of history — overwrites oldest'
                            : 'Fills in $depthStr, then stops recording',
                        style: Theme.of(context).textTheme.bodySmall,
                      ),
                      Text('$usedDisp / $total records used',
                          style: Theme.of(context).textTheme.labelSmall),
                    ],
                  )),
                ]),
                const SizedBox(height: 10),
                ClipRRect(
                  borderRadius: BorderRadius.circular(4),
                  child: LinearProgressIndicator(
                    value: total > 0 ? (usedDisp / total).clamp(0.0, 1.0) : 0.0,
                    minHeight: 7,
                    backgroundColor: Colors.grey.withValues(alpha: 0.2),
                    valueColor: AlwaysStoppedAnimation(
                      circular ? Colors.orange
                          : usedDisp / total.clamp(1, total) > 0.9 ? Colors.red
                          : Colors.indigo),
                  ),
                ),
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
            onEnabledChanged: (v) {
              HapticFeedback.lightImpact();
              setState(() { _enabled[id] = v; _localDirty = true; });
            },
            onIntervalChanged: (v) => setState(() { _intervals[id] = v; _localDirty = true; }),
          )),

          const SizedBox(height: 8),

          // Write strategy card
          Card(
            child: Padding(
              padding: const EdgeInsets.all(14),
              child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                const Row(children: [
                  Icon(Icons.tune, size: 18),
                  SizedBox(width: 8),
                  Text('Write strategy', style: TextStyle(fontWeight: FontWeight.w600)),
                ]),
                const SizedBox(height: 12),

                Text('Recording mode',
                    style: TextStyle(fontSize: 12, color: Colors.grey[600])),
                const SizedBox(height: 6),
                Wrap(spacing: 8, children: [
                  for (final e in const [
                    [0, 'Every interval'],
                    [1, 'On change'],
                    [2, 'Adaptive'],
                  ])
                    ChoiceChip(
                      label: Text(e[1] as String),
                      selected: _logMode == e[0] as int,
                      onSelected: (_) {
                        HapticFeedback.lightImpact();
                        setState(() {
                          _logMode       = e[0] as int;
                          _localDirty    = true;
                          _strategyDirty = true;
                        });
                      },
                    ),
                ]),

                const SizedBox(height: 12),
                Text('When memory is full',
                    style: TextStyle(fontSize: 12, color: Colors.grey[600])),
                const SizedBox(height: 6),
                Wrap(spacing: 8, children: [
                  ChoiceChip(
                    label: const Text('Stop recording'),
                    selected: _logOverflow == 0,
                    onSelected: (_) {
                      HapticFeedback.lightImpact();
                      setState(() {
                        _logOverflow   = 0;
                        _localDirty    = true;
                        _strategyDirty = true;
                      });
                    },
                  ),
                  ChoiceChip(
                    avatar: const Icon(Icons.loop, size: 14),
                    label: const Text('Circular overwrite'),
                    selected: _logOverflow == 1,
                    onSelected: (_) {
                      HapticFeedback.lightImpact();
                      setState(() {
                        _logOverflow   = 1;
                        _localDirty    = true;
                        _strategyDirty = true;
                      });
                    },
                  ),
                ]),

                const SizedBox(height: 12),
                Text('Timestamp source',
                    style: TextStyle(fontSize: 12, color: Colors.grey[600])),
                const SizedBox(height: 6),
                Wrap(spacing: 8, children: [
                  ChoiceChip(
                    label: const Text('Seconds from boot'),
                    selected: _logTsSource == 0,
                    onSelected: (_) {
                      HapticFeedback.lightImpact();
                      setState(() {
                        _logTsSource   = 0;
                        _localDirty    = true;
                        _strategyDirty = true;
                      });
                    },
                  ),
                  ChoiceChip(
                    avatar: const Icon(Icons.access_time, size: 14),
                    label: const Text('Real time (RTC)'),
                    selected: _logTsSource == 1,
                    onSelected: (_) {
                      HapticFeedback.lightImpact();
                      setState(() {
                        _logTsSource   = 1;
                        _localDirty    = true;
                        _strategyDirty = true;
                      });
                    },
                  ),
                ]),
              ]),
            ),
          ),

          const SizedBox(height: 12),
          FilledButton.icon(
            onPressed: (ble.isConnected && !beacon.isBusy && _localDirty)
                ? () { HapticFeedback.lightImpact(); _applyAll(); }
                : null,
            icon: const Icon(Icons.check),
            label: Text(!ble.isConnected ? 'Connect to apply'
                : _localDirty ? 'Apply' : 'No changes'),
          ),
          SizedBox(height: MediaQuery.of(context).padding.bottom + 24),
        ]),
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

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.symmetric(vertical: 4),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(children: [
              Icon(icon, size: 22),
              const SizedBox(width: 10),
              Expanded(child: Text(name,
                  style: const TextStyle(fontWeight: FontWeight.w600))),
              Switch(value: enabled, onChanged: onEnabledChanged),
            ]),
            if (enabled) ...[
              const SizedBox(height: 8),
              Row(children: [
                const SizedBox(width: 10),
                Text('Interval',
                    style: Theme.of(context).textTheme.bodySmall),
                const SizedBox(width: 12),
                DurationInput(
                  valueSeconds: interval,
                  minSeconds: 1,
                  maxSeconds: 86400,
                  onChanged: onIntervalChanged,
                ),
              ]),
            ],
          ],
        ),
      ),
    );
  }

}
