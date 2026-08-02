import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/app_provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import '../providers/devices_provider.dart';
import 'home_tab.dart';
import 'beacon_tab.dart';
import 'logging_tab.dart';
import 'data_tab.dart';

/// Fleet dashboard — root screen when layoutMode == 1.
/// Shows a searchable card list of all saved beacons.
class FleetHomeTab extends StatefulWidget {
  const FleetHomeTab({super.key});
  @override
  State<FleetHomeTab> createState() => _FleetHomeTabState();
}

class _FleetHomeTabState extends State<FleetHomeTab> {
  String _query = '';

  @override
  Widget build(BuildContext context) {
    final devs    = context.watch<DevicesProvider>();
    final ble     = context.watch<BleProvider>();
    final beacon  = context.watch<BeaconProvider>();

    // Build a MAC→RSSI lookup from live scan results
    final rssiMap = { for (final r in ble.scanResults) r.mac: r.rssi };

    final filtered = devs.devices.where((d) {
      if (_query.isEmpty) return true;
      final q = _query.toLowerCase();
      return d.displayName.toLowerCase().contains(q) ||
             d.mac.toLowerCase().contains(q) ||
             d.notes.toLowerCase().contains(q);
    }).toList();

    return Scaffold(
      appBar: AppBar(
        title: const Text('Fleet'),
        bottom: PreferredSize(
          preferredSize: const Size.fromHeight(52),
          child: Padding(
            padding: const EdgeInsets.fromLTRB(12, 0, 12, 8),
            child: TextField(
              decoration: const InputDecoration(
                hintText: 'Search beacons…',
                prefixIcon: Icon(Icons.search, size: 20),
                isDense: true,
                contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                border: OutlineInputBorder(),
              ),
              onChanged: (v) => setState(() => _query = v),
            ),
          ),
        ),
      ),
      body: filtered.isEmpty
          ? Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(Icons.bluetooth_disabled, size: 64,
                      color: Colors.grey.withValues(alpha: 0.3)),
                  const SizedBox(height: 12),
                  Text(devs.devices.isEmpty
                      ? 'No beacons saved yet.\nGo to Devices tab to scan and connect.'
                      : 'No beacons match "$_query"',
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.bodySmall),
                ],
              ),
            )
          : ListView.builder(
              padding: EdgeInsets.fromLTRB(12, 8, 12,
                  MediaQuery.of(context).padding.bottom + 16),
              itemCount: filtered.length,
              itemBuilder: (ctx, i) {
                final dev = filtered[i];
                final isConnected = ble.isConnected &&
                    ble.connectedMac == dev.mac;
                final rssi = isConnected ? ble.rssi : rssiMap[dev.mac];
                final online = rssi != null;
                final tempC = isConnected ? beacon.tempC : dev.lastTempC;
                final batMv = isConnected ? beacon.batMv : dev.lastBatMv;

                return Card(
                  margin: const EdgeInsets.symmetric(vertical: 4),
                  child: ListTile(
                    onTap: () => _openDetail(ctx, dev.mac, dev.displayName),
                    leading: CircleAvatar(
                      backgroundColor: (isConnected
                          ? Colors.green : online ? Colors.blue : Colors.grey)
                          .withValues(alpha: 0.15),
                      child: Icon(
                        isConnected
                            ? Icons.bluetooth_connected
                            : online ? Icons.bluetooth_searching
                            : Icons.bluetooth_disabled,
                        color: isConnected ? Colors.green
                            : online ? Colors.blue : Colors.grey,
                        size: 20,
                      ),
                    ),
                    title: Row(children: [
                      Expanded(child: Text(dev.displayName,
                          style: const TextStyle(fontWeight: FontWeight.w600))),
                      if (online) ...[
                        Container(
                          width: 8, height: 8,
                          decoration: BoxDecoration(
                            shape: BoxShape.circle,
                            color: isConnected ? Colors.green : Colors.blue,
                          ),
                        ),
                        const SizedBox(width: 4),
                        Text(rssi != null ? '$rssi dBm' : '',
                            style: const TextStyle(fontSize: 11, color: Colors.grey)),
                      ],
                    ]),
                    subtitle: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(dev.mac, style: const TextStyle(fontSize: 11)),
                        const SizedBox(height: 2),
                        Row(children: [
                          if (tempC != null) ...[
                            const Icon(Icons.thermostat, size: 12, color: Colors.orange),
                            Text(' ${tempC.toStringAsFixed(1)}°C ',
                                style: const TextStyle(fontSize: 11)),
                          ],
                          if (batMv != null) ...[
                            const Icon(Icons.battery_3_bar, size: 12, color: Colors.green),
                            Text(' ${batMv} mV', style: const TextStyle(fontSize: 11)),
                          ],
                          if (!online && dev.lastSeen != null) ...[
                            const Spacer(),
                            Text(_fmtLastSeen(dev.lastSeen!),
                                style: const TextStyle(fontSize: 10, color: Colors.grey)),
                          ],
                        ]),
                      ],
                    ),
                    trailing: const Icon(Icons.chevron_right, size: 18),
                  ),
                );
              },
            ),
    );
  }

  String _fmtLastSeen(DateTime t) {
    final diff = DateTime.now().difference(t);
    if (diff.inMinutes < 1) return 'just now';
    if (diff.inHours < 1)   return '${diff.inMinutes}m ago';
    if (diff.inDays  < 1)   return '${diff.inHours}h ago';
    return '${diff.inDays}d ago';
  }

  void _openDetail(BuildContext ctx, String mac, String name) {
    Navigator.of(ctx).push(MaterialPageRoute(
      builder: (_) => _BeaconDetailScreen(mac: mac, name: name),
    ));
  }
}

// ── Beacon detail screen (Fleet mode) ────────────────────────────────────────

class _BeaconDetailScreen extends StatefulWidget {
  final String mac;
  final String name;
  const _BeaconDetailScreen({required this.mac, required this.name});
  @override
  State<_BeaconDetailScreen> createState() => _BeaconDetailScreenState();
}

class _BeaconDetailScreenState extends State<_BeaconDetailScreen>
    with SingleTickerProviderStateMixin {
  late TabController _tabs;

  @override
  void initState() {
    super.initState();
    _tabs = TabController(length: 4, vsync: this);
  }

  @override
  void dispose() { _tabs.dispose(); super.dispose(); }

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleProvider>();
    final isConnected = ble.isConnected && ble.connectedMac == widget.mac;

    return Scaffold(
      appBar: AppBar(
        title: Text(widget.name),
        actions: [
          if (!isConnected)
            TextButton.icon(
              onPressed: () async {
                final ok = await context.read<BleProvider>()
                    .connectByMac(widget.mac, widget.name);
                if (ok && context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(content: Text('Connected to ${widget.name}')));
                }
              },
              icon: const Icon(Icons.bluetooth, size: 16),
              label: const Text('Connect'),
            )
          else
            TextButton.icon(
              onPressed: () async {
                await context.read<BleProvider>().disconnect();
              },
              icon: const Icon(Icons.bluetooth_disabled, size: 16),
              label: const Text('Disconnect'),
              style: TextButton.styleFrom(foregroundColor: Colors.red),
            ),
        ],
        bottom: TabBar(
          controller: _tabs,
          tabs: const [
            Tab(text: 'Overview'),
            Tab(text: 'Beacon'),
            Tab(text: 'Logging'),
            Tab(text: 'Data'),
          ],
        ),
      ),
      body: TabBarView(
        controller: _tabs,
        // Reuse the exact same tab content widgets as Focused mode
        children: const [
          HomeTab(),
          BeaconTab(),
          LoggingTab(),
          DataTab(),
        ],
      ),
    );
  }
}
