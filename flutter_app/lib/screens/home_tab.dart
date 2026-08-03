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

    final app = context.watch<AppProvider>();

    return Scaffold(
      appBar: AppBar(
        title: Row(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.baseline,
          textBaseline: TextBaseline.alphabetic,
          children: [
            const Text('Home'),
            if (app.appVersion.isNotEmpty) ...[
              const SizedBox(width: 8),
              Text(
                'v${app.appVersion}',
                style: TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.normal,
                  color: Theme.of(context).colorScheme.onSurfaceVariant,
                ),
              ),
            ],
          ],
        ),
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
              child: _ValueCardsGrid(
                s: s,
                beacon: beacon,
                onTapChart: connected
                    ? (type) => _openChart(context, type, beacon)
                    : null,
              ),
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

  void _openChart(BuildContext ctx, int type, BeaconProvider beacon) {
    showModalBottomSheet(
      context: ctx,
      isScrollControlled: true,
      backgroundColor: Colors.transparent,
      useSafeArea: true,
      builder: (_) => _ChartSheet(type: type, beacon: beacon),
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
              style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w600),
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
  final BeaconProvider beacon;
  final void Function(int type)? onTapChart;
  const _ValueCardsGrid({
    required this.s, required this.beacon, this.onTapChart});

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
          onTap: onTapChart != null ? () => onTapChart!(0) : null,
        ),
        _ValueCard(
          label: 'Battery',
          value: s != null ? '${s.batMv} mV (${batPct ?? '?'}%)' : '—',
          icon: Icons.battery_full,
          color: batPct != null
              ? (batPct > 20 ? const Color(0xFF4CAF50) : const Color(0xFFF44336))
              : Colors.grey,
          onTap: onTapChart != null ? () => onTapChart!(1) : null,
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
          onTap: onTapChart != null ? () => onTapChart!(2) : null,
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
  final String   label;
  final String   value;
  final IconData icon;
  final Color?   color;
  final VoidCallback? onTap;
  const _ValueCard({required this.label, required this.value,
    required this.icon, this.color, this.onTap});

  @override
  Widget build(BuildContext context) => Card(
    child: InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(12),
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
          if (onTap != null)
            Icon(Icons.show_chart_outlined, size: 14, color: Colors.grey[400]),
        ]),
      ),
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
          const Row(mainAxisSize: MainAxisSize.min, children: [
            Icon(Icons.loop, size: 13, color: Colors.orange),
            SizedBox(width: 3),
            Text('Circular overwrite', style: TextStyle(fontSize: 11, color: Colors.orange)),
          ])
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

// ── Chart bottom sheet ────────────────────────────────────────────────────────

class _ChartSheet extends StatefulWidget {
  final int type;             // 0=temp  1=battery  2=light
  final BeaconProvider beacon;
  const _ChartSheet({required this.type, required this.beacon});

  @override
  State<_ChartSheet> createState() => _ChartSheetState();
}

class _ChartSheetState extends State<_ChartSheet> {
  @override
  void initState() {
    super.initState();
    widget.beacon.addListener(_rebuild);
  }

  @override
  void dispose() {
    widget.beacon.removeListener(_rebuild);
    super.dispose();
  }

  void _rebuild() { if (mounted) setState(() {}); }

  static const _titles = ['Temperature', 'Battery voltage', 'Light'];
  static const _units  = ['°C', 'mV', 'raw'];
  static const _colors = [Color(0xFF4CAF50), Color(0xFF2196F3), Color(0xFFFFC107)];
  static const _icons  = [
    Icons.thermostat, Icons.battery_full, Icons.light_mode_outlined];

  @override
  Widget build(BuildContext context) {
    final beacon = widget.beacon;
    final t = widget.type;

    final liveData = t == 0 ? beacon.liveTempHistory
        : t == 1 ? beacon.liveBatHistory
        : beacon.liveLightHistory;
    final logData = t == 0 ? beacon.logTempHistory
        : t == 1 ? beacon.logBatHistory
        : beacon.logLightHistory;

    final isLive = liveData.isNotEmpty;
    final points  = isLive ? liveData : logData;
    final color   = _colors[t];

    return DraggableScrollableSheet(
      initialChildSize: 0.75,
      minChildSize: 0.35,
      maxChildSize: 1.0,
      builder: (ctx, controller) => Container(
        decoration: BoxDecoration(
          color: Theme.of(context).colorScheme.surface,
          borderRadius: const BorderRadius.vertical(top: Radius.circular(20)),
        ),
        child: CustomScrollView(
          controller: controller,
          slivers: [SliverToBoxAdapter(
            child: Column(children: [
              // Drag handle
              Container(
                margin: const EdgeInsets.symmetric(vertical: 10),
                width: 40, height: 4,
                decoration: BoxDecoration(
                  color: Colors.grey[400],
                  borderRadius: BorderRadius.circular(2),
                ),
              ),
              // Title row
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
                child: Row(children: [
                  Icon(_icons[t], color: color, size: 22),
                  const SizedBox(width: 10),
                  Text(_titles[t],
                      style: const TextStyle(
                          fontSize: 17, fontWeight: FontWeight.w600)),
                  const Spacer(),
                  Container(
                    padding: const EdgeInsets.symmetric(
                        horizontal: 10, vertical: 4),
                    decoration: BoxDecoration(
                      color: (isLive ? Colors.green : Colors.blue)
                          .withValues(alpha: 0.15),
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: Row(mainAxisSize: MainAxisSize.min, children: [
                      Icon(isLive ? Icons.circle : Icons.history,
                          size: 10,
                          color: isLive ? Colors.green : Colors.blue),
                      const SizedBox(width: 4),
                      Text(isLive ? 'Live' : 'Log',
                          style: TextStyle(
                              fontSize: 12,
                              color: isLive ? Colors.green : Colors.blue)),
                    ]),
                  ),
                ]),
              ),
              const SizedBox(height: 8),
              // Stats row
              if (points.isNotEmpty)
                _StatsRow(points: points, unit: _units[t], color: color),
              const SizedBox(height: 12),
              // Chart
              SizedBox(
                height: 300,
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(8, 0, 16, 8),
                  child: points.isEmpty
                      ? const Center(child: Text('No data available',
                            style: TextStyle(color: Colors.grey)))
                      : _buildChart(points, color),
                ),
              ),
              SizedBox(height: MediaQuery.of(context).padding.bottom + 16),
            ]),
          )],
        ),
      ),
    );
  }

  Widget _buildChart(List<ChartPoint> points, Color color) {
    final t0 = points.first.ts.millisecondsSinceEpoch / 60000.0;
    final spots = points.map((p) =>
        FlSpot(p.ts.millisecondsSinceEpoch / 60000.0 - t0, p.value)).toList();

    final values = points.map((p) => p.value);
    final rawMin = values.reduce((a, b) => a < b ? a : b);
    final rawMax = values.reduce((a, b) => a > b ? a : b);
    final pad = ((rawMax - rawMin) * 0.15).clamp(1.0, 20.0);
    final yMin = rawMin - pad;
    final yMax = rawMax + pad;
    final xMax = spots.last.x.clamp(0.01, double.infinity);
    final xInterval = (xMax / 4).clamp(0.5, double.infinity);

    return LineChart(LineChartData(
      minX: 0, maxX: xMax,
      minY: yMin, maxY: yMax,
      clipData: const FlClipData.all(),
      gridData: FlGridData(
        show: true,
        horizontalInterval: ((yMax - yMin) / 5).clamp(0.1, double.infinity),
        getDrawingHorizontalLine: (_) =>
            FlLine(color: Colors.grey.withValues(alpha: 0.2), strokeWidth: 1),
        getDrawingVerticalLine: (_) =>
            FlLine(color: Colors.grey.withValues(alpha: 0.15), strokeWidth: 1),
      ),
      borderData: FlBorderData(
        show: true,
        border: Border(
          bottom: BorderSide(color: Colors.grey.withValues(alpha: 0.3)),
          left:   BorderSide(color: Colors.grey.withValues(alpha: 0.3)),
        ),
      ),
      titlesData: FlTitlesData(
        leftTitles: AxisTitles(sideTitles: SideTitles(
          showTitles: true, reservedSize: 44,
          getTitlesWidget: (v, _) => Text(v.toStringAsFixed(1),
              style: const TextStyle(fontSize: 10)),
        )),
        bottomTitles: AxisTitles(sideTitles: SideTitles(
          showTitles: true, reservedSize: 24,
          interval: xInterval,
          getTitlesWidget: (v, _) {
            final m = v.round();
            return Padding(
              padding: const EdgeInsets.only(top: 4),
              child: Text(m < 60 ? '${m}хв' : '${m ~/ 60}год',
                  style: const TextStyle(fontSize: 10)),
            );
          },
        )),
        topTitles:   const AxisTitles(sideTitles: SideTitles(showTitles: false)),
        rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
      ),
      lineTouchData: LineTouchData(
        touchTooltipData: LineTouchTooltipData(
          getTooltipItems: (ts) => ts.map((s) => LineTooltipItem(
            s.y.toStringAsFixed(2),
            TextStyle(color: color, fontWeight: FontWeight.bold),
          )).toList(),
        ),
      ),
      lineBarsData: [LineChartBarData(
        spots: spots,
        isCurved: true,
        curveSmoothness: 0.3,
        color: color,
        barWidth: 2.5,
        dotData: FlDotData(
          show: spots.length <= 40,
          getDotPainter: (_, __, ___, ____) =>
              FlDotCirclePainter(radius: 3, color: color, strokeWidth: 0,
                  strokeColor: color),
        ),
        belowBarData: BarAreaData(
          show: true,
          color: color.withValues(alpha: 0.12),
        ),
      )],
    ));
  }
}

class _StatsRow extends StatelessWidget {
  final List<ChartPoint> points;
  final String unit;
  final Color  color;
  const _StatsRow({required this.points, required this.unit, required this.color});

  @override
  Widget build(BuildContext context) {
    final values = points.map((p) => p.value).toList();
    final minV = values.reduce((a, b) => a < b ? a : b);
    final maxV = values.reduce((a, b) => a > b ? a : b);
    final avgV = values.reduce((a, b) => a + b) / values.length;

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16),
      child: Row(children: [
        _StatBadge('MIN', minV, unit, color),
        const SizedBox(width: 8),
        _StatBadge('AVG', avgV, unit, color),
        const SizedBox(width: 8),
        _StatBadge('MAX', maxV, unit, color),
      ]),
    );
  }
}

class _StatBadge extends StatelessWidget {
  final String label;
  final double value;
  final String unit;
  final Color  color;
  const _StatBadge(this.label, this.value, this.unit, this.color);

  @override
  Widget build(BuildContext context) => Expanded(
    child: Container(
      padding: const EdgeInsets.symmetric(vertical: 6),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(8),
      ),
      child: Column(children: [
        Text(label, style: TextStyle(fontSize: 10, color: color,
            fontWeight: FontWeight.w700)),
        Text(value.toStringAsFixed(1),
            style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600)),
        Text(unit, style: TextStyle(fontSize: 10, color: Colors.grey[600])),
      ]),
    ),
  );
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
