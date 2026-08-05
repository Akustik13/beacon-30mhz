import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:permission_handler/permission_handler.dart';
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

  // BLE settings form state
  int _opMode = bleOpContinuous;
  int _txPower = 24;
  int _intervalS = 1800;
  int _durationSec = 60;
  int _advIntervalMs = 1000;
  int _nameMode = 0;
  String _name = '';
  int _ledMode = 0;

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
                      DropdownMenuItem(value: 2, child: Text('Triple blink')),
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

}

// ── Scan section ───────────────────────────────────────────────────────────

class _ScanSection extends StatefulWidget {
  final _DevicesTabState parent;
  const _ScanSection({required this.parent});
  @override
  State<_ScanSection> createState() => _ScanSectionState();
}

class _ScanSectionState extends State<_ScanSection> {
  bool _permDenied     = false;
  bool _beaconsOnly    = true;

  bool _isBeacon(String name) {
    final n = name.toUpperCase();
    return n.contains('BCN') || n.contains('BEACON');
  }

  Future<void> _startScan(BleProvider ble) async {
    final scan    = await Permission.bluetoothScan.request();
    final connect = await Permission.bluetoothConnect.request();
    if (scan.isPermanentlyDenied || connect.isPermanentlyDenied ||
        scan.isDenied || connect.isDenied) {
      setState(() => _permDenied = true);
      return;
    }
    setState(() => _permDenied = false);
    ble.startScan(timeoutSec: 12);
  }

  @override
  Widget build(BuildContext context) {
    final ble     = context.watch<BleProvider>();
    final devProv = context.watch<DevicesProvider>();

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [

        // ── Connected strip ─────────────────────────────────────────────
        if (ble.isConnected)
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            child: Row(children: [
              const Icon(Icons.bluetooth_connected, size: 16, color: Colors.green),
              const SizedBox(width: 6),
              Expanded(child: Text(ble.connectedName ?? ble.connectedMac,
                  style: const TextStyle(color: Colors.green, fontSize: 13))),
              OutlinedButton.icon(
                style: OutlinedButton.styleFrom(
                  foregroundColor: Colors.red,
                  side: const BorderSide(color: Colors.red),
                  minimumSize: const Size(0, 34),
                  padding: const EdgeInsets.symmetric(horizontal: 10),
                ),
                onPressed: ble.disconnect,
                icon: const Icon(Icons.bluetooth_disabled, size: 14),
                label: const Text('Disconnect', style: TextStyle(fontSize: 12)),
              ),
            ]),
          ),

        // ── Permission denied banner ─────────────────────────────────────
        if (_permDenied)
          Container(
            margin: const EdgeInsets.fromLTRB(12, 8, 12, 0),
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              color: Colors.red.withOpacity(0.12),
              borderRadius: BorderRadius.circular(10),
              border: Border.all(color: Colors.red.withOpacity(0.5)),
            ),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              const Row(children: [
                Icon(Icons.bluetooth_disabled, color: Colors.red, size: 18),
                SizedBox(width: 8),
                Text('Bluetooth permission required',
                    style: TextStyle(color: Colors.red, fontWeight: FontWeight.w600)),
              ]),
              const SizedBox(height: 6),
              const Text(
                'Grant Bluetooth scan and connect permissions to find beacons.',
                style: TextStyle(fontSize: 12, color: Colors.red),
              ),
              const SizedBox(height: 8),
              SizedBox(
                height: 36,
                child: OutlinedButton(
                  style: OutlinedButton.styleFrom(
                    foregroundColor: Colors.red,
                    side: const BorderSide(color: Colors.red),
                    minimumSize: Size.zero,
                  ),
                  onPressed: openAppSettings,
                  child: const Text('Open app settings', style: TextStyle(fontSize: 13)),
                ),
              ),
            ]),
          ),

        // ── Prominent scan button (when not connected) ───────────────────
        if (!ble.isConnected)
          Padding(
            padding: const EdgeInsets.fromLTRB(12, 12, 12, 0),
            child: FilledButton.icon(
              style: FilledButton.styleFrom(
                minimumSize: const Size(double.infinity, 56),
                backgroundColor:
                    ble.isScanning ? const Color(0xFF6A1B9A) : const Color(0xFF1565C0),
                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
              ),
              onPressed: ble.isScanning ? ble.stopScan : () => _startScan(ble),
              icon: ble.isScanning
                  ? const SizedBox(
                      width: 20, height: 20,
                      child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white))
                  : const Icon(Icons.bluetooth_searching, size: 24),
              label: Text(
                ble.isScanning ? 'Stop scanning…' : 'Scan for beacons',
                style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
              ),
            ),
          ),

        // ── When connected: BLE settings ───────────────────────────────
        if (ble.isConnected)
          Expanded(
            child: SingleChildScrollView(
              padding: EdgeInsets.fromLTRB(12, 8, 12,
                  MediaQuery.of(context).padding.bottom + 32),
              child: Column(children: [
                widget.parent.buildBleSettings(),
              ]),
            ),
          )
        else ...[

          // ── Auto-scan toggle ─────────────────────────────────────────
          Padding(
            padding: const EdgeInsets.fromLTRB(14, 4, 4, 0),
            child: Row(children: [
              const Icon(Icons.autorenew, size: 15, color: Colors.grey),
              const SizedBox(width: 6),
              const Text('Auto-scan', style: TextStyle(fontSize: 13)),
              const Spacer(),
              Transform.scale(
                scale: 0.85,
                child: Switch.adaptive(
                  value: ble.autoScanEnabled,
                  onChanged: (v) => ble.setAutoScanEnabled(v),
                ),
              ),
            ]),
          ),

          // ── Filter chip + scan status ────────────────────────────────
          Padding(
            padding: const EdgeInsets.fromLTRB(12, 8, 12, 0),
            child: Row(children: [
              FilterChip(
                label: const Text('Beacons only', style: TextStyle(fontSize: 12)),
                selected: _beaconsOnly,
                onSelected: (v) => setState(() => _beaconsOnly = v),
                avatar: const Icon(Icons.filter_alt_outlined, size: 14),
                visualDensity: VisualDensity.compact,
              ),
              const SizedBox(width: 8),
              if (ble.isScanning || ble.scanResults.isNotEmpty)
                Expanded(child: Row(children: [
                  if (ble.isScanning) ...[
                    const SizedBox(width: 10, height: 10,
                      child: CircularProgressIndicator(strokeWidth: 1.5)),
                    const SizedBox(width: 6),
                  ],
                  Flexible(child: Text(
                    ble.isScanning ? 'Scanning…'
                        : '${ble.scanResults.length} found',
                    style: Theme.of(context).textTheme.labelSmall,
                    overflow: TextOverflow.ellipsis,
                  )),
                ])),
            ]),
          ),

          const Divider(height: 12),

          // ── Scan results ─────────────────────────────────────────────
          Expanded(child: Builder(builder: (context) {
            final visible = _beaconsOnly
                ? ble.scanResults.where((r) => _isBeacon(r.name)).toList()
                : ble.scanResults;

            if (visible.isEmpty) {
              return Center(
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Icon(Icons.bluetooth_searching, size: 72,
                        color: Colors.grey.withOpacity(0.25)),
                    const SizedBox(height: 12),
                    Text(
                      ble.isScanning
                          ? (_beaconsOnly ? 'Waiting for beacons…' : 'Looking for devices…')
                          : (_beaconsOnly
                              ? 'No beacons found\nTap "Scan for beacons" to try again'
                              : 'Tap "Scan for beacons" to start'),
                      style: Theme.of(context).textTheme.bodySmall,
                      textAlign: TextAlign.center,
                    ),
                  ],
                ),
              );
            }

            return ListView.builder(
              padding: const EdgeInsets.only(bottom: 80),
              itemCount: visible.length,
              itemBuilder: (ctx, i) {
                final r          = visible[i];
                final saved      = devProv.byMac(r.mac);
                final displayName = saved?.name.isNotEmpty == true
                    ? saved!.name
                    : (r.name.isNotEmpty ? r.name : r.mac);
                final beacon = _isBeacon(r.name);

                return ListTile(
                  leading: Stack(
                    clipBehavior: Clip.none,
                    children: [
                      Icon(
                        beacon ? Icons.bluetooth : Icons.bluetooth_searching,
                        color: _rssiColor(r.rssi, beacon),
                      ),
                      if (beacon)
                        Positioned(
                          right: -3, top: -3,
                          child: Container(
                            width: 9, height: 9,
                            decoration: const BoxDecoration(
                              color: Colors.green,
                              shape: BoxShape.circle,
                            ),
                          ),
                        ),
                    ],
                  ),
                  title: Text(
                    displayName,
                    style: TextStyle(
                      fontWeight: beacon ? FontWeight.w600 : FontWeight.normal,
                      color: beacon ? null : Colors.grey,
                    ),
                  ),
                  subtitle: Row(children: [
                    Expanded(child: Text(r.mac,
                        style: Theme.of(context).textTheme.labelSmall)),
                    if (beacon)
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 1),
                        decoration: BoxDecoration(
                          color: Colors.green.withOpacity(0.15),
                          borderRadius: BorderRadius.circular(8),
                        ),
                        child: const Text('Beacon',
                            style: TextStyle(color: Colors.green, fontSize: 10)),
                      ),
                  ]),
                  trailing: Text('${r.rssi} dBm',
                      style: TextStyle(
                          color: _rssiColor(r.rssi, beacon), fontSize: 12)),
                  onTap: () async {
                    await ble.stopScan();
                    final ok = await ble.connect(r.device, displayName);
                    if (ok) {
                      await devProv.touchDevice(r.mac, displayName, rssi: r.rssi);
                    }
                  },
                );
              },
            );
          })),
        ],
      ],
    );
  }

  Color _rssiColor(int rssi, bool beacon) {
    if (!beacon) return Colors.grey;
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
