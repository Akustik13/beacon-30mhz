import 'package:flutter/material.dart';
import '../../app_theme.dart';

class HoursMaskGrid extends StatelessWidget {
  final int mask;
  final void Function(int)? onChanged;

  const HoursMaskGrid({super.key, required this.mask, this.onChanged});

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text('Active hours (24h)', style: TextStyle(fontSize: 12, color: AppColors.textSub)),
        const SizedBox(height: 6),
        Wrap(
          spacing: 3,
          runSpacing: 3,
          children: List.generate(24, (h) {
            final active = (mask >> h) & 1 == 1;
            return GestureDetector(
              onTap: onChanged != null
                  ? () => onChanged!(active ? mask & ~(1 << h) : mask | (1 << h))
                  : null,
              child: Container(
                width: 26,
                height: 26,
                decoration: BoxDecoration(
                  color: active ? AppColors.primary : AppColors.card,
                  borderRadius: BorderRadius.circular(4),
                  border: Border.all(color: AppColors.divider),
                ),
                alignment: Alignment.center,
                child: Text(
                  '$h',
                  style: TextStyle(
                    fontSize: 9,
                    color: active ? Colors.white : AppColors.textSub,
                  ),
                ),
              ),
            );
          }),
        ),
      ],
    );
  }
}

class DaysMaskRow extends StatelessWidget {
  final int mask;
  final void Function(int)? onChanged;
  static const _days = ['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su'];

  const DaysMaskRow({super.key, required this.mask, this.onChanged});

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: List.generate(7, (i) {
        final active = (mask >> i) & 1 == 1;
        return GestureDetector(
          onTap: onChanged != null
              ? () => onChanged!(active ? mask & ~(1 << i) : mask | (1 << i))
              : null,
          child: Container(
            margin: const EdgeInsets.only(right: 3),
            padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
            decoration: BoxDecoration(
              color: active ? AppColors.primary : AppColors.card,
              borderRadius: BorderRadius.circular(4),
              border: Border.all(color: AppColors.divider),
            ),
            child: Text(
              _days[i],
              style: TextStyle(
                fontSize: 10,
                color: active ? Colors.white : AppColors.textSub,
              ),
            ),
          ),
        );
      }),
    );
  }
}

class MonthsMaskRow extends StatelessWidget {
  final int mask;
  final void Function(int)? onChanged;
  static const _months = ['J','F','M','A','M','J','J','A','S','O','N','D'];

  const MonthsMaskRow({super.key, required this.mask, this.onChanged});

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: List.generate(12, (i) {
        final active = (mask >> i) & 1 == 1;
        return GestureDetector(
          onTap: onChanged != null
              ? () => onChanged!(active ? mask & ~(1 << i) : mask | (1 << i))
              : null,
          child: Container(
            margin: const EdgeInsets.only(right: 2),
            padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 4),
            decoration: BoxDecoration(
              color: active ? AppColors.secondary : AppColors.card,
              borderRadius: BorderRadius.circular(4),
              border: Border.all(color: AppColors.divider),
            ),
            child: Text(
              _months[i],
              style: TextStyle(
                fontSize: 10,
                color: active ? Colors.white : AppColors.textSub,
              ),
            ),
          ),
        );
      }),
    );
  }
}
