import 'dart:async';
import 'package:flutter/material.dart';
import '../../app_theme.dart';
import '../../models/beacon_device.dart';

class BeaconCard extends StatefulWidget {
  final BeaconDevice device;
  final VoidCallback? onTap;
  final VoidCallback? onDelete;
  final VoidCallback? onIconTap;
  final bool selected;
  final bool isConnected;
  // Live data from BeaconProvider when connected
  final double? liveTemp;
  final int? liveBatteryPercent;
  final int? liveBatteryMv;
  final int? liveRssi;

  const BeaconCard({
    super.key,
    required this.device,
    this.onTap,
    this.onDelete,
    this.onIconTap,
    this.selected = false,
    this.isConnected = false,
    this.liveTemp,
    this.liveBatteryPercent,
    this.liveBatteryMv,
    this.liveRssi,
  });

  @override
  State<BeaconCard> createState() => _BeaconCardState();
}

class _BeaconCardState extends State<BeaconCard>
    with SingleTickerProviderStateMixin {
  late AnimationController _blink;
  late Animation<double> _blinkAnim;
  Timer? _ticker;
  int _secondsSinceLast = 0;

  @override
  void initState() {
    super.initState();
    _blink = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 600),
    );
    _blinkAnim = Tween<double>(begin: 0.3, end: 1.0).animate(
        CurvedAnimation(parent: _blink, curve: Curves.easeInOut));

    _ticker = Timer.periodic(const Duration(seconds: 1), (_) {
      if (!mounted) return;
      setState(() {
        final ls = widget.device.lastSeen;
        _secondsSinceLast =
            ls != null ? DateTime.now().difference(ls).inSeconds : 0;
      });
    });
  }

  @override
  void didUpdateWidget(BeaconCard old) {
    super.didUpdateWidget(old);
    if (widget.isConnected && widget.liveRssi != null) {
      // Blink when RSSI data arrives (simulated: pulse once per update)
      if (widget.liveRssi != old.liveRssi) {
        _blink.forward(from: 0).then((_) {
          if (mounted) _blink.reverse();
        });
      }
    }
  }

  @override
  void dispose() {
    _ticker?.cancel();
    _blink.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final battPct = widget.isConnected
        ? (widget.liveBatteryPercent ?? widget.device.batteryPercent)
        : widget.device.batteryPercent;
    final isActive = widget.isConnected || _isRecentlySeen();

    return Card(
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(12),
        side: BorderSide(
          color: widget.isConnected
              ? AppColors.secondary
              : (widget.selected ? AppColors.primary : AppColors.divider),
          width: widget.isConnected || widget.selected ? 1.5 : 1,
        ),
      ),
      child: InkWell(
        borderRadius: BorderRadius.circular(12),
        onTap: widget.onTap,
        child: Padding(
          padding: const EdgeInsets.all(12),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(children: [
                // Tappable emoji icon
                GestureDetector(
                  onTap: widget.onIconTap,
                  child: Stack(
                    clipBehavior: Clip.none,
                    children: [
                      Text(widget.device.icon,
                          style: const TextStyle(fontSize: 30)),
                      if (widget.onIconTap != null)
                        Positioned(
                          right: -4, bottom: -4,
                          child: Container(
                            padding: const EdgeInsets.all(1.5),
                            decoration: BoxDecoration(
                              color: AppColors.card,
                              borderRadius: BorderRadius.circular(6),
                            ),
                            child: const Icon(Icons.edit,
                                size: 9, color: AppColors.textSub),
                          ),
                        ),
                    ],
                  ),
                ),
                const SizedBox(width: 12),

                // Name + nickname
                Expanded(
                  child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                    Row(children: [
                      Expanded(
                        child: Text(widget.device.displayName,
                            style: Theme.of(context).textTheme.titleSmall,
                            overflow: TextOverflow.ellipsis),
                      ),
                      _statusDot(isActive),
                    ]),
                    if (widget.device.nickname.isNotEmpty)
                      Text('"${widget.device.nickname}"',
                          style: const TextStyle(
                              fontSize: 11, color: AppColors.textSub)),
                    if (widget.device.animalTag.isNotEmpty &&
                        widget.device.animalTag != widget.device.name)
                      Text(widget.device.animalTag,
                          style: const TextStyle(
                              fontSize: 10,
                              color: AppColors.textSub,
                              fontFamily: 'monospace')),
                  ]),
                ),

                if (widget.onDelete != null)
                  IconButton(
                    icon: const Icon(Icons.delete_outline,
                        size: 18, color: AppColors.textSub),
                    onPressed: widget.onDelete,
                    padding: EdgeInsets.zero,
                    constraints: const BoxConstraints(),
                  ),
              ]),

              const SizedBox(height: 8),
              const Divider(height: 1, color: AppColors.divider),
              const SizedBox(height: 8),

              // Data row
              Row(children: [
                // Temperature
                _tempChip(),
                const SizedBox(width: 10),
                // Battery
                _battChip(battPct),
                const SizedBox(width: 10),
                // RSSI (with blink if connected)
                _rssiWidget(),
                const Spacer(),
                _lastSeenWidget(),
              ]),

              // Sessions count
              if (widget.device.sessionCount > 0) ...[
                const SizedBox(height: 4),
                Text(
                    '${widget.device.sessionCount} session${widget.device.sessionCount == 1 ? "" : "s"} logged',
                    style: const TextStyle(
                        fontSize: 10, color: AppColors.textSub)),
              ],
            ],
          ),
        ),
      ),
    );
  }

  Widget _tempChip() {
    final temp = widget.isConnected ? widget.liveTemp : widget.device.lastTempC;
    if (temp == null) return const SizedBox.shrink();
    return _dataChip(
        Icons.thermostat, '${temp.toStringAsFixed(1)}°C', _tempColor(temp));
  }

  Widget _battChip(int battPct) {
    final hasBatt = widget.isConnected
        ? widget.liveBatteryPercent != null
        : widget.device.lastBatteryMv != null;
    if (!hasBatt) return const SizedBox.shrink();
    return _dataChip(
        _battIcon(battPct), '$battPct%', _battColor(battPct));
  }

  Widget _rssiWidget() {
    final rssi = widget.isConnected ? widget.liveRssi : widget.device.lastRssi;
    if (rssi == null) return const SizedBox.shrink();
    if (widget.isConnected) {
      return AnimatedBuilder(
        animation: _blinkAnim,
        builder: (_, child) => Opacity(opacity: _blinkAnim.value, child: child),
        child: _dataChip(Icons.signal_cellular_alt, '$rssi dBm', AppColors.primary),
      );
    }
    return _dataChip(
        Icons.signal_cellular_alt, '$rssi dBm', AppColors.primary);
  }

  Widget _lastSeenWidget() {
    if (widget.isConnected) {
      return Row(mainAxisSize: MainAxisSize.min, children: [
        const Icon(Icons.access_time, size: 10, color: AppColors.secondary),
        const SizedBox(width: 2),
        Text('Live', style: const TextStyle(
            fontSize: 10, color: AppColors.secondary,
            fontWeight: FontWeight.w600)),
      ]);
    }
    final ls = widget.device.lastSeen;
    if (ls == null) {
      return const Text('Never seen',
          style: TextStyle(fontSize: 10, color: AppColors.textSub));
    }
    return Row(mainAxisSize: MainAxisSize.min, children: [
      const Icon(Icons.access_time, size: 10, color: AppColors.textSub),
      const SizedBox(width: 2),
      Text(_fmtSince(_secondsSinceLast),
          style: const TextStyle(fontSize: 10, color: AppColors.textSub)),
    ]);
  }

  bool _isRecentlySeen() {
    final ls = widget.device.lastSeen;
    if (ls == null) return false;
    return DateTime.now().difference(ls).inHours < 1;
  }

  Widget _statusDot(bool active) => Row(mainAxisSize: MainAxisSize.min, children: [
    Container(
      width: 8, height: 8,
      decoration: BoxDecoration(
        shape: BoxShape.circle,
        color: active
            ? AppColors.secondary
            : AppColors.textSub.withOpacity(0.4),
      ),
    ),
    const SizedBox(width: 4),
    Text(
        widget.isConnected
            ? 'Connected'
            : (active ? 'Active' : 'Offline'),
        style: TextStyle(
          fontSize: 9,
          color: active ? AppColors.secondary : AppColors.textSub,
        )),
  ]);

  Widget _dataChip(IconData icon, String label, Color color) =>
      Row(mainAxisSize: MainAxisSize.min, children: [
        Icon(icon, size: 11, color: color),
        const SizedBox(width: 2),
        Text(label, style: TextStyle(fontSize: 11, color: color)),
      ]);

  Color _tempColor(double t) {
    if (t > 39) return AppColors.error;
    if (t > 38) return AppColors.warning;
    return AppColors.secondary;
  }

  IconData _battIcon(int pct) {
    if (pct > 70) return Icons.battery_full;
    if (pct > 40) return Icons.battery_5_bar;
    if (pct > 20) return Icons.battery_2_bar;
    return Icons.battery_0_bar;
  }

  Color _battColor(int pct) {
    if (pct < 20) return AppColors.error;
    if (pct < 40) return AppColors.warning;
    return AppColors.secondary;
  }

  String _fmtSince(int seconds) {
    if (seconds < 60) return '${seconds}s ago';
    if (seconds < 3600) return '${seconds ~/ 60}m ago';
    if (seconds < 86400) return '${seconds ~/ 3600}h ago';
    return '${seconds ~/ 86400}d ago';
  }
}
