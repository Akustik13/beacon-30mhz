import 'package:flutter/material.dart';
import '../../app_theme.dart';
import '../../services/location_service.dart';

class LocationChip extends StatelessWidget {
  final LocationData? location;

  const LocationChip({super.key, this.location});

  @override
  Widget build(BuildContext context) {
    if (location == null) {
      return const Chip(
        avatar: Icon(Icons.location_off, size: 14, color: AppColors.textSub),
        label: Text('No GPS', style: TextStyle(fontSize: 11, color: AppColors.textSub)),
        backgroundColor: AppColors.card,
        padding: EdgeInsets.zero,
        materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
      );
    }
    final lat = location!.lat.toStringAsFixed(5);
    final lon = location!.lon.toStringAsFixed(5);
    return Chip(
      avatar: const Icon(Icons.location_on, size: 14, color: AppColors.secondary),
      label: Text('$lat, $lon',
          style: const TextStyle(fontSize: 11, color: AppColors.text)),
      backgroundColor: AppColors.card,
      padding: EdgeInsets.zero,
      materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
    );
  }
}
