import 'dart:async';
import 'package:flutter/material.dart';
import 'package:fl_chart/fl_chart.dart';
import 'package:provider/provider.dart';
import '../providers/app_provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import '../providers/devices_provider.dart';
import '../protocol/opcodes.dart';

class HomeTab extends StatefulWidget {
  const HomeTab({super.key});
  @override
  State<HomeTab> createState() => _HomeTabState();
}

class _HomeTabState extends State<HomeTab> {
  bool _chartExpanded    = false;
  bool _syncBannerShown  = false;

  @override
  Widget build(BuildContext context) {
    final ble    = context.watch<BleProvider>();
    final beacon = context.watch<BeaconProvider>();
    final devs   = context.watch<DevicesProvider>();
    final connected = ble.isConnected;
    final s = beacon.status;

    // One-time auto-sync banner
    if (beacon.autoSyncedTime && !_syncBannerShown) {
      _syncBannerShown = true;
      WidgetsBinding.instance.addPostFrameCallback((_) {
        beacon.clearAutoSyncFlag();
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
            content: Text('Beacon time was out of sync — synced automatically'),
            duration: Duration(seconds: 4),
          ));
        }
      });
    }
    if (!connected) _syncBannerShown = false;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Home'),
        actions: [
          if (connected && beacon.isRefreshing)
            const Padding(padding: EdgeInsets.all(16),
              child: SizedBox(width: 20, height: 20,
                child: CircularProgressIndicator(strokeWidth: 2)))
          else if (connected)
            IconButton(icon: const Icon(Icons.refresh), onPressed: beacon.refreshAll),
          _RssiChip(rssi: ble.rssi, visible: connected),
        ],
      ),
      body: RefreshIndicator(
        onRefresh: connected ? beacon.refreshAll : () async {},
        child: SingleChildScrollView(
          physics: const AlwaysScrollableScrollPhysics(),
          padding: EdgeInsets.fromLTRB(12, 12, 12,
              MediaQuery.of(context).padding.bottom + 96),
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [

            // ── Not-connected banner ─────────────────────────────────────
            if (!connected)
              GestureDetector(
                onTap: () => context.read<AppProvider>().setTabIndex(4),
                child: Container(
                  width: double.infinity,
                  margin: const EdgeInsets.only(bottom: 12),
                  padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
                  decoration: BoxDecoration(
                    color: Colors.blueGrey.withValues(alpha: 0.15),
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: Colors.blueGrey.withValues(alpha: 0.35)),
                  ),
                  child: const Row(children: [
                    Icon(Icons.bluetooth_searching, size: 18, color: Colors.blueGrey),
                    SizedBox(width: 10),
                    Expanded(child: Text('Not connected — tap to scan',
                        style: TextStyle(fontSize: 13, color: Colors.blueGrey))),
                    Icon(Icons.chevron_right, size: 18, color: Colors.blueGrey),
                  ]),
                ),
              ),

            // ── Device card ──────────────────────────────────────────────
            Opacity(
              opacity: connected ? 1.0 : 0.45,
              child: _DeviceCard(ble: ble, beacon: beacon,
                  saved: devs.byMac(ble.connectedMac)),
            ),
            const SizedBox(height: 10),

            // ── Value cards 2×2 ─────────────────────────────────────────
            Opacity(
              opacity: connected ? 1.0 : 0.45,
              child: _ValueCardsGrid(s: s),
            ),
            const SizedBox(height: 10),

            // ── RTC time row ─────────────────────────────────────────────
            if (connected)
              _RtcTimeRow(beacon: beacon),

            // ── Chart expand ─────────────────────────────────────────────
            Opacity(
              opacity: connected ? 1.0 : 0.45,
              child: _ChartExpandRow(
                expanded: _chartExpanded,
                onToggle: () => setState(() => _chartExpanded = !_chartExpanded),
              ),
            ),
            if (_chartExpanded) ...[
              const SizedBox(height: 6),
              Opacity(
                opacity: connected ? 1.0 : 0.45,
                // Bug 2 fix: live-only history, ClipRect, bounded Y
                child: _TempSparkline(points: beacon.liveTempHistory),
              ),
              const SizedBox(height: 8),
            ],

            // ── Storage bar ──────────────────────────────────────────────
            Opacity(
              opacity: connected ? 1.0 : 0.45,
              child: _StorageBar(
                used:     beacon.logUsed,
                total:    beacon.logTotal > 0 ? beacon.logTotal : logEntriesMax,
                circular: beacon.logCircular,
              ),
            ),
            const SizedBox(height: 10),

            // ── Error banner ─────────────────────────────────────────────
            if (beacon.lastError != null && connected) ...[
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
              const SizedBox(height: 10),
            ],

            // ── Actions ──────────────────────────────────────────────────
            Row(children: [
              Expanded(child: OutlinedButton.icon(
                onPressed: connected ? () => beacon.syncTime() : null,
                icon: const Icon(Icons.access_time, size: 16),
                label: const Text('Sync Time'),
              )),
              const SizedBox(width: 8),
              Expanded(child: OutlinedButton.icon(
                onPressed: connected ? () => _confirmReboot(context, beacon) : null,
                icon: const Icon(Icons.restart_alt, size: 16),
                label: const Text('Reboot'),
              )),
            ]),
          ]),
        ),
      ),
    );
  }

  Future<void> _confirmReboot(BuildContext ctx, BeaconProvider beacon) async {
    final ok = await showDialog<bool>(
      context: ctx,
      builder: (_) => AlertDialog(
        title: const Text('Reboot beacon?'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Reboot')),
        ],
      ),
    );
    if (ok == true) beacon.reboot();
  }
}

// ── RTC time row ─────────────────────────────────────────────────────────────

class _RtcTimeRow extends StatefulWidget {
  final BeaconProvider beacon;
  const _RtcTimeRow({required this.beacon});
  @override
  State<_RtcTimeRow> createState() => _RtcTimeRowState();
}

class _RtcTimeRowState extends State<_RtcTimeRow> {
  late Timer _tick;

  @override
  void initState() {
    super.initState();
    _tick = Timer.periodic(const Duration(seconds: 1), (_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() { _tick.cancel(); super.dispose(); }

  @override
  Widget build(BuildContext context) {
    final ts     = widget.beacon.beaconTimeS;
    final readAt = widget.beacon.beaconTimeReadAt;
    if (ts == null || readAt == null) return const SizedBox.shrink();

    // Advance displayed time by wall-clock seconds elapsed since last read
    final elapsed   = DateTime.now().difference(readAt).inSeconds;
    final beaconNow = DateTime.fromMillisecondsSinceEpoch((ts + elapsed) * 1000).toLocal();

    return Padding(
      padding: const EdgeInsets.only(bottom: 10),
      child: Card(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
          child: Row(children: [
            const Icon(Icons.schedule, size: 18),
            const SizedBox(width: 10),
            Text('Beacon RTC: ', style: Theme.of(context).textTheme.bodySmall),
            Text(
              '${beaconNow.year}-'
              '${beaconNow.month.toString().padLeft(2, '0')}-'
              '${beaconNow.day.toString().padLeft(2, '0')}  '
              '${beaconNow.hour.toString().padLeft(2, '0')}:'
              '${beaconNow.minute.toString().padLeft(2, '0')}:'
              '${beaconNow.second.toString().padLeft(2, '0')}',
              style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w600,
                  fontFamily: 'monospace'),
            ),
          ]),
        ),
      ),
    );
  }
}

// ── Device card ──────────────────────────────────────────────────────────────

class _DeviceCard extends StatelessWidget {
  final BleProvider ble;
  final BeaconProvider beacon;
  final dynamic saved;
  const _DeviceCard({required this.ble, required this.beacon, this.saved});

  @override
  Widget build(BuildContext context) {
    final connected = ble.isConnected;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Row(children: [
          CircleAvatar(
            backgroundColor:
                (connected ? Colors.green : Colors.grey).withValues(alpha: 0.15),
            child: Icon(
              connected ? Icons.bluetooth_connected : Icons.bluetooth_disabled,
              color: connected ? Colors.green : Colors.grey,
            ),
          ),
          const SizedBox(width: 12),
          Expanded(child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                connected
                    ? (ble.connectedName ?? ble.connectedMac)
                    : (saved?.name ?? 'No device'),
                style: Theme.of(context).textTheme.titleSmall,
              ),
              Text(connected ? ble.connectedMac : '—',
                  style: Theme.of(context).textTheme.labelSmall),
              Text(
                connected && beacon.fwVersion != null
                    ? 'FW: ${beacon.fwVersion}'
                    : connected ? 'Reading…' : '—',
                style: Theme.of(context).textTheme.labelSmall,
              ),
            ],
          )),
          if (connected) ...[
            if (beacon.txActive) _Chip('TX', Colors.red),
            if (beacon.status?.schedActive == 1) _Chip('SCHED', Colors.orange),
          ],
        ]),
      ),
    );
  }
}

class _Chip extends StatelessWidget {
  final String label;
  final Color  color;
  const _Chip(this.label, this.color);

  @override
  Widget build(BuildContext context) => Container(
    margin: const EdgeInsets.only(left: 4),
    padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
    decoration: BoxDecoration(
      color: color.withValues(alpha: 0.15),
      borderRadius: BorderRadius.circular(12),
    ),
    child: Text(label, style: TextStyle(color: color, fontSize: 11)),
  );
}

// ── Value cards 2×2 ──────────────────────────────────────────────────────────

class _ValueCardsGrid extends StatelessWidget {
  final dynamic s;
  const _ValueCardsGrid({required this.s});

  @override
  Widget build(BuildContext context) {
    final tc = (s?.tempC as double?) ?? double.nan;
    final tempColor = !tc.isNaN
        ? (tc >= 36.0 && tc <= 38.0 ? const Color(0xFF4CAF50)
            : (tc >= 35.0 && tc <= 39.0) ? const Color(0xFFFFC107)
            : const Color(0xFFF44336))
        : Colors.grey;
    final batPct = s?.batPct as int?;

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
          value: s != null ? '${tc.toStringAsFixed(1)} °C' : '—',
          icon: Icons.thermostat,
          color: tempColor,
        ),
        _ValueCard(
          label: 'Battery',
          value: s != null ? '${s.batMv} mV (${batPct ?? '?'}%)' : '—',
          icon: Icons.battery_full,
          color: batPct != null
              ? (batPct > 20 ? const Color(0xFF4CAF50) : const Color(0xFFF44336))
              : Colors.grey,
        ),
        _ValueCard(
          label: 'Uptime',
          value: s != null ? _fmtUptime(s.uptimeS as int) : '—',
          icon: Icons.timer_outlined,
          color: null,
        ),
        _ValueCard(
          label: 'Light',
          value: s != null ? '${s.lightRaw}' : '—',
          icon: Icons.light_mode_outlined,
          color: null,
        ),
      ],
    );
  }

  static String _fmtUptime(int sec) {
    final h = sec ~/ 3600;
    final m = (sec % 3600) ~/ 60;
    if (h > 0) return '${h}h ${m}m';
    return '${m}m';
  }
}

class _ValueCard extends StatelessWidget {
  final String  label;
  final String  value;
  final IconData icon;
  final Color?  color;
  const _ValueCard({required this.label, required this.value,
    required this.icon, this.color});

  @override
  Widget build(BuildContext context) => Card(
    child: Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      child: Row(children: [
        Icon(icon, size: 24,
            color: color ?? Theme.of(context).colorScheme.primary),
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

// ── Chart expand row ──────────────────────────────────────────────────────────

class _ChartExpandRow extends StatelessWidget {
  final bool expanded;
  final VoidCallback onToggle;
  const _ChartExpandRow({required this.expanded, required this.onToggle});

  @override
  Widget build(BuildContext context) => InkWell(
    onTap: onToggle,
    borderRadius: BorderRadius.circular(8),
    child: Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(children: [
        Icon(expanded ? Icons.expand_less : Icons.expand_more, size: 18,
            color: Theme.of(context).colorScheme.primary),
        const SizedBox(width: 6),
        Text(expanded ? 'Hide temperature chart' : 'Show temperature chart',
            style: TextStyle(fontSize: 13,
                color: Theme.of(context).colorScheme.primary)),
      ]),
    ),
  );
}

// ── Temperature sparkline (Bug 2: ClipRect + live-only history + bounded Y) ──

class _TempSparkline extends StatelessWidget {
  final List<ChartPoint> points;
  const _TempSparkline({required this.points});

  @override
  Widget build(BuildContext context) {
    if (points.isEmpty) {
      return Container(
        height: 120,
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: Colors.grey.withValues(alpha: 0.05),
          borderRadius: BorderRadius.circular(8),
          border: Border.all(color: Colors.grey.withValues(alpha: 0.2)),
        ),
        child: const Text('No data', style: TextStyle(color: Colors.grey, fontSize: 13)),
      );
    }

    final spots = points.asMap().entries
        .map((e) => FlSpot(e.key.toDouble(), e.value.value))
        .toList();

    // Auto-scale Y from live data with ±2°C padding
    final values = points.map((p) => p.value);
    final rawMin = values.reduce((a, b) => a < b ? a : b);
    final rawMax = values.reduce((a, b) => a > b ? a : b);
    final yMin = (rawMin - 2).clamp(-20.0, 80.0);
    final yMax = (rawMax + 2).clamp(yMin + 1, 80.0);

    return ClipRect(
      child: SizedBox(
        height: 140,
        child: LineChart(LineChartData(
          minY: yMin, maxY: yMax,
          gridData: const FlGridData(show: false),
          borderData: FlBorderData(show: false),
          clipData: const FlClipData.all(),
          titlesData: FlTitlesData(
            leftTitles: AxisTitles(sideTitles: SideTitles(
              showTitles: true, reservedSize: 32,
              getTitlesWidget: (v, _) => Text('${v.round()}°',
                  style: const TextStyle(fontSize: 10)),
            )),
            bottomTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
            topTitles:    const AxisTitles(sideTitles: SideTitles(showTitles: false)),
            rightTitles:  const AxisTitles(sideTitles: SideTitles(showTitles: false)),
          ),
          lineBarsData: [LineChartBarData(
            spots: spots,
            isCurved: true,
            color: const Color(0xFF4CAF50),
            barWidth: 2,
            dotData: const FlDotData(show: false),
            belowBarData: BarAreaData(
              show: true,
              color: const Color(0xFF4CAF50).withValues(alpha: 0.12),
            ),
          )],
        )),
      ),
    );
  }
}

// ── Storage bar ───────────────────────────────────────────────────────────────

class _StorageBar extends StatelessWidget {
  final int  used;
  final int  total;
  final bool circular;
  const _StorageBar({required this.used, required this.total, this.circular = false});

  @override
  Widget build(BuildContext context) {
    final knownTotal    = total > 0;
    final overwriting   = circular && used > total;
    final usedDisp      = overwriting ? total : used;
    final pct           = knownTotal ? (usedDisp / total).clamp(0.0, 1.0) : 0.0;
    final color = overwriting ? Colors.orange
        : pct > 0.9 ? Colors.red
        : pct > 0.7 ? Colors.orange
        : Colors.green;

    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(children: [
        const Text('Log Storage',
            style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600)),
        const Spacer(),
        if (overwriting)
          const Text('⟳ Circular overwrite active',
              style: TextStyle(fontSize: 11, color: Colors.orange))
        else
          Text(knownTotal ? '$used / $total records' : '— records',
              style: Theme.of(context).textTheme.labelSmall),
      ]),
      const SizedBox(height: 6),
      ClipRRect(
        borderRadius: BorderRadius.circular(4),
        child: LinearProgressIndicator(
          value: pct,
          minHeight: 8,
          backgroundColor: Colors.grey.withValues(alpha: 0.2),
          valueColor: AlwaysStoppedAnimation(
            knownTotal ? (usedDisp > 0 ? color : Colors.green) : Colors.grey),
        ),
      ),
      const SizedBox(height: 4),
      Text(
        overwriting
            ? '100% full — oldest data overwritten'
            : knownTotal
                ? (used > 0 ? '${(pct * 100).round()}% used' : '0% used')
                : 'No data',
        style: Theme.of(context).textTheme.labelSmall,
      ),
    ]);
  }
}

// ── RSSI chip ─────────────────────────────────────────────────────────────────

class _RssiChip extends StatelessWidget {
  final int  rssi;
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
