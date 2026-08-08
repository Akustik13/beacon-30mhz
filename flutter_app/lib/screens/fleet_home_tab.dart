import 'dart:async';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/app_provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import '../providers/devices_provider.dart';
import '../protocol/opcodes.dart';
import 'home_tab.dart' show HomeTab, RssiChartSheet;
import 'beacon_tab.dart';
import 'logging_tab.dart';
import 'data_tab.dart';
import 'events_tab.dart';
import 'devices_tab.dart';

/// Fleet dashboard — root screen when layoutMode == 1.
/// Shows a searchable card list of all saved beacons.
class FleetHomeTab extends StatefulWidget {
  const FleetHomeTab({super.key});
  @override
  State<FleetHomeTab> createState() => _FleetHomeTabState();
}

class _FleetHomeTabState extends State<FleetHomeTab> with TickerProviderStateMixin {
  String _query = '';
  late AnimationController _scanAnim;

  @override
  void initState() {
    super.initState();
    _scanAnim = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 2),
    )..repeat();
  }

  @override
  void dispose() {
    _scanAnim.dispose();
    super.dispose();
  }

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
        actions: [
          if (ble.isScanning)
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 14),
              child: RotationTransition(
                turns: _scanAnim,
                child: Icon(Icons.radar, size: 18,
                    color: Theme.of(context).colorScheme.primary),
              ),
            ),
        ],
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

  // Remember last open tab per beacon MAC so re-entering restores the same tab
  final _lastTabPerMac = <String, int>{};

  void _openDetail(BuildContext ctx, String mac, String name) {
    Navigator.of(ctx).push(MaterialPageRoute(
      builder: (_) => _BeaconDetailScreen(
        mac: mac,
        name: name,
        initialTab: _lastTabPerMac[mac] ?? 0,
        onTabChanged: (i) => _lastTabPerMac[mac] = i,
      ),
    ));
  }
}

// ── Beacon detail screen (Fleet mode) ────────────────────────────────────────

class _BeaconDetailScreen extends StatefulWidget {
  final String mac;
  final String name;
  final int initialTab;
  final void Function(int)? onTabChanged;
  const _BeaconDetailScreen({
    required this.mac, required this.name,
    this.initialTab = 0, this.onTabChanged,
  });
  @override
  State<_BeaconDetailScreen> createState() => _BeaconDetailScreenState();
}

class _BeaconDetailScreenState extends State<_BeaconDetailScreen>
    with TickerProviderStateMixin {
  late TabController _tabs;
  late AnimationController _scanAnim;
  Timer? _clockTimer;
  bool _connecting = false;

  @override
  void initState() {
    super.initState();
    _tabs = TabController(
      length: 4, vsync: this,
      initialIndex: widget.initialTab.clamp(0, 3),
    );
    _tabs.addListener(() {
      if (!_tabs.indexIsChanging) widget.onTabChanged?.call(_tabs.index);
    });
    _scanAnim = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 2),
    )..repeat();
    // Rebuild every second so the beacon clock in status box ticks
    _clockTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    // Save current tab before disposal so _lastTabPerMac is always up to date
    widget.onTabChanged?.call(_tabs.index);
    _clockTimer?.cancel();
    _tabs.dispose();
    _scanAnim.dispose();
    super.dispose();
  }

  // Hides the inner Scaffold's AppBar toolbar (title row) while keeping
  // AppBar.bottom (inner TabBar) if the widget has one.
  Widget _embed(Widget child) => Theme(
    data: Theme.of(context).copyWith(
      appBarTheme: Theme.of(context).appBarTheme.copyWith(
        toolbarHeight: 0,
        elevation: 0,
        scrolledUnderElevation: 0,
        shadowColor: Colors.transparent,
      ),
    ),
    child: child,
  );

  Future<void> _connect() async {
    setState(() => _connecting = true);
    final ok = await context.read<BleProvider>()
        .connectByMac(widget.mac, widget.name);
    if (!mounted) return;
    setState(() => _connecting = false);
    if (!ok) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Connection failed')));
    }
  }

  @override
  Widget build(BuildContext context) {
    final ble         = context.watch<BleProvider>();
    final beacon      = context.watch<BeaconProvider>();
    final isConnected = ble.isConnected && ble.connectedMac == widget.mac;
    final rssi        = ble.rssi;
    final rssiColor   = rssi > -70
        ? const Color(0xFF4CAF50)
        : rssi > -85 ? const Color(0xFFFFC107) : const Color(0xFFF44336);

    return Scaffold(
      appBar: AppBar(
        toolbarHeight: 60,
        // ── When connected: beacon name + signal live inside the title badge ──
        title: isConnected
            ? GestureDetector(
                onTap: () => RssiChartSheet.show(context),
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                  decoration: BoxDecoration(
                    color: Colors.green.withValues(alpha: 0.12),
                    borderRadius: BorderRadius.circular(20),
                    border: Border.all(color: Colors.green.withValues(alpha: 0.4)),
                  ),
                  child: Row(mainAxisSize: MainAxisSize.min, children: [
                    const Icon(Icons.circle, size: 7, color: Colors.green),
                    const SizedBox(width: 8),
                    Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Text(widget.name,
                            style: const TextStyle(
                                color: Colors.green,
                                fontSize: 14,
                                fontWeight: FontWeight.bold)),
                        if (rssi != 0)
                          Row(mainAxisSize: MainAxisSize.min, children: [
                            Text('$rssi dBm',
                                style: TextStyle(
                                    color: rssiColor, fontSize: 11)),
                            const SizedBox(width: 3),
                            Icon(Icons.show_chart, size: 11,
                                color: rssiColor.withValues(alpha: 0.6)),
                          ]),
                      ],
                    ),
                  ]),
                ),
              )
            : Text(widget.name),
        actions: [
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 8, horizontal: 2),
            child: _AppBarStatus(
              isConnected: isConnected,
              tabIdx: _tabs.index,
              beacon: beacon,
            ),
          ),
          if (isConnected)
            // ── Only disconnect button on the right ────────────
            IconButton(
              icon: const Icon(Icons.link_off),
              tooltip: 'Disconnect',
              color: Colors.red,
              onPressed: () => context.read<BleProvider>().disconnect(),
            )
          else if (_connecting)
            const Padding(
              padding: EdgeInsets.all(16),
              child: SizedBox(width: 20, height: 20,
                  child: CircularProgressIndicator(strokeWidth: 2)),
            )
          else if (ble.isScanning)
            // ── Scanning indicator (auto-scan running) ─────────
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 14),
              child: Row(mainAxisSize: MainAxisSize.min, children: [
                RotationTransition(
                  turns: _scanAnim,
                  child: Icon(Icons.radar,
                      size: 18,
                      color: Theme.of(context).colorScheme.primary),
                ),
                const SizedBox(width: 6),
                Text('Scanning…',
                    style: TextStyle(
                        fontSize: 12,
                        color: Theme.of(context).colorScheme.primary)),
              ]),
            )
          else
            TextButton.icon(
              onPressed: _connect,
              icon: const Icon(Icons.bluetooth, size: 16),
              label: const Text('Connect'),
            ),
          const SizedBox(width: 4),
        ],
        bottom: TabBar(
          controller: _tabs,
          tabs: const [
            Tab(child: _TabLabel(Icons.home_outlined,  'Overview')),
            Tab(child: _TabLabel(Icons.tune_outlined,  'Config')),
            Tab(child: _TabLabel(Icons.show_chart,     'Data')),
            Tab(child: _TabLabel(Icons.bluetooth,      'BLE')),
          ],
        ),
      ),
      body: TabBarView(
        controller: _tabs,
        children: [
          _embed(const HomeTab()),
          _embed(const _ManageTab()),   // Beacon · Logging · Events
          _embed(const DataTab()),
          _embed(const DevicesTab()),
        ],
      ),
    );
  }
}

// ── AppBar status box: temp/bat + time/storage + memory depth ────────────────

class _AppBarStatus extends StatelessWidget {
  final bool           isConnected;
  final int            tabIdx;
  final BeaconProvider beacon;
  const _AppBarStatus({
    required this.isConnected,
    required this.tabIdx,
    required this.beacon,
  });

  // Mirror LoggingTab._memoryDepthS(): only the 4 logged sensor IDs, same capacity logic.
  static String _memDepth(BeaconProvider b) {
    if (b.sensors.isEmpty) return '—';
    double wps = 0;
    for (final id in [sensorIdTemp, sensorIdLight, sensorIdBatt, sensorIdAccel]) {
      final s = b.sensorById(id);
      if (s == null) continue;
      if ((s['enabled'] ?? 0) == 1) {
        final iv = s['interval_s'] ?? 0;
        if (iv > 0) wps += 1.0 / iv;
      }
    }
    if (wps == 0) return '∞';
    final total = b.logTotal > 0 ? b.logTotal : logEntriesMax;
    final circ  = b.config?.logOverflow == 1;
    final used  = b.logUsed.clamp(0, total);
    final cap   = circ ? total : (total - used).clamp(0, total);
    return _fmtSec((cap / wps).round());
  }

  static String _fmtSec(int s) {
    if (s >= 365 * 86400) return '∞';
    if (s >= 86400) { final d = s ~/ 86400; final h = (s % 86400) ~/ 3600; return h > 0 ? '${d}д ${h}г' : '${d}д'; }
    if (s >= 3600)  { final h = s ~/ 3600;  final m = (s % 3600) ~/ 60;   return m > 0 ? '${h}г ${m}х' : '${h}г'; }
    if (s >= 60)    { return '${s ~/ 60}хв'; }
    return '${s}с';
  }

  @override
  Widget build(BuildContext context) {
    final on = isConnected;
    final s  = beacon.status;

    final tempStr  = (on && s != null) ? '${s.tempC.toStringAsFixed(1)}°' : '—';
    final batStr   = (on && s != null) ? '${s.batPct}%' : '—';
    final batPct   = s?.batPct ?? 100;
    final batColor = on ? (batPct > 20 ? Colors.green : Colors.red) : Colors.grey;

    Widget row1, row2;

    if (tabIdx == 1) {
      // ── Config tab: battery days estimate (stub) + memory depth ─────────
      String batEst = '—';
      if (on && s != null) {
        final estDays = (s.batPct / 100 * 365).round();
        if (estDays >= 330)      batEst = '≥11M';
        else if (estDays >= 30)  batEst = '~${(estDays / 30).round()}M';
        else                     batEst = '~${estDays}д';
      }
      final memStr   = _memDepth(beacon);
      final circ     = beacon.config?.logOverflow == 1;
      final memColor = on ? (circ ? Colors.orange : const Color(0xFF3F51B5)) : Colors.grey;

      row1 = Row(mainAxisSize: MainAxisSize.min, children: [
        Icon(Icons.battery_full, size: 11, color: batColor),
        const SizedBox(width: 3),
        Text(batEst,
            style: TextStyle(fontSize: 11,
                color: on ? null : Colors.grey,
                fontWeight: FontWeight.w600)),
      ]);
      row2 = Row(mainAxisSize: MainAxisSize.min, children: [
        Icon(Icons.timelapse, size: 10, color: memColor),
        const SizedBox(width: 2),
        Text(memStr,
            style: TextStyle(fontSize: 10, color: memColor,
                fontWeight: FontWeight.w600)),
      ]);

    } else {
      // ── Tabs 0 (Overview), 2 (Data), 3 (BLE): row1 = temp + battery ────
      row1 = Row(mainAxisSize: MainAxisSize.min, children: [
        Icon(Icons.thermostat, size: 11, color: on ? Colors.orange : Colors.grey),
        const SizedBox(width: 2),
        Text(tempStr,
            style: TextStyle(fontSize: 11,
                color: on ? null : Colors.grey,
                fontWeight: FontWeight.w600)),
        const SizedBox(width: 7),
        Icon(Icons.battery_full, size: 11, color: batColor),
        const SizedBox(width: 2),
        Text(batStr,
            style: TextStyle(fontSize: 11, color: on ? null : Colors.grey)),
      ]);

      if (tabIdx == 2) {
        // Data tab: flash storage usage
        final total = beacon.logTotal > 0 ? beacon.logTotal : logEntriesMax;
        row2 = Row(mainAxisSize: MainAxisSize.min, children: [
          Icon(Icons.sd_storage, size: 10, color: on ? Colors.blue : Colors.grey),
          const SizedBox(width: 2),
          Text('${beacon.logUsed}/$total',
              style: TextStyle(fontSize: 10,
                  color: on ? Colors.blue : Colors.grey)),
        ]);
      } else {
        // Overview (0) + BLE (3): beacon time HH:mm:ss
        String timeStr = '--:--:--';
        if (on && beacon.beaconTimeS != null && beacon.beaconTimeReadAt != null) {
          final el  = DateTime.now().difference(beacon.beaconTimeReadAt!).inSeconds;
          final now = DateTime.fromMillisecondsSinceEpoch(
              (beacon.beaconTimeS! + el) * 1000).toLocal();
          timeStr = '${now.hour.toString().padLeft(2, '0')}:'
              '${now.minute.toString().padLeft(2, '0')}:'
              '${now.second.toString().padLeft(2, '0')}';
        }
        row2 = Row(mainAxisSize: MainAxisSize.min, children: [
          Icon(Icons.schedule, size: 10, color: on ? Colors.teal : Colors.grey),
          const SizedBox(width: 2),
          Text(timeStr,
              style: TextStyle(fontSize: 10, color: on ? Colors.teal : Colors.grey)),
        ]);
      }
    }

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 4),
      decoration: BoxDecoration(
        color: Colors.grey.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Colors.grey.withValues(alpha: 0.25)),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [row1, const SizedBox(height: 2), row2],
      ),
    );
  }
}

// ── Compact tab label: icon left of text, single-height row ──────────────────

class _TabLabel extends StatelessWidget {
  final IconData icon;
  final String   label;
  const _TabLabel(this.icon, this.label);

  @override
  Widget build(BuildContext context) => Row(
    mainAxisSize: MainAxisSize.min,
    children: [
      Icon(icon, size: 16),
      const SizedBox(width: 6),
      Text(label, style: const TextStyle(fontSize: 13)),
    ],
  );
}

// ── Config group: Beacon · Logging · Events in inner tab bar ─────────────────

class _ManageTab extends StatefulWidget {
  const _ManageTab();
  @override
  State<_ManageTab> createState() => _ManageTabState();
}

class _ManageTabState extends State<_ManageTab>
    with SingleTickerProviderStateMixin, AutomaticKeepAliveClientMixin {
  @override
  bool get wantKeepAlive => true;

  late TabController _inner;

  @override
  void initState() {
    super.initState();
    _inner = TabController(length: 3, vsync: this);
  }

  @override
  void dispose() {
    _inner.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return Column(children: [
      Material(
        color: Theme.of(context).colorScheme.surfaceContainerHighest,
        child: TabBar(
          controller: _inner,
          indicatorSize: TabBarIndicatorSize.tab,
          tabs: const [
            Tab(text: 'Beacon'),
            Tab(text: 'Logging'),
            Tab(text: 'Events'),
          ],
        ),
      ),
      Expanded(
        child: TabBarView(
          controller: _inner,
          children: const [
            BeaconTab(),
            LoggingTab(),
            EventsTab(),
          ],
        ),
      ),
    ]);
  }
}
