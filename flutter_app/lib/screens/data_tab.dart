import 'dart:io';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:fl_chart/fl_chart.dart';
import 'package:share_plus/share_plus.dart';
import 'package:path_provider/path_provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';

class DataTab extends StatefulWidget {
  const DataTab({super.key});
  @override
  State<DataTab> createState() => _DataTabState();
}

class _DataTabState extends State<DataTab> {
  int _chartIndex = 0;

  static const _chartLabels = ['Temperature', 'Battery', 'Light', 'Accel'];
  static const _chartIcons  = [
    Icons.thermostat, Icons.battery_full,
    Icons.light_mode_outlined, Icons.vibration,
  ];

  @override
  Widget build(BuildContext context) {
    final ble    = context.watch<BleProvider>();
    final beacon = context.watch<BeaconProvider>();

    return Scaffold(
      appBar: AppBar(
        title: const Text('Data'),
        actions: [
          if (beacon.isDownloadingLog)
            Padding(
              padding: const EdgeInsets.all(14),
              child: SizedBox(width: 20, height: 20,
                child: CircularProgressIndicator(
                  strokeWidth: 2,
                  value: beacon.downloadProgress > 0 ? beacon.downloadProgress : null,
                )),
            )
          else
            IconButton(
              icon: const Icon(Icons.download),
              tooltip: 'Download log',
              onPressed: ble.isConnected ? beacon.downloadLog : null,
            ),
          IconButton(
            icon: const Icon(Icons.share),
            tooltip: 'Export CSV',
            onPressed: beacon.logEntries.isEmpty ? null : () => _exportCsv(context, beacon),
          ),
        ],
      ),
      body: Column(
        children: [
          // Download progress bar
          if (beacon.isDownloadingLog)
            LinearProgressIndicator(value: beacon.downloadProgress),

          // Log info row
          if (beacon.logEntries.isNotEmpty || beacon.logTotal > 0)
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              child: Row(children: [
                Text('${beacon.logEntries.length} records',
                    style: Theme.of(context).textTheme.bodySmall),
                const Spacer(),
                Text('${beacon.logUsed} / ${beacon.logTotal} stored',
                    style: Theme.of(context).textTheme.bodySmall),
              ]),
            ),

          // Chart selector chips
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              child: Row(
                children: List.generate(_chartLabels.length, (i) => Padding(
                  padding: const EdgeInsets.only(right: 8),
                  child: FilterChip(
                    avatar: Icon(_chartIcons[i], size: 14),
                    label: Text(_chartLabels[i]),
                    selected: _chartIndex == i,
                    onSelected: (_) => setState(() => _chartIndex = i),
                  ),
                )),
              ),
            ),
          ),

          const SizedBox(height: 8),

          // Chart area
          Expanded(child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 8),
            child: _buildChart(context, beacon, _chartIndex),
          )),

          if (!ble.isConnected && beacon.logEntries.isEmpty)
            const Padding(
              padding: EdgeInsets.all(16),
              child: Text('Connect a beacon and press Download to view data',
                  textAlign: TextAlign.center),
            ),
        ],
      ),
    );
  }

  Widget _buildChart(BuildContext context, BeaconProvider beacon, int idx) {
    switch (idx) {
      case 0: return _LineChartCard(
        title: 'Temperature (°C)',
        points: beacon.tempHistory,
        lineColor: const Color(0xFFF44336),
        minY: 30, maxY: 42,
        formatY: (v) => '${v.toStringAsFixed(1)}°',
      );
      case 1: return _LineChartCard(
        title: 'Battery (mV)',
        points: beacon.batHistory,
        lineColor: const Color(0xFF4CAF50),
        minY: 2800, maxY: 4300,
        formatY: (v) => '${v.round()}',
      );
      case 2: return _LineChartCard(
        title: 'Light (raw)',
        points: beacon.lightHistory,
        lineColor: const Color(0xFFFFC107),
        formatY: (v) => v.round().toString(),
      );
      case 3: return _AccelChart(beacon: beacon);
      default: return const SizedBox.shrink();
    }
  }

  Future<void> _exportCsv(BuildContext context, BeaconProvider beacon) async {
    final buf = StringBuffer('ts_unix,temp_c,bat_mv,bat_pct,light_raw\n');
    for (final e in beacon.logEntries) {
      buf.write([
        e['ts'] ?? '',
        e['temp_c'] ?? '',
        e['bat_mv'] ?? '',
        e['bat_pct'] ?? '',
        e['light_raw'] ?? '',
      ].join(','));
      buf.write('\n');
    }
    try {
      final dir  = await getTemporaryDirectory();
      final file = File('${dir.path}/beacon_log.csv');
      await file.writeAsString(buf.toString());
      await Share.shareXFiles([XFile(file.path)], text: 'Beacon Log CSV');
    } catch (e) {
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Export failed: $e'), backgroundColor: Colors.red));
      }
    }
  }
}

// ── Line chart card ────────────────────────────────────────────────────────

class _LineChartCard extends StatelessWidget {
  final String title;
  final List<ChartPoint> points;
  final Color lineColor;
  final double? minY;
  final double? maxY;
  final String Function(double) formatY;

  const _LineChartCard({
    required this.title, required this.points, required this.lineColor,
    this.minY, this.maxY, required this.formatY,
  });

  @override
  Widget build(BuildContext context) {
    if (points.isEmpty) {
      return Center(child: Text('No data yet — download log or wait for live data',
          style: Theme.of(context).textTheme.bodySmall, textAlign: TextAlign.center));
    }

    final spots = _toSpots(points);
    final minX  = spots.first.x;
    final maxX  = spots.last.x;
    double yMin = minY ?? spots.map((s) => s.y).reduce((a, b) => a < b ? a : b) - 1;
    double yMax = maxY ?? spots.map((s) => s.y).reduce((a, b) => a > b ? a : b) + 1;
    if (yMax <= yMin) yMax = yMin + 1;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
          child: Text(title, style: const TextStyle(fontWeight: FontWeight.bold)),
        ),
        Expanded(
          child: LineChart(
            LineChartData(
              minX: minX, maxX: maxX, minY: yMin, maxY: yMax,
              gridData: FlGridData(
                show: true,
                getDrawingHorizontalLine: (_) => FlLine(
                  color: Colors.white12, strokeWidth: 1),
                getDrawingVerticalLine: (_) => FlLine(
                  color: Colors.white12, strokeWidth: 1),
              ),
              borderData: FlBorderData(
                border: const Border(
                  bottom: BorderSide(color: Colors.white24),
                  left:   BorderSide(color: Colors.white24),
                ),
              ),
              titlesData: FlTitlesData(
                leftTitles: AxisTitles(
                  sideTitles: SideTitles(
                    showTitles: true,
                    reservedSize: 42,
                    getTitlesWidget: (v, _) => Text(formatY(v),
                        style: const TextStyle(fontSize: 10, color: Colors.white54)),
                  ),
                ),
                bottomTitles: AxisTitles(
                  sideTitles: SideTitles(
                    showTitles: true,
                    reservedSize: 22,
                    interval: (maxX - minX) / 5,
                    getTitlesWidget: (v, _) {
                      final dt = DateTime.fromMillisecondsSinceEpoch(v.toInt() * 1000);
                      return Text('${dt.hour}:${dt.minute.toString().padLeft(2, '0')}',
                          style: const TextStyle(fontSize: 9, color: Colors.white54));
                    },
                  ),
                ),
                rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
                topTitles:   const AxisTitles(sideTitles: SideTitles(showTitles: false)),
              ),
              lineBarsData: [
                LineChartBarData(
                  spots: spots,
                  color: lineColor,
                  barWidth: 1.5,
                  dotData: FlDotData(show: spots.length <= 50),
                  belowBarData: BarAreaData(
                    show: true,
                    color: lineColor.withValues(alpha: 0.08),
                  ),
                ),
              ],
              lineTouchData: LineTouchData(
                touchTooltipData: LineTouchTooltipData(
                  getTooltipItems: (spots) => spots.map((s) => LineTooltipItem(
                    formatY(s.y), const TextStyle(color: Colors.white, fontSize: 12),
                  )).toList(),
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }

  List<FlSpot> _toSpots(List<ChartPoint> pts) {
    return pts.map((p) => FlSpot(
      p.ts.millisecondsSinceEpoch / 1000,
      p.value,
    )).toList();
  }
}

// ── Accel chart (3 lines: X, Y, Z) ────────────────────────────────────────

class _AccelChart extends StatelessWidget {
  final BeaconProvider beacon;
  const _AccelChart({required this.beacon});

  @override
  Widget build(BuildContext context) {
    final hasData = beacon.accelXHistory.isNotEmpty ||
        beacon.accelYHistory.isNotEmpty || beacon.accelZHistory.isNotEmpty;

    if (!hasData) {
      return Center(child: Text('No accel data yet',
          style: Theme.of(context).textTheme.bodySmall));
    }

    final allPts = [
      ...beacon.accelXHistory, ...beacon.accelYHistory, ...beacon.accelZHistory,
    ];
    final minX = allPts.map((p) => p.ts.millisecondsSinceEpoch / 1000.0).reduce((a, b) => a < b ? a : b);
    final maxX = allPts.map((p) => p.ts.millisecondsSinceEpoch / 1000.0).reduce((a, b) => a > b ? a : b);
    final minY = allPts.map((p) => p.value).reduce((a, b) => a < b ? a : b) - 0.1;
    final maxY = allPts.map((p) => p.value).reduce((a, b) => a > b ? a : b) + 0.1;

    FlSpot toSpot(ChartPoint p) =>
        FlSpot(p.ts.millisecondsSinceEpoch / 1000.0, p.value);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Padding(
          padding: EdgeInsets.symmetric(horizontal: 8, vertical: 4),
          child: Row(children: [
            Text('Accelerometer (g)', style: TextStyle(fontWeight: FontWeight.bold)),
            Spacer(),
            _Legend('X', Color(0xFFF44336)),
            SizedBox(width: 8),
            _Legend('Y', Color(0xFF4CAF50)),
            SizedBox(width: 8),
            _Legend('Z', Color(0xFF2196F3)),
          ]),
        ),
        Expanded(
          child: LineChart(LineChartData(
            minX: minX, maxX: maxX,
            minY: minY, maxY: maxY,
            gridData: FlGridData(
              show: true,
              getDrawingHorizontalLine: (_) => const FlLine(color: Colors.white12, strokeWidth: 1),
              getDrawingVerticalLine:   (_) => const FlLine(color: Colors.white12, strokeWidth: 1),
            ),
            borderData: FlBorderData(border: const Border(
              bottom: BorderSide(color: Colors.white24),
              left:   BorderSide(color: Colors.white24),
            )),
            titlesData: FlTitlesData(
              leftTitles: AxisTitles(sideTitles: SideTitles(
                showTitles: true, reservedSize: 36,
                getTitlesWidget: (v, _) => Text(v.toStringAsFixed(1),
                    style: const TextStyle(fontSize: 10, color: Colors.white54)),
              )),
              bottomTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
              rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
              topTitles:   const AxisTitles(sideTitles: SideTitles(showTitles: false)),
            ),
            lineBarsData: [
              if (beacon.accelXHistory.isNotEmpty)
                _accelBar(beacon.accelXHistory.map(toSpot).toList(), const Color(0xFFF44336)),
              if (beacon.accelYHistory.isNotEmpty)
                _accelBar(beacon.accelYHistory.map(toSpot).toList(), const Color(0xFF4CAF50)),
              if (beacon.accelZHistory.isNotEmpty)
                _accelBar(beacon.accelZHistory.map(toSpot).toList(), const Color(0xFF2196F3)),
            ],
          )),
        ),
      ],
    );
  }

  LineChartBarData _accelBar(List<FlSpot> spots, Color color) => LineChartBarData(
    spots: spots, color: color, barWidth: 1.2,
    dotData: FlDotData(show: spots.length <= 30),
  );
}

class _Legend extends StatelessWidget {
  final String label;
  final Color color;
  const _Legend(this.label, this.color);
  @override
  Widget build(BuildContext context) => Row(children: [
    Container(width: 12, height: 3, color: color),
    const SizedBox(width: 4),
    Text(label, style: TextStyle(fontSize: 11, color: color)),
  ]);
}
