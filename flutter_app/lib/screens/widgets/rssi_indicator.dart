import 'package:flutter/material.dart';
import '../../app_theme.dart';

class RssiIndicator extends StatelessWidget {
  final int rssi;
  final double size;

  const RssiIndicator({super.key, required this.rssi, this.size = 20});

  Color get _color {
    if (rssi >= -60) return AppColors.secondary;
    if (rssi >= -75) return AppColors.warning;
    return AppColors.error;
  }

  String get _label {
    if (rssi == 0) return '--';
    return '${rssi} dBm';
  }

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(Icons.signal_cellular_alt, size: size, color: _color),
        const SizedBox(width: 4),
        Text(_label, style: TextStyle(fontSize: 12, color: _color)),
      ],
    );
  }
}
