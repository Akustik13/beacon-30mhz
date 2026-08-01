import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/ble_provider.dart';
import '../providers/devices_provider.dart';
import '../providers/beacon_provider.dart';
import '../protocol/ble_settings.dart';
import '../protocol/opcodes.dart';

class DevicesTab extends StatefulWidget {
  const DevicesTab({super.key});
  @override
  State<DevicesTab> createState() => _DevicesTabState();
}

class _DevicesTabState extends State<DevicesTab>
    with SingleTickerProviderStateMixin {
  late final TabController _tc;
  bool _bleExpanded = false;
  bool _wakeExpanded = false;

  // BLE settings form state
  int _opMode = bleOpContinuous;
  int _txPower = 24;
  int _intervalS = 1800;
  int _durationSec = 60;
  int _advIntervalMs = 1000;
  int _nameMode = 0;
  String _name = '';
  int _ledMode = 0;

  // Wake config form state
  bool _wakeEnable = false;
  int  _threshMg  = 200;
  int  _durMs     = 500;
  int  _wakeAction = 0;

  @override
  void initState() {
    super.initState();
    _tc = TabController(length: 2, vsync: this);
    WidgetsBinding.instance.addPostFrameCallback((_) {
      context.read<DevicesProvider>().load();
    });
  }

  @override
  void dispose() {
    _tc.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Devices'),
        bottom: TabBar(
          controller: _tc,
          tabs: const [Tab(text: 'Scan'), Tab(text: 'My Beacons')],
        ),
      ),
      body: TabBarView(
        controller: _tc,
        children: [_ScanSection(parent: this), _SavedSection(parent: this)],
      ),
    );
  }

  void _loadBleSettings() {
    final s = context.read<BeaconProvider>().bleSettings;
    if (s == null) return;
    setState(() {
      _opMode = s.opMode; _txPower = s.txPower; _intervalS = s.intervalS;
      _durationSec = s.durationSec; _advIntervalMs = s.advIntervalMs;
      _nameMode = s.nameMode; _name = s.name; _ledMode = s.ledMode;
    });
  }

  void _loadWakeCfg() {
    final w = context.read<BeaconProvider>().wakeCfg;
    if (w == null) return;
    setState(() {
      _wakeEnable = (w['enable'] as int) != 0;
      _threshMg   = w['threshold_mg'] as int;
      _durMs      = w['duration_ms'] as int;
      _wakeAction = w['action'] as int;
    });
  }

  Future<void> _saveBleSettings() async {
    final beacon = context.read<BeaconProvider>();
    final s = BleSettings(
      opMode: _opMode, txPower: _txPower, intervalS: _intervalS,
      durationSec: _durationSec, advIntervalMs: _advIntervalMs,
      nameMode: _nameMode, name: _name, ledMode: _ledMode,
    );
    final ok = await beacon.writeBleSettings(s);
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text(ok ? 'BLE settings saved' : 'Failed to save BLE settings'),
        backgroundColor: ok ? Colors.green : Colors.red,
      ));
    }
  }

  Future<void> _saveWakeCfg() async {
    final beacon = context.read<BeaconProvider>();
    final ok = await beacon.writeWakeCfg({
      'enable': _wakeEnable ? 1 : 0,
      'threshold_mg': _threshMg,
      'duration_ms': _durMs,
      'action': _wakeAction,
    });
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text(ok ? 'Wake config saved' : 'Failed to save wake config'),
        backgroundColor: ok ? Colors.green : Colors.red,
      ));
    }
  }

  Widget buildBleSettings() {
    final connected = context.watch<BleProvider>().isConnected;
    return Card(
      child: ExpansionTile(
        leading: const Icon(Icons.settings_bluetooth),
        title: const Text('BLE Settings'),
        initiallyExpanded: _bleExpanded,
        onExpansionChanged: (v) {
          setState(() => _bleExpanded = v);
          if (v) _loadBleSettings();
        },
        children: [
          Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text('Operation Mode', style: TextStyle(fontWeight: FontWeight.w600)),
                const SizedBox(height: 8),
                Wrap(
                  spacing: 8,
                  children: [
                    _opChip('Off', bleOpOff),
                    _opChip('Continuous', bleOpContinuous),
                    _opChip('Schedule', bleOpSchedule),
                    _opChip('Gekon', bleOpGekon),
                  ],
                ),
                const SizedBox(height: 12),
                _labelSlider('TX Power', _txPower.toDouble(), 0, 31, 31,
                    (v) => setState(() => _txPower = v.round())),
                _labelSlider('Interval (s)', _intervalS.toDouble(), 60, 7200, 120,
                    (v) => setState(() => _intervalS = v.round())),
                _labelSlider('Duration (s)', _durationSec.toDouble(), 10, 600, 59,
                    (v) => setState(() => _durationSec = v.round())),
                _labelSlider('Adv Interval (ms)', _advIntervalMs.toDouble(), 100, 5000, 49,
                    (v) => setState(() => _advIntervalMs = v.round())),
                const SizedBox(height: 8),
                Row(children: [
                  const Text('LED Mode: '),
                  DropdownButton<int>(
                    value: _ledMode,
                    onChanged: (v) => setState(() => _ledMode = v!),
                    items: const [
                      DropdownMenuItem(value: 0, child: Text('Normal')),
                      DropdownMenuItem(value: 1, child: Text('Off')),
                    ],
                  ),
                ]),
                const SizedBox(height: 8),
                Row(children: [
                  Checkbox(value: _nameMode == 1, onChanged: (v) =>
                      setState(() => _nameMode = (v == true) ? 1 : 0)),
                  const Text('Custom name'),
                  const SizedBox(width: 8),
                  if (_nameMode == 1) Expanded(
                    child: TextField(
                      decoration: const InputDecoration(hintText: 'Device name (max 11)'),
                      maxLength: 11,
                      controller: TextEditingController(text: _name),
                      onChanged: (v) => _name = v,
                    ),
                  ),
                ]),
                const SizedBox(height: 12),
                FilledButton(
                  onPressed: connected ? _saveBleSettings : null,
                  child: const Text('Save BLE Settings'),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _opChip(String label, int value) => FilterChip(
    label: Text(label),
    selected: _opMode == value,
    onSelected: (_) => setState(() => _opMode = value),
  );

  Widget _labelSlider(String label, double value, double min, double max,
      double divisions, ValueChanged<double> onChanged) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(children: [
          Text(label), const Spacer(), Text(value.round().toString()),
        ]),
        Slider(value: value.clamp(min, max), min: min, max: max,
            divisions: divisions.round(), onChanged: onChanged),
      ],
    );
  }

  Widget buildWakeConfig() {
    final connected = context.watch<BleProvider>().isConnected;
    return Card(
      child: ExpansionTile(
        leading: const Icon(Icons.motion_photos_on_outlined),
        title: const Text('Wake-on-Motion'),
        initiallyExpanded: _wakeExpanded,
        onExpansionChanged: (v) {
          setState(() => _wakeExpanded = v);
          if (v) _loadWakeCfg();
        },
        children: [
          Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                SwitchListTile(
                  title: const Text('Enable Wake-on-Motion'),
                  value: _wakeEnable,
                  onChanged: (v) => setState(() => _wakeEnable = v),
                  contentPadding: EdgeInsets.zero,
                ),
                _labelSlider('Threshold (mg)', _threshMg.toDouble(), 10, 2000, 199,
                    (v) => setState(() => _threshMg = v.round())),
                _labelSlider('Duration (ms)', _durMs.toDouble(), 50, 5000, 99,
                    (v) => setState(() => _durMs = v.round())),
                const SizedBox(height: 8),
                Row(children: [
                  const Text('Action: '),
                  DropdownButton<int>(
                    value: _wakeAction,
                    onChanged: (v) => setState(() => _wakeAction = v!),
                    items: const [
                      DropdownMenuItem(value: 0, child: Text('Log')),
                      DropdownMenuItem(value: 1, child: Text('TX pulse')),
                      DropdownMenuItem(value: 2, child: Text('Both')),
                    ],
                  ),
                ]),
                const SizedBox(height: 12),
                FilledButton(
                  onPressed: connected ? _saveWakeCfg : null,
                  child: const Text('Save Wake Config'),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

// ── Scan section ───────────────────────────────────────────────────────────

class _ScanSection extends StatelessWidget {
  final _DevicesTabState parent;
  const _ScanSection({required this.parent});

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleProvider>();
    final devProv = context.watch<DevicesProvider>();

    return Column(
      children: [
        // Status + scan button
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          child: Row(children: [
            Expanded(child: Text(ble.statusMsg,
                style: Theme.of(context).textTheme.bodySmall)),
            if (ble.isConnected)
              OutlinedButton.icon(
                onPressed: ble.disconnect,
                icon: const Icon(Icons.bluetooth_disabled, size: 16),
                label: Text(ble.connectedName ?? 'Disconnect'),
              )
            else
              FilledButton.icon(
                onPressed: ble.isScanning ? ble.stopScan
                    : () => ble.startScan(timeoutSec: 10),
                icon: ble.isScanning
                    ? const SizedBox(width: 16, height: 16,
                        child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white))
                    : const Icon(Icons.search, size: 16),
                label: Text(ble.isScanning ? 'Stop' : 'Scan'),
              ),
          ]),
        ),

        // BLE + Wake settings (when connected)
        if (ble.isConnected)
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: Column(children: [
              parent.buildBleSettings(),
              parent.buildWakeConfig(),
            ]),
          ),

        const Divider(),

        // Scan results
        Expanded(
          child: ble.scanResults.isEmpty
              ? Center(child: Text(ble.isScanning ? 'Scanning…' : 'No devices found',
                  style: Theme.of(context).textTheme.bodySmall))
              : ListView.builder(
                  itemCount: ble.scanResults.length,
                  itemBuilder: (ctx, i) {
                    final r = ble.scanResults[i];
                    final saved = devProv.byMac(r.mac);
                    final displayName = saved?.name.isNotEmpty == true
                        ? saved!.name : (r.name.isNotEmpty ? r.name : r.mac);
                    return ListTile(
                      leading: _rssiIcon(r.rssi),
                      title: Text(displayName),
                      subtitle: Text(r.mac,
                          style: Theme.of(context).textTheme.labelSmall),
                      trailing: Text('${r.rssi} dBm',
                          style: TextStyle(color: _rssiColor(r.rssi), fontSize: 12)),
                      onTap: ble.isConnected ? null : () async {
                        await ble.stopScan();
                        final ok = await ble.connect(r.device, displayName);
                        if (ok) {
                          await devProv.touchDevice(r.mac, displayName, rssi: r.rssi);
                        }
                      },
                    );
                  },
                ),
        ),
      ],
    );
  }

  Widget _rssiIcon(int rssi) => Icon(
    Icons.signal_cellular_alt,
    color: _rssiColor(rssi),
  );

  Color _rssiColor(int rssi) {
    if (rssi > -70) return const Color(0xFF4CAF50);
    if (rssi > -85) return const Color(0xFFFFC107);
    return const Color(0xFFF44336);
  }
}

// ── Saved beacons section ──────────────────────────────────────────────────

class _SavedSection extends StatelessWidget {
  final _DevicesTabState parent;
  const _SavedSection({required this.parent});

  @override
  Widget build(BuildContext context) {
    final devProv = context.watch<DevicesProvider>();
    final ble = context.watch<BleProvider>();

    if (devProv.devices.isEmpty) {
      return const Center(child: Text('No saved beacons'));
    }

    return ListView.builder(
      itemCount: devProv.devices.length,
      itemBuilder: (ctx, i) {
        final d = devProv.devices[i];
        final isConnected = ble.isConnected && ble.connectedMac == d.mac;
        return Card(
          margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
          child: ListTile(
            leading: CircleAvatar(
              backgroundColor: isConnected ? Colors.green : Colors.grey,
              child: Icon(Icons.bluetooth,
                  color: Colors.white, size: 18),
            ),
            title: Text(d.displayName),
            subtitle: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(d.mac, style: Theme.of(context).textTheme.labelSmall),
                if (d.lastSeen != null)
                  Text('Last seen: ${_fmtDate(d.lastSeen!)}',
                      style: Theme.of(context).textTheme.labelSmall),
                if (d.lastBatMv != null)
                  Text('Bat: ${d.lastBatMv} mV',
                      style: Theme.of(context).textTheme.labelSmall),
              ],
            ),
            trailing: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                if (!isConnected)
                  IconButton(
                    icon: const Icon(Icons.link),
                    tooltip: 'Connect',
                    onPressed: () => ble.connectByMac(d.mac, d.displayName),
                  ),
                IconButton(
                  icon: const Icon(Icons.edit_outlined),
                  tooltip: 'Rename',
                  onPressed: () => _showRenameDialog(ctx, devProv, d),
                ),
                IconButton(
                  icon: const Icon(Icons.delete_outline),
                  tooltip: 'Remove',
                  onPressed: () => devProv.remove(d.mac),
                ),
              ],
            ),
            isThreeLine: true,
          ),
        );
      },
    );
  }

  String _fmtDate(DateTime dt) {
    final now = DateTime.now();
    final diff = now.difference(dt);
    if (diff.inMinutes < 2) return 'just now';
    if (diff.inHours < 1) return '${diff.inMinutes}m ago';
    if (diff.inDays < 1) return '${diff.inHours}h ago';
    return '${dt.day}.${dt.month}.${dt.year}';
  }

  void _showRenameDialog(BuildContext ctx, DevicesProvider prov, SavedBeacon d) {
    final ctrl = TextEditingController(text: d.name);
    showDialog(
      context: ctx,
      builder: (_) => AlertDialog(
        title: const Text('Rename beacon'),
        content: TextField(
          controller: ctrl,
          decoration: const InputDecoration(labelText: 'Name'),
          autofocus: true,
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
          FilledButton(onPressed: () {
            d.name = ctrl.text.trim();
            prov.update(d);
            Navigator.pop(ctx);
          }, child: const Text('Save')),
        ],
      ),
    );
  }
}
