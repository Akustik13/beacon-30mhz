import 'dart:io';
import 'dart:math';
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
  int  _chartIndex = 0;
  bool _smoothLine = true;

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
      appBar: AppBar(title: const Text('Data')),
      body: Column(
        children: [
          // ── Download progress bar ─────────────────────────────────────
          if (beacon.isDownloadingLog)
            LinearProgressIndicator(value:
                beacon.downloadProgress > 0 ? beacon.downloadProgress : null),

          // ── Primary action buttons ────────────────────────────────────
          Padding(
            padding: const EdgeInsets.fromLTRB(12, 10, 12, 4),
            child: Row(children: [
              Expanded(
                flex: 3,
                child: FilledButton.icon(
                  style: FilledButton.styleFrom(minimumSize: const Size(0, 48)),
                  onPressed: (ble.isConnected && !beacon.isDownloadingLog)
                      ? beacon.downloadLog : null,
                  icon: beacon.isDownloadingLog
                      ? const SizedBox(width: 16, height: 16,
                          child: CircularProgressIndicator(
                              strokeWidth: 2, color: Colors.white))
                      : const Icon(Icons.download, size: 18),
                  label: Text(
                    beacon.isDownloadingLog
                        ? '${(beacon.downloadProgress * 100).round()}%'
                        : 'Download from beacon',
                    style: const TextStyle(fontSize: 14),
                  ),
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                flex: 2,
                child: OutlinedButton.icon(
                  style: OutlinedButton.styleFrom(minimumSize: const Size(0, 48)),
                  onPressed: beacon.logEntries.isEmpty
                      ? null : () => _exportCsv(context, beacon),
                  icon: const Icon(Icons.ios_share, size: 18),
                  label: const Text('Export CSV', style: TextStyle(fontSize: 14)),
                ),
              ),
            ]),
          ),

          // ── Erase flash button ────────────────────────────────────────
          Padding(
            padding: const EdgeInsets.fromLTRB(12, 0, 12, 6),
            child: OutlinedButton.icon(
              style: OutlinedButton.styleFrom(
                foregroundColor: Colors.red,
                minimumSize: const Size(double.infinity, 44),
              ),
              onPressed: (ble.isConnected && !beacon.isDownloadingLog && !beacon.isBusy)
                  ? () => _confirmErase(context, beacon) : null,
              icon: const Icon(Icons.delete_sweep, size: 18),
              label: const Text('Erase flash memory', style: TextStyle(fontSize: 14)),
            ),
          ),

          // ── Log info row ──────────────────────────────────────────────
          if (beacon.logEntries.isNotEmpty || beacon.logTotal > 0)
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 2, 16, 0),
              child: Row(children: [
                Text('${beacon.logEntries.length} records downloaded',
                    style: Theme.of(context).textTheme.labelSmall),
                const Spacer(),
                Text('${beacon.logUsed} / ${beacon.logTotal} on device',
                    style: Theme.of(context).textTheme.labelSmall),
              ]),
            ),
          if (beacon.logEntries.isNotEmpty)
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 0, 16, 2),
              child: Text(
                '${beacon.logTempCount} temp · '
                '${beacon.logBatCount} batt · '
                '${beacon.logLightCount} light · '
                '${beacon.logAccelCount} accel',
                style: Theme.of(context).textTheme.labelSmall
                    ?.copyWith(color: Colors.grey),
              ),
            ),

          const Divider(height: 12),

          // ── Chart selector chips + smooth toggle ──────────────────────
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              child: Row(
                children: [
                  ...List.generate(_chartLabels.length, (i) => Padding(
                    padding: const EdgeInsets.only(right: 8),
                    child: FilterChip(
                      avatar: Icon(_chartIcons[i], size: 14),
                      label: Text(_chartLabels[i]),
                      selected: _chartIndex == i,
                      onSelected: (_) => setState(() => _chartIndex = i),
                    ),
                  )),
                  // Smooth / Raw toggle (hidden for accel chart)
                  if (_chartIndex != 3)
                    FilterChip(
                      avatar: Icon(
                        _smoothLine ? Icons.gesture : Icons.show_chart,
                        size: 14,
                      ),
                      label: Text(_smoothLine ? 'Smooth' : 'Raw'),
                      selected: _smoothLine,
                      onSelected: (_) => setState(() => _smoothLine = !_smoothLine),
                    ),
                ],
              ),
            ),
          ),

          const SizedBox(height: 6),

          // ── Chart area ────────────────────────────────────────────────
          Expanded(child: Padding(
            padding: EdgeInsets.fromLTRB(8, 0, 8,
                MediaQuery.of(context).padding.bottom + 8),
            child: beacon.logEntries.isEmpty && !beacon.isDownloadingLog
                ? Center(child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Icon(Icons.show_chart, size: 64,
                          color: Colors.grey.withOpacity(0.25)),
                      const SizedBox(height: 12),
                      Text(
                        ble.isConnected
                            ? 'Press "Download from beacon" to load data'
                            : 'Connect a beacon, then download data',
                        style: Theme.of(context).textTheme.bodySmall,
                        textAlign: TextAlign.center,
                      ),
                    ],
                  ))
                : AnimatedSwitcher(
                    duration: const Duration(milliseconds: 350),
                    transitionBuilder: (child, animation) =>
                        FadeTransition(opacity: animation, child: child),
                    child: KeyedSubtree(
                      key: ValueKey('$_chartIndex/$_smoothLine'),
                      child: _buildChart(context, beacon, _chartIndex),
                    ),
                  ),
          )),
        ],
      ),
    );
  }

  Widget _buildChart(BuildContext context, BeaconProvider beacon, int idx) {
    switch (idx) {
      case 0: return _LineChartCard(
        title: 'Temperature', unit: '°C',
        points: beacon.logTempHistory,
        lineColor: const Color(0xFFEF5350),
        formatY: (v) => v.toStringAsFixed(1),
        smoothLine: _smoothLine,
      );
      case 1: return _LineChartCard(
        title: 'Battery', unit: 'mV',
        points: beacon.logBatHistory,
        lineColor: const Color(0xFF66BB6A),
        formatY: (v) => v.round().toString(),
        smoothLine: _smoothLine,
      );
      case 2: return _LineChartCard(
        title: 'Light', unit: 'raw',
        points: beacon.logLightHistory,
        lineColor: const Color(0xFFFFCA28),
        formatY: (v) => v.round().toString(),
        smoothLine: _smoothLine,
      );
      case 3: return _AccelChart(beacon: beacon);
      default: return const SizedBox.shrink();
    }
  }

  void _confirmErase(BuildContext ctx, BeaconProvider beacon) {
    final usedNow  = beacon.logUsed;
    final totalNow = beacon.logTotal > 0 ? beacon.logTotal : 1;
    final pctNow   = (usedNow * 100 ~/ totalNow).clamp(0, 100);
    showDialog(
      context: ctx,
      builder: (_) => AlertDialog(
        title: const Text('Erase flash memory?'),
        content: Text(
            'All logged data on the beacon will be permanently deleted.\n\n'
            'Records stored: $usedNow / $totalNow ($pctNow% full).\n\n'
            'Export CSV first if you want to save the data.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
          FilledButton(
            style: FilledButton.styleFrom(backgroundColor: Colors.red),
            onPressed: () async {
              Navigator.pop(ctx);
              final ok = await beacon.eraseLog();
              if (ctx.mounted) {
                ScaffoldMessenger.of(ctx).showSnackBar(SnackBar(
                  content: Text(ok
                      ? 'Erased $usedNow records ($pctNow% of capacity)'
                      : 'Erase failed'),
                  backgroundColor: ok ? Colors.green : Colors.red,
                  duration: const Duration(seconds: 4),
                ));
              }
            },
            child: const Text('Erase'),
          ),
        ],
      ),
    );
  }

  Future<void> _exportCsv(BuildContext context, BeaconProvider beacon) async {
    final buf = StringBuffer('ts_unix,temp_c,bat_mv,bat_pct,light_raw\n');
    for (final e in beacon.logEntries) {
      buf.write([
        e['ts'] ?? '', e['temp_c'] ?? '', e['bat_mv'] ?? '',
        e['bat_pct'] ?? '', e['light_raw'] ?? '',
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

// ── Line chart card with pinch-to-zoom (Listener-based, no gesture conflicts)

class _LineChartCard extends StatefulWidget {
  final String title;
  final String unit;
  final List<ChartPoint> points;
  final Color lineColor;
  final String Function(double) formatY;
  final bool fullscreen;
  final bool smoothLine;

  const _LineChartCard({
    required this.title,
    required this.unit,
    required this.points,
    required this.lineColor,
    required this.formatY,
    this.fullscreen = false,
    this.smoothLine = true,
  });

  @override
  State<_LineChartCard> createState() => _LineChartCardState();
}

class _LineChartCardState extends State<_LineChartCard> {
  // Zoom / pan state
  double _zoom    = 1.0;  // 1.0 = full data range visible
  double _panFrac = 0.0;  // 0.0 = leftmost, 1.0 = rightmost of zoomed window
  double _yOffset = 0.0;  // Y offset in data units (+ = view moves up)

  // Pointer tracking (Listener-based — no gesture arena conflict)
  final _pts = <int, Offset>{};
  double  _gsZoom      = 1.0;
  double  _gsDist      = 0.0;
  Offset  _gsCenter    = Offset.zero;
  Offset? _lastSinglePos;   // for single-finger pan

  // Data range — set each build, used by pointer handlers
  double _spanX     = 1.0;
  double _dataMinX  = 0.0;
  double _lastYRange = 1.0; // current yMax-yMin (set each build)

  @override
  void didUpdateWidget(_LineChartCard old) {
    super.didUpdateWidget(old);
    if (widget.points != old.points) {
      _zoom = 1.0; _panFrac = 0.0; _yOffset = 0.0;
    }
  }

  void _pDown(PointerDownEvent e) {
    _pts[e.pointer] = e.localPosition;
    if (_pts.length == 1) {
      _lastSinglePos = e.localPosition;
    } else if (_pts.length == 2) {
      _lastSinglePos = null; // switch to pinch mode
      final vs = _pts.values.toList();
      _gsDist   = (vs[0] - vs[1]).distance;
      _gsZoom   = _zoom;
      _gsCenter = Offset((vs[0].dx + vs[1].dx) / 2, (vs[0].dy + vs[1].dy) / 2);
    }
  }

  void _pMove(PointerMoveEvent e, double chartW, double chartH) {
    _pts[e.pointer] = e.localPosition;

    if (_pts.length >= 2 && _gsDist > 0) {
      // ── Pinch-zoom + 2-finger pan ─────────────────────────────────
      final vs     = _pts.values.toList();
      final dist   = (vs[0] - vs[1]).distance;
      final center = Offset((vs[0].dx + vs[1].dx) / 2, (vs[0].dy + vs[1].dy) / 2);
      final newZoom = (_gsZoom * dist / _gsDist).clamp(1.0, 20.0);
      final dx = center.dx - _gsCenter.dx;
      _gsCenter = center;
      final viewSpan   = _spanX / newZoom;
      final maxPanData = (_spanX - viewSpan).clamp(0.0, double.infinity);
      double newPanFrac = _panFrac;
      if (maxPanData > 0 && chartW > 0) {
        final dataPerPixel = viewSpan / chartW;
        final newPanData   = (_panFrac * maxPanData - dx * dataPerPixel).clamp(0.0, maxPanData);
        newPanFrac = newPanData / maxPanData;
      }
      setState(() { _zoom = newZoom; _panFrac = newPanFrac; });

    } else if (_pts.length == 1 && _lastSinglePos != null) {
      // ── Single-finger pan ─────────────────────────────────────────
      final dx = e.localPosition.dx - _lastSinglePos!.dx;
      final dy = e.localPosition.dy - _lastSinglePos!.dy;
      _lastSinglePos = e.localPosition;

      // X pan — only when zoomed in
      if (_zoom > 1.01 && chartW > 0) {
        final viewSpan   = _spanX / _zoom;
        final maxPanData = (_spanX - viewSpan).clamp(0.0, double.infinity);
        if (maxPanData > 0) {
          final dataPerPixel = viewSpan / chartW;
          final newPanData   = (_panFrac * maxPanData - dx * dataPerPixel).clamp(0.0, maxPanData);
          _panFrac = newPanData / maxPanData;
        }
      }

      // Y pan — always (drag to scroll Y scale)
      if (chartH > 0 && _lastYRange > 0) {
        _yOffset -= dy * (_lastYRange / chartH);
      }

      setState(() {});
    }
  }

  void _pUp(PointerUpEvent e) {
    _pts.remove(e.pointer);
    if (_pts.isEmpty) _lastSinglePos = null;
  }
  void _pCancel(PointerCancelEvent e) {
    _pts.remove(e.pointer);
    if (_pts.isEmpty) _lastSinglePos = null;
  }

  void _resetZoom() => setState(() { _zoom = 1.0; _panFrac = 0.0; _yOffset = 0.0; });

  @override
  Widget build(BuildContext context) {
    if (widget.points.isEmpty) {
      return Center(child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(Icons.show_chart, size: 56, color: Colors.grey.withValues(alpha: 0.2)),
          const SizedBox(height: 12),
          Text('No data — press "Download from beacon"',
              style: Theme.of(context).textTheme.bodySmall,
              textAlign: TextAlign.center),
        ],
      ));
    }

    final theme  = Theme.of(context);
    final isDark = theme.brightness == Brightness.dark;
    final gridC  = isDark ? Colors.white10 : Colors.black.withValues(alpha: 0.07);
    final axisC  = isDark ? Colors.white24 : Colors.black26;
    final lblC   = isDark ? Colors.white54 : Colors.black45;

    final spots    = _toSpots(widget.points);
    final dataMinX = spots.first.x;
    final dataMaxX = spots.last.x;
    _spanX    = (dataMaxX - dataMinX).clamp(1.0, double.infinity);
    _dataMinX = dataMinX;

    // Compute visible window from zoom + pan
    final viewSpan    = _spanX / _zoom;
    final maxPanData  = (_spanX - viewSpan).clamp(0.0, double.infinity);
    final panOffset   = _panFrac * maxPanData;
    final viewMinX    = _dataMinX + panOffset;
    final viewMaxX    = viewMinX + viewSpan;
    final edgeX       = (viewSpan * 0.05).clamp(0.5, double.infinity);
    final minX        = viewMinX - edgeX;
    final maxX        = viewMaxX + edgeX;

    final yVals  = spots.map((s) => s.y).toList();
    final rawMin = yVals.reduce((a, b) => a < b ? a : b);
    final rawMax = yVals.reduce((a, b) => a > b ? a : b);
    final rawAvg = yVals.reduce((a, b) => a + b) / yVals.length;
    final yPad   = ((rawMax - rawMin) * 0.12).clamp(0.5, double.infinity);
    final yMin   = rawMin - yPad + _yOffset;
    final yMax   = rawMax + yPad + _yOffset;
    final yRange = (yMax - yMin).clamp(0.1, double.infinity);
    _lastYRange  = yRange;

    double niceInterval(double range, int ticks) {
      final raw  = range / ticks;
      final mag  = (raw == 0) ? 1.0 : pow(10, (log(raw) / ln10).floor()).toDouble();
      final norm = raw / mag;
      if (norm <= 1) return mag;
      if (norm <= 2) return 2 * mag;
      if (norm <= 5) return 5 * mag;
      return 10 * mag;
    }
    final yInterval = niceInterval(yRange, 5);
    final xInterval = niceInterval(viewSpan, 4);
    final spanH = _spanX / 3600;

    String xLabel(double v) {
      final dt = DateTime.fromMillisecondsSinceEpoch(v.toInt() * 1000).toLocal();
      if (spanH > 24) {
        return '${dt.day.toString().padLeft(2,'0')}/${dt.month.toString().padLeft(2,'0')}\n'
            '${dt.hour.toString().padLeft(2,'0')}:${dt.minute.toString().padLeft(2,'0')}';
      }
      return '${dt.hour.toString().padLeft(2,'0')}:${dt.minute.toString().padLeft(2,'0')}';
    }

    return Column(children: [
      // ── header ──────────────────────────────────────────────────────
      Padding(
        padding: const EdgeInsets.fromLTRB(12, 6, 4, 6),
        child: Row(children: [
          Text(widget.title,
              style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 14)),
          Text('  (${widget.unit})', style: TextStyle(fontSize: 12, color: lblC)),
          const Spacer(),
          _StatBadge('MIN', widget.formatY(rawMin), Colors.blueAccent),
          const SizedBox(width: 6),
          _StatBadge('AVG', widget.formatY(rawAvg), Colors.orange),
          const SizedBox(width: 6),
          _StatBadge('MAX', widget.formatY(rawMax), widget.lineColor),
          // Reset zoom/pan button
          if (_zoom > 1.01 || _yOffset.abs() > 0.01)
            IconButton(
              icon: const Icon(Icons.zoom_out_map, size: 18),
              tooltip: 'Reset zoom & pan',
              padding: const EdgeInsets.symmetric(horizontal: 4),
              constraints: const BoxConstraints(),
              onPressed: _resetZoom,
            ),
          if (!widget.fullscreen)
            Builder(builder: (ctx) => IconButton(
              icon: const Icon(Icons.fullscreen, size: 20),
              tooltip: 'Fullscreen',
              padding: const EdgeInsets.symmetric(horizontal: 4),
              constraints: const BoxConstraints(),
              onPressed: () => Navigator.push(ctx, MaterialPageRoute(
                builder: (_) => _FullscreenChartScreen(
                  title: widget.title, unit: widget.unit, points: widget.points,
                  lineColor: widget.lineColor, formatY: widget.formatY,
                  smoothLine: widget.smoothLine,
                ),
              )),
            )),
        ]),
      ),
      Padding(
        padding: const EdgeInsets.fromLTRB(12, 0, 12, 4),
        child: Row(children: [
          Icon(Icons.pinch_outlined, size: 11, color: lblC),
          const SizedBox(width: 3),
          Text('Pinch to zoom · drag to pan · tap for tooltip',
              style: TextStyle(fontSize: 9, color: lblC)),
        ]),
      ),
      const Divider(height: 1),

      // ── chart + Listener for pinch-zoom + single-finger pan ─────────
      Expanded(child: LayoutBuilder(
        builder: (ctx, constraints) {
          final chartW = constraints.maxWidth;
          final chartH = constraints.maxHeight;
          return Listener(
            behavior: HitTestBehavior.translucent,
            onPointerDown:   _pDown,
            onPointerMove:   (e) => _pMove(e, chartW, chartH),
            onPointerUp:     _pUp,
            onPointerCancel: _pCancel,
            child: Padding(
              padding: const EdgeInsets.fromLTRB(4, 12, 16, 8),
              child: LineChart(LineChartData(
                minX: minX, maxX: maxX,
                minY: yMin, maxY: yMax,
                clipData: const FlClipData.all(),
                gridData: FlGridData(
                  show: true,
                  drawHorizontalLine: true,
                  drawVerticalLine: true,
                  horizontalInterval: yInterval,
                  verticalInterval: xInterval,
                  getDrawingHorizontalLine: (_) => FlLine(color: gridC, strokeWidth: 1),
                  getDrawingVerticalLine: (_) =>
                      FlLine(color: gridC, strokeWidth: 1, dashArray: [4, 4]),
                ),
                borderData: FlBorderData(
                  show: true,
                  border: Border(
                    left:   BorderSide(color: axisC, width: 1),
                    bottom: BorderSide(color: axisC, width: 1),
                    right:  BorderSide(color: Colors.transparent),
                    top:    BorderSide(color: Colors.transparent),
                  ),
                ),
                titlesData: FlTitlesData(
                  leftTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      reservedSize: 52,
                      interval: yInterval,
                      getTitlesWidget: (v, meta) {
                        if (v == meta.min || v == meta.max) return const SizedBox.shrink();
                        return Padding(
                          padding: const EdgeInsets.only(right: 6),
                          child: Text(widget.formatY(v),
                              textAlign: TextAlign.right,
                              style: TextStyle(fontSize: 10, color: lblC)),
                        );
                      },
                    ),
                  ),
                  bottomTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      reservedSize: spanH > 24 ? 36 : 22,
                      interval: xInterval,
                      getTitlesWidget: (v, meta) {
                        if (v == meta.min || v == meta.max) return const SizedBox.shrink();
                        return Padding(
                          padding: const EdgeInsets.only(top: 4),
                          child: Text(xLabel(v),
                              textAlign: TextAlign.center,
                              style: TextStyle(fontSize: 9, color: lblC)),
                        );
                      },
                    ),
                  ),
                  rightTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false, reservedSize: 8)),
                  topTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false, reservedSize: 8)),
                ),
                lineBarsData: [
                  LineChartBarData(
                    spots: spots,
                    color: widget.lineColor,
                    barWidth: 2.0,
                    isCurved: widget.smoothLine,
                    curveSmoothness: widget.smoothLine ? 0.35 : 0.0,
                    dotData: FlDotData(
                      show: spots.length <= 40,
                      getDotPainter: (_, __, ___, ____) =>
                          FlDotCirclePainter(
                              radius: 2.5,
                              color: widget.lineColor,
                              strokeWidth: 1.5,
                              strokeColor: Colors.white.withValues(alpha: 0.6))),
                    belowBarData: BarAreaData(
                      show: true,
                      gradient: LinearGradient(
                        begin: Alignment.topCenter,
                        end: Alignment.bottomCenter,
                        colors: [
                          widget.lineColor.withValues(alpha: 0.30),
                          widget.lineColor.withValues(alpha: 0.0),
                        ],
                      ),
                    ),
                  ),
                ],
                lineTouchData: LineTouchData(
                  handleBuiltInTouches: true,
                  touchTooltipData: LineTouchTooltipData(
                    tooltipRoundedRadius: 8,
                    getTooltipItems: (ts) => ts.map((s) {
                      final dt = DateTime.fromMillisecondsSinceEpoch(
                          s.x.toInt() * 1000).toLocal();
                      final t = '${dt.hour.toString().padLeft(2,'0')}:'
                          '${dt.minute.toString().padLeft(2,'0')}:'
                          '${dt.second.toString().padLeft(2,'0')}';
                      return LineTooltipItem(
                        '${widget.formatY(s.y)} ${widget.unit}\n$t',
                        TextStyle(
                          color: theme.colorScheme.onSurface,
                          fontSize: 12,
                          fontWeight: FontWeight.w600,
                        ),
                      );
                    }).toList(),
                  ),
                ),
              )),
            ),
          );
        },
      )),
    ]);
  }

  List<FlSpot> _toSpots(List<ChartPoint> pts) {
    final sorted = pts.toList()..sort((a, b) => a.ts.compareTo(b.ts));
    return sorted.map((p) => FlSpot(p.ts.millisecondsSinceEpoch / 1000, p.value)).toList();
  }
}

class _StatBadge extends StatelessWidget {
  final String label;
  final String value;
  final Color  color;
  const _StatBadge(this.label, this.value, this.color);

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
    decoration: BoxDecoration(
      color: color.withValues(alpha: 0.12),
      borderRadius: BorderRadius.circular(6),
      border: Border.all(color: color.withValues(alpha: 0.35)),
    ),
    child: RichText(text: TextSpan(children: [
      TextSpan(text: '$label ',
          style: TextStyle(fontSize: 9, color: color.withValues(alpha: 0.7))),
      TextSpan(text: value,
          style: TextStyle(fontSize: 11, fontWeight: FontWeight.bold, color: color)),
    ])),
  );
}

// ── Accel chart (3 lines X/Y/Z) with pinch-zoom ───────────────────────────

class _AccelChart extends StatefulWidget {
  final BeaconProvider beacon;
  const _AccelChart({required this.beacon});
  @override
  State<_AccelChart> createState() => _AccelChartState();
}

class _AccelChartState extends State<_AccelChart> {
  double _zoom    = 1.0;
  double _panFrac = 0.0;
  double _yOffset = 0.0;
  double _spanX   = 1.0;
  double _minX0   = 0.0;
  double _lastYRange = 1.0;

  final _pts = <int, Offset>{};
  double  _gsZoom      = 1.0;
  double  _gsDist      = 0.0;
  Offset  _gsCenter    = Offset.zero;
  Offset? _lastSinglePos;

  void _pDown(PointerDownEvent e) {
    _pts[e.pointer] = e.localPosition;
    if (_pts.length == 1) {
      _lastSinglePos = e.localPosition;
    } else if (_pts.length == 2) {
      _lastSinglePos = null;
      final vs = _pts.values.toList();
      _gsDist   = (vs[0] - vs[1]).distance;
      _gsZoom   = _zoom;
      _gsCenter = Offset((vs[0].dx + vs[1].dx) / 2, (vs[0].dy + vs[1].dy) / 2);
    }
  }

  void _pMove(PointerMoveEvent e, double chartW, double chartH) {
    _pts[e.pointer] = e.localPosition;

    if (_pts.length >= 2 && _gsDist > 0) {
      final vs     = _pts.values.toList();
      final dist   = (vs[0] - vs[1]).distance;
      final center = Offset((vs[0].dx + vs[1].dx) / 2, (vs[0].dy + vs[1].dy) / 2);
      final newZoom = (_gsZoom * dist / _gsDist).clamp(1.0, 20.0);
      final dx = center.dx - _gsCenter.dx;
      _gsCenter = center;
      final viewSpan   = _spanX / newZoom;
      final maxPanData = (_spanX - viewSpan).clamp(0.0, double.infinity);
      double newPanFrac = _panFrac;
      if (maxPanData > 0 && chartW > 0) {
        final dataPerPixel = viewSpan / chartW;
        final newPanData = (_panFrac * maxPanData - dx * dataPerPixel).clamp(0.0, maxPanData);
        newPanFrac = newPanData / maxPanData;
      }
      setState(() { _zoom = newZoom; _panFrac = newPanFrac; });

    } else if (_pts.length == 1 && _lastSinglePos != null) {
      final dx = e.localPosition.dx - _lastSinglePos!.dx;
      final dy = e.localPosition.dy - _lastSinglePos!.dy;
      _lastSinglePos = e.localPosition;
      if (_zoom > 1.01 && chartW > 0) {
        final viewSpan   = _spanX / _zoom;
        final maxPanData = (_spanX - viewSpan).clamp(0.0, double.infinity);
        if (maxPanData > 0) {
          final dataPerPixel = viewSpan / chartW;
          final newPanData = (_panFrac * maxPanData - dx * dataPerPixel).clamp(0.0, maxPanData);
          _panFrac = newPanData / maxPanData;
        }
      }
      if (chartH > 0 && _lastYRange > 0) {
        _yOffset -= dy * (_lastYRange / chartH);
      }
      setState(() {});
    }
  }

  void _pUp(PointerUpEvent e) {
    _pts.remove(e.pointer);
    if (_pts.isEmpty) _lastSinglePos = null;
  }
  void _pCancel(PointerCancelEvent e) {
    _pts.remove(e.pointer);
    if (_pts.isEmpty) _lastSinglePos = null;
  }

  @override
  Widget build(BuildContext context) {
    final beacon = widget.beacon;
    final hasData = beacon.logAccelXHistory.isNotEmpty ||
        beacon.logAccelYHistory.isNotEmpty || beacon.logAccelZHistory.isNotEmpty;

    if (!hasData) {
      return Center(child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(Icons.vibration, size: 56, color: Colors.grey.withValues(alpha: 0.2)),
          const SizedBox(height: 12),
          Text('No accel data — press "Download from beacon"',
              style: Theme.of(context).textTheme.bodySmall,
              textAlign: TextAlign.center),
        ],
      ));
    }

    final isDark = Theme.of(context).brightness == Brightness.dark;
    final gridC  = isDark ? Colors.white10 : Colors.black.withValues(alpha: 0.07);
    final axisC  = isDark ? Colors.white24 : Colors.black26;
    final lblC   = isDark ? Colors.white54 : Colors.black45;

    final allPts = [
      ...beacon.logAccelXHistory, ...beacon.logAccelYHistory, ...beacon.logAccelZHistory,
    ];
    final dataMinX0 = allPts.map((p) => p.ts.millisecondsSinceEpoch / 1000.0).reduce((a, b) => a < b ? a : b);
    final dataMaxX0 = allPts.map((p) => p.ts.millisecondsSinceEpoch / 1000.0).reduce((a, b) => a > b ? a : b);
    _spanX = (dataMaxX0 - dataMinX0).clamp(1.0, double.infinity);
    _minX0 = dataMinX0;

    final viewSpan   = _spanX / _zoom;
    final maxPanData = (_spanX - viewSpan).clamp(0.0, double.infinity);
    final panOffset  = _panFrac * maxPanData;
    final viewMinX   = _minX0 + panOffset;
    final viewMaxX   = viewMinX + viewSpan;
    final edgeX      = (viewSpan * 0.05).clamp(0.5, double.infinity);
    final minX       = viewMinX - edgeX;
    final maxX       = viewMaxX + edgeX;

    final rawYMin = allPts.map((p) => p.value).reduce((a, b) => a < b ? a : b);
    final rawYMax = allPts.map((p) => p.value).reduce((a, b) => a > b ? a : b);
    final yPad    = ((rawYMax - rawYMin) * 0.12).clamp(0.05, double.infinity);
    _lastYRange   = (rawYMax - rawYMin + 2 * yPad).clamp(0.01, double.infinity);

    FlSpot toSpot(ChartPoint p) =>
        FlSpot(p.ts.millisecondsSinceEpoch / 1000.0, p.value);

    return Column(children: [
      Padding(
        padding: const EdgeInsets.fromLTRB(12, 6, 12, 2),
        child: Row(children: [
          const Text('Accelerometer',
              style: TextStyle(fontWeight: FontWeight.bold, fontSize: 14)),
          Text('  (g)', style: TextStyle(fontSize: 12, color: lblC)),
          const Spacer(),
          if (_zoom > 1.01 || _yOffset.abs() > 0.001)
            IconButton(
              icon: const Icon(Icons.zoom_out_map, size: 18),
              tooltip: 'Reset zoom & pan',
              padding: const EdgeInsets.symmetric(horizontal: 4),
              constraints: const BoxConstraints(),
              onPressed: () => setState(() { _zoom = 1.0; _panFrac = 0.0; _yOffset = 0.0; }),
            ),
          _Legend('X', const Color(0xFFF44336)),
          const SizedBox(width: 8),
          _Legend('Y', const Color(0xFF4CAF50)),
          const SizedBox(width: 8),
          _Legend('Z', const Color(0xFF2196F3)),
        ]),
      ),
      Padding(
        padding: const EdgeInsets.fromLTRB(12, 0, 12, 4),
        child: Row(children: [
          Icon(Icons.pinch_outlined, size: 11, color: lblC),
          const SizedBox(width: 3),
          Text('Pinch to zoom · drag to pan · tap for tooltip',
              style: TextStyle(fontSize: 9, color: lblC)),
        ]),
      ),
      const Divider(height: 1),
      Expanded(child: LayoutBuilder(
        builder: (ctx, constraints) {
          final chartW = constraints.maxWidth;
          final chartH = constraints.maxHeight;
          return Listener(
            behavior: HitTestBehavior.translucent,
            onPointerDown:   _pDown,
            onPointerMove:   (e) => _pMove(e, chartW, chartH),
            onPointerUp:     _pUp,
            onPointerCancel: _pCancel,
            child: Padding(
              padding: const EdgeInsets.fromLTRB(4, 12, 16, 8),
              child: LineChart(LineChartData(
                minX: minX, maxX: maxX,
                minY: rawYMin - yPad + _yOffset, maxY: rawYMax + yPad + _yOffset,
                clipData: const FlClipData.all(),
                gridData: FlGridData(
                  show: true,
                  getDrawingHorizontalLine: (_) => FlLine(color: gridC, strokeWidth: 1),
                  getDrawingVerticalLine: (_) =>
                      FlLine(color: gridC, strokeWidth: 1, dashArray: [4, 4]),
                ),
                borderData: FlBorderData(
                  show: true,
                  border: Border(
                    left:   BorderSide(color: axisC, width: 1),
                    bottom: BorderSide(color: axisC, width: 1),
                    right:  BorderSide.none,
                    top:    BorderSide.none,
                  ),
                ),
                titlesData: FlTitlesData(
                  leftTitles: AxisTitles(sideTitles: SideTitles(
                    showTitles: true, reservedSize: 44,
                    getTitlesWidget: (v, meta) {
                      if (v == meta.min || v == meta.max) return const SizedBox.shrink();
                      return Padding(
                        padding: const EdgeInsets.only(right: 6),
                        child: Text(v.toStringAsFixed(2),
                            textAlign: TextAlign.right,
                            style: TextStyle(fontSize: 10, color: lblC)),
                      );
                    },
                  )),
                  bottomTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false, reservedSize: 8)),
                  rightTitles:  const AxisTitles(
                      sideTitles: SideTitles(showTitles: false, reservedSize: 8)),
                  topTitles:    const AxisTitles(
                      sideTitles: SideTitles(showTitles: false, reservedSize: 8)),
                ),
                lineBarsData: [
                  if (beacon.logAccelXHistory.isNotEmpty)
                    _accelBar(beacon.logAccelXHistory.map(toSpot).toList(),
                        const Color(0xFFF44336)),
                  if (beacon.logAccelYHistory.isNotEmpty)
                    _accelBar(beacon.logAccelYHistory.map(toSpot).toList(),
                        const Color(0xFF4CAF50)),
                  if (beacon.logAccelZHistory.isNotEmpty)
                    _accelBar(beacon.logAccelZHistory.map(toSpot).toList(),
                        const Color(0xFF2196F3)),
                ],
                lineTouchData: LineTouchData(
                  touchTooltipData: LineTouchTooltipData(
                    tooltipRoundedRadius: 8,
                    getTooltipItems: (spots) => spots.map((s) {
                      const labels = ['X', 'Y', 'Z'];
                      const colors = [
                        Color(0xFFF44336), Color(0xFF4CAF50), Color(0xFF2196F3)];
                      final i = s.barIndex.clamp(0, 2);
                      return LineTooltipItem(
                        '${labels[i]}: ${s.y.toStringAsFixed(3)} g',
                        TextStyle(color: colors[i], fontSize: 12,
                            fontWeight: FontWeight.bold),
                      );
                    }).toList(),
                  ),
                ),
              )),
            ),
          );
        },
      )),
    ]);
  }

  LineChartBarData _accelBar(List<FlSpot> spots, Color color) =>
      LineChartBarData(
        spots: spots, color: color, barWidth: 1.5,
        isCurved: spots.length > 20, curveSmoothness: 0.1,
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

// ── Fullscreen chart screen ────────────────────────────────────────────────

class _FullscreenChartScreen extends StatelessWidget {
  final String title;
  final String unit;
  final List<ChartPoint> points;
  final Color lineColor;
  final String Function(double) formatY;
  final bool smoothLine;

  const _FullscreenChartScreen({
    required this.title,
    required this.unit,
    required this.points,
    required this.lineColor,
    required this.formatY,
    this.smoothLine = true,
  });

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(title),
        actions: [
          IconButton(
            icon: const Icon(Icons.fullscreen_exit),
            tooltip: 'Exit fullscreen',
            onPressed: () => Navigator.pop(context),
          ),
        ],
      ),
      body: SafeArea(
        child: _LineChartCard(
          title: title, unit: unit, points: points,
          lineColor: lineColor, formatY: formatY,
          fullscreen: true, smoothLine: smoothLine,
        ),
      ),
    );
  }
}
