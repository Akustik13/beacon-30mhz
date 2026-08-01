import 'package:flutter/material.dart';
import '../../app_theme.dart';

class BleConsoleWidget extends StatefulWidget {
  final List<String> lines;
  final VoidCallback? onClear;

  const BleConsoleWidget({super.key, required this.lines, this.onClear});

  @override
  State<BleConsoleWidget> createState() => _BleConsoleWidgetState();
}

class _BleConsoleWidgetState extends State<BleConsoleWidget> {
  final _scroll = ScrollController();

  @override
  void didUpdateWidget(covariant BleConsoleWidget old) {
    super.didUpdateWidget(old);
    if (widget.lines.length != old.lines.length) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (_scroll.hasClients) {
          _scroll.animateTo(
            _scroll.position.maxScrollExtent,
            duration: const Duration(milliseconds: 150),
            curve: Curves.easeOut,
          );
        }
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Row(
          children: [
            const Text('BLE Console', style: TextStyle(fontSize: 12, color: AppColors.textSub)),
            const Spacer(),
            if (widget.onClear != null)
              TextButton.icon(
                onPressed: widget.onClear,
                icon: const Icon(Icons.delete_sweep, size: 14),
                label: const Text('Clear', style: TextStyle(fontSize: 11)),
                style: TextButton.styleFrom(
                  foregroundColor: AppColors.textSub,
                  padding: const EdgeInsets.symmetric(horizontal: 8),
                ),
              ),
          ],
        ),
        Container(
          height: 160,
          decoration: BoxDecoration(
            color: const Color(0xFF0D0D1A),
            borderRadius: BorderRadius.circular(8),
            border: Border.all(color: AppColors.divider),
          ),
          child: widget.lines.isEmpty
              ? const Center(
                  child: Text('No log entries',
                      style: TextStyle(fontSize: 11, color: AppColors.textSub)))
              : ListView.builder(
                  controller: _scroll,
                  padding: const EdgeInsets.all(8),
                  itemCount: widget.lines.length,
                  itemBuilder: (_, i) => Text(
                    widget.lines[i],
                    style: const TextStyle(
                      fontFamily: 'monospace',
                      fontSize: 10,
                      color: Color(0xFF80CBC4),
                    ),
                  ),
                ),
        ),
      ],
    );
  }

  @override
  void dispose() {
    _scroll.dispose();
    super.dispose();
  }
}
