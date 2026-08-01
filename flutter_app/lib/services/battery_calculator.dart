import '../models/beacon_config.dart';

class BatteryForecast {
  final double txMaAvg;
  final double bleMaAvg;
  final double sleepUa;
  final double avgMa;
  final double estimatedDaysTotal;
  final double estimatedDaysRemaining;
  final int batteryPercent;

  const BatteryForecast({
    required this.txMaAvg,
    required this.bleMaAvg,
    required this.sleepUa,
    required this.avgMa,
    required this.estimatedDaysTotal,
    required this.estimatedDaysRemaining,
    required this.batteryPercent,
  });
}

class BatteryCalculator {
  static const double _sleepUa = 0.55;
  static const double _bleMa = 3.5;

  static BatteryForecast calculate(BeaconConfig cfg, {int batteryPercent = 100}) {
    final txMa = BeaconConfig.powerMa(cfg.powerLevel);

    // Active hours per day from bitmask
    final activeBits = _countBits(cfg.activeHours, 24);
    final activeHoursPerDay = activeBits.toDouble();

    // TX duty cycle within active period
    final txDutyRatio = cfg.txPeriodMs > 0
        ? (cfg.txDurationMs / cfg.txPeriodMs).clamp(0.0, 1.0)
        : 0.0;

    // BLE sessions per day
    final bleSessionsPerDay = cfg.bleIntervalMin > 0
        ? (activeHoursPerDay * 60 / cfg.bleIntervalMin)
        : 0.0;
    final bleDurationHours = cfg.bleDurationSec / 3600.0;

    final txMaAvg = txMa * txDutyRatio;
    final bleMaAvg = _bleMa * bleSessionsPerDay * bleDurationHours / 24.0;
    final sleepFraction = 1.0 - (activeHoursPerDay / 24.0);
    final sleepMaAvg = (_sleepUa / 1000.0) * sleepFraction;

    final avgMa = txMaAvg + bleMaAvg + sleepMaAvg;
    final capacityMah = cfg.batteryCapacityMah.toDouble();
    final totalDays = avgMa > 0 ? (capacityMah / avgMa) / 24.0 : 0.0;
    final remaining = totalDays * batteryPercent / 100.0;

    return BatteryForecast(
      txMaAvg: txMaAvg,
      bleMaAvg: bleMaAvg,
      sleepUa: _sleepUa,
      avgMa: avgMa,
      estimatedDaysTotal: totalDays,
      estimatedDaysRemaining: remaining,
      batteryPercent: batteryPercent,
    );
  }

  static int _countBits(int value, int bits) {
    int count = 0;
    for (int i = 0; i < bits; i++) {
      if (value & (1 << i) != 0) count++;
    }
    return count;
  }
}
