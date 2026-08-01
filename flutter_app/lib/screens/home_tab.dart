import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/app_provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import '../providers/devices_provider.dart';

class HomeTab extends StatelessWidget {
  const HomeTab({super.key});

  @override
  Widget build(BuildContext context) {
    final ble    = context.watch<BleProvider>();
    final beacon = context.watch<BeaconProvider>();
    final devs   = context.watch<DevicesProvider>();

    return Scaffold(
      appBar: AppBar(
        title: const Text('Home'),
        actions: [
          if (ble.isConnected && beacon.isRefreshing)
            const Padding(
              padding: EdgeInsets.all(16),
              child: SizedBox(width: 20, height: 20,
                child: CircularProgressIndicator(strokeWidth: 2)),
            )
          else if (ble.isConnected)
            IconButton(
              icon: const Icon(Icons.refresh),
              onPressed: beacon.refreshAll,
            ),
          _RssiChip(rssi: ble.rssi, visible: ble.isConnected),
        ],
      ),
      body: ble.isConnected ? _ConnectedBody(beacon: beacon, devs: devs, ble: ble)
          : _DisconnectedBody(ble: ble),
    );
  }
}

// ── Not connected ──────────────────────────────────────────────────────────

class _DisconnectedBody extends StatelessWidget {
  final BleProvider ble;
  const _DisconnectedBody({required this.ble});

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Icon(Icons.bluetooth_disabled, size: 72, color: Colors.grey),
          const SizedBox(height: 16),
          const Text('Not connected', style: TextStyle(fontSize: 18)),
          const SizedBox(height: 8),
          Text(ble.statusMsg,
              style: Theme.of(context).textTheme.bodySmall),
          const SizedBox(height: 24),
          FilledButton.icon(
            onPressed: () => context.read<AppProvider>().setTabIndex(4),
            icon: const Icon(Icons.bluetooth_searching),
            label: const Text('Go to Devices'),
          ),
        ],
      ),
    );
  }
}

// ── Connected ─────────────────────────────────────────────────────────────

class _ConnectedBody extends StatelessWidget {
  final BeaconProvider beacon;
  final DevicesProvider devs;
  final BleProvider ble;
  const _ConnectedBody({required this.beacon, required this.devs, required this.ble});

  @override
  Widget build(BuildContext context) {
    final s = beacon.status;
    final saved = devs.byMac(ble.connectedMac);

    return RefreshIndicator(
      onRefresh: beacon.refreshAll,
      child: SingleChildScrollView(
        physics: const AlwaysScrollableScrollPhysics(),
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Device card
            _DeviceCard(ble: ble, beacon: beacon, saved: saved),
            const SizedBox(height: 12),

            // Value cards grid
            if (s != null) ...[
              _ValueCardsRow(s: s),
              const SizedBox(height: 12),
            ],

            // Storage bar
            if (beacon.logTotal > 0)
              _StorageBar(used: beacon.logUsed, total: beacon.logTotal),

            // Last error
            if (beacon.lastError != null) ...[
              const SizedBox(height: 8),
              Container(
                padding: const EdgeInsets.all(10),
                decoration: BoxDecoration(
                  color: Colors.red.withValues(alpha: 0.1),
                  borderRadius: BorderRadius.circular(8),
                  border: Border.all(color: Colors.red.withValues(alpha: 0.3)),
                ),
                child: Row(children: [
                  const Icon(Icons.error_outline, color: Colors.red, size: 16),
                  const SizedBox(width: 8),
                  Expanded(child: Text(beacon.lastError!,
                      style: const TextStyle(color: Colors.red, fontSize: 12))),
                ]),
              ),
            ],

            const SizedBox(height: 12),

            // Actions
            Row(children: [
              Expanded(child: OutlinedButton.icon(
                onPressed: beacon.syncTime,
                icon: const Icon(Icons.access_time, size: 16),
                label: const Text('Sync Time'),
              )),
              const SizedBox(width: 8),
              Expanded(child: OutlinedButton.icon(
                onPressed: () async {
                  final ok = await showDialog<bool>(
                    context: context,
                    builder: (_) => AlertDialog(
                      title: const Text('Reboot beacon?'),
                      actions: [
                        TextButton(onPressed: () => Navigator.pop(context, false),
                            child: const Text('Cancel')),
                        FilledButton(onPressed: () => Navigator.pop(context, true),
                            child: const Text('Reboot')),
                      ],
                    ),
                  );
                  if (ok == true) beacon.reboot();
                },
                icon: const Icon(Icons.restart_alt, size: 16),
                label: const Text('Reboot'),
              )),
            ]),
          ],
        ),
      ),
    );
  }
}

class _DeviceCard extends StatelessWidget {
  final BleProvider ble;
  final BeaconProvider beacon;
  final dynamic saved;
  const _DeviceCard({required this.ble, required this.beacon, this.saved});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Row(children: [
          CircleAvatar(
            backgroundColor: Colors.green.withValues(alpha: 0.15),
            child: const Icon(Icons.bluetooth_connected, color: Colors.green),
          ),
          const SizedBox(width: 12),
          Expanded(child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(ble.connectedName ?? ble.connectedMac,
                  style: Theme.of(context).textTheme.titleSmall),
              Text(ble.connectedMac,
                  style: Theme.of(context).textTheme.labelSmall),
              if (beacon.fwVersion != null)
                Text('FW: ${beacon.fwVersion}',
                    style: Theme.of(context).textTheme.labelSmall),
            ],
          )),
          Column(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: [
              if (beacon.txActive)
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                  decoration: BoxDecoration(
                    color: Colors.red.withValues(alpha: 0.15),
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: const Text('TX', style: TextStyle(color: Colors.red, fontSize: 11)),
                ),
              if (beacon.status?.schedActive == 1)
                const Text('SCHED', style: TextStyle(color: Colors.orange, fontSize: 11)),
            ],
          ),
        ]),
      ),
    );
  }
}

class _ValueCardsRow extends StatelessWidget {
  final dynamic s;
  const _ValueCardsRow({required this.s});

  @override
  Widget build(BuildContext context) {
    final tc = s.tempC as double;
    final tempColor = tc >= 36.0 && tc <= 38.0 ? const Color(0xFF4CAF50)
        : (tc >= 35.0 && tc <= 39.0) ? const Color(0xFFFFC107)
        : const Color(0xFFF44336);

    return GridView.count(
      crossAxisCount: 2,
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      childAspectRatio: 2.2,
      crossAxisSpacing: 8,
      mainAxisSpacing: 8,
      children: [
        _ValueCard(
          label: 'Temperature',
          value: '${tc.toStringAsFixed(1)} °C',
          icon: Icons.thermostat,
          color: tempColor,
        ),
        _ValueCard(
          label: 'Battery',
          value: '${s.batMv} mV (${s.batPct}%)',
          icon: Icons.battery_full,
          color: s.batPct > 20 ? const Color(0xFF4CAF50) : const Color(0xFFF44336),
        ),
        _ValueCard(
          label: 'Uptime',
          value: _fmtUptime(s.uptimeS),
          icon: Icons.timer_outlined,
          color: null,
        ),
        _ValueCard(
          label: 'Light',
          value: s.lightRaw.toString(),
          icon: Icons.light_mode_outlined,
          color: null,
        ),
      ],
    );
  }

  String _fmtUptime(int s) {
    final h = s ~/ 3600;
    final m = (s % 3600) ~/ 60;
    if (h > 0) return '${h}h ${m}m';
    return '${m}m';
  }
}

class _ValueCard extends StatelessWidget {
  final String label;
  final String value;
  final IconData icon;
  final Color? color;
  const _ValueCard({required this.label, required this.value,
    required this.icon, this.color});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
        child: Row(children: [
          Icon(icon, size: 24, color: color ?? Theme.of(context).colorScheme.primary),
          const SizedBox(width: 10),
          Expanded(child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Text(label, style: Theme.of(context).textTheme.labelSmall),
              Text(value, style: Theme.of(context).textTheme.titleSmall,
                  maxLines: 1, overflow: TextOverflow.ellipsis),
            ],
          )),
        ]),
      ),
    );
  }
}

class _StorageBar extends StatelessWidget {
  final int used;
  final int total;
  const _StorageBar({required this.used, required this.total});

  @override
  Widget build(BuildContext context) {
    final pct = total > 0 ? used / total : 0.0;
    final color = pct > 0.9 ? Colors.red : pct > 0.7 ? Colors.orange : Colors.green;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(children: [
          const Text('Log Storage', style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600)),
          const Spacer(),
          Text('$used / $total records', style: Theme.of(context).textTheme.labelSmall),
        ]),
        const SizedBox(height: 6),
        ClipRRect(
          borderRadius: BorderRadius.circular(4),
          child: LinearProgressIndicator(
            value: pct.clamp(0.0, 1.0),
            minHeight: 8,
            backgroundColor: Colors.grey.withValues(alpha: 0.2),
            valueColor: AlwaysStoppedAnimation(color),
          ),
        ),
        const SizedBox(height: 4),
        Text('${(pct * 100).round()}% used',
            style: Theme.of(context).textTheme.labelSmall),
      ],
    );
  }
}

class _RssiChip extends StatelessWidget {
  final int rssi;
  final bool visible;
  const _RssiChip({required this.rssi, required this.visible});

  @override
  Widget build(BuildContext context) {
    if (!visible) return const SizedBox.shrink();
    final color = rssi > -70 ? const Color(0xFF4CAF50)
        : rssi > -85 ? const Color(0xFFFFC107) : const Color(0xFFF44336);
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 14),
      child: Text('$rssi dBm', style: TextStyle(color: color, fontSize: 12)),
    );
  }
}
