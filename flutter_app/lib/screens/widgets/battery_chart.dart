import 'package:flutter/material.dart';
import 'package:fl_chart/fl_chart.dart';
import '../../app_theme.dart';
import '../../services/battery_calculator.dart';
import '../../models/beacon_config.dart';

class BatteryForecastChart extends StatelessWidget {
  final BeaconConfig config;
  final int batteryPercent;

  const BatteryForecastChart({
    super.key,
    required this.config,
    required this.batteryPercent,
  });

  @override
  Widget build(BuildContext context) {
    final forecast = BatteryCalculator.calculate(config, batteryPercent: batteryPercent);
    final totalDays = forecast.estimatedDaysTotal;
    final remaining = forecast.estimatedDaysRemaining;

    // Build discharge curve (linear approximation)
    final spots = <FlSpot>[];
    for (int i = 0; i <= 20; i++) {
      final day = (totalDays * i / 20);
      final pct = (100.0 * (1 - i / 20)).clamp(0.0, 100.0);
      spots.add(FlSpot(day, pct));
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(4, 0, 4, 8),
          child: Wrap(spacing: 16, children: [
            _stat('Avg current', '${(forecast.avgMa * 1000).toStringAsFixed(0)} µA'),
            _stat('Total life', '${totalDays.toStringAsFixed(0)} days'),
            _stat('Remaining', '${remaining.toStringAsFixed(0)} days'),
          ]),
        ),
        SizedBox(
          height: 140,
          child: LineChart(LineChartData(
            gridData: FlGridData(
              show: true,
              drawVerticalLine: true,
              getDrawingHorizontalLine: (_) =>
                  FlLine(color: AppColors.divider, strokeWidth: 0.5),
              getDrawingVerticalLine: (_) =>
                  FlLine(color: AppColors.divider, strokeWidth: 0.5),
            ),
            borderData: FlBorderData(
              border: const Border(
                bottom: BorderSide(color: AppColors.divider),
                left: BorderSide(color: AppColors.divider),
              ),
            ),
            titlesData: FlTitlesData(
              leftTitles: AxisTitles(
                sideTitles: SideTitles(
                  showTitles: true,
                  reservedSize: 32,
                  getTitlesWidget: (v, _) =>
                      Text('${v.toInt()}%', style: const TextStyle(fontSize: 9, color: AppColors.textSub)),
                ),
              ),
              bottomTitles: AxisTitles(
                sideTitles: SideTitles(
                  showTitles: true,
                  reservedSize: 18,
                  getTitlesWidget: (v, _) =>
                      Text('d${v.toInt()}', style: const TextStyle(fontSize: 9, color: AppColors.textSub)),
                ),
              ),
              topTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
              rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
            ),
            lineBarsData: [
              LineChartBarData(
                spots: spots,
                isCurved: false,
                color: AppColors.secondary,
                barWidth: 2,
                dotData: const FlDotData(show: false),
                belowBarData: BarAreaData(
                  show: true,
                  color: AppColors.secondary.withOpacity(0.15),
                ),
              ),
              // Current position marker
              LineChartBarData(
                spots: [FlSpot(totalDays - remaining, batteryPercent.toDouble())],
                color: AppColors.warning,
                barWidth: 0,
                dotData: FlDotData(
                  show: true,
                  getDotPainter: (_, __, ___, ____) => FlDotCirclePainter(
                    radius: 5,
                    color: AppColors.warning,
                    strokeColor: Colors.white,
                    strokeWidth: 1.5,
                  ),
                ),
              ),
            ],
          )),
        ),
      ],
    );
  }

  Widget _stat(String label, String value) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Text(label, style: const TextStyle(fontSize: 10, color: AppColors.textSub)),
      Text(value, style: const TextStyle(fontSize: 13, fontWeight: FontWeight.bold, color: AppColors.text)),
    ],
  );
}
