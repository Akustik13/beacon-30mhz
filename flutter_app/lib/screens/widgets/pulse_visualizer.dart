import 'dart:math';
import 'package:flutter/material.dart';
import '../../app_theme.dart';
import '../../models/beacon_config.dart';

class PulseVisualizer extends StatelessWidget {
  final int txPeriodMs;
  final int txDurationMs;
  final int txPulseOnMs;
  final int txPulseOffMs;

  const PulseVisualizer({
    super.key,
    required this.txPeriodMs,
    required this.txDurationMs,
    required this.txPulseOnMs,
    required this.txPulseOffMs,
  });

  @override
  Widget build(BuildContext context) {
    if (txPeriodMs <= 0) {
      return const SizedBox(height: 90, child: Center(
        child: Text('Set TX Period > 0', style: TextStyle(color: AppColors.textSub, fontSize: 11)),
      ));
    }

    final burstFrac = (txDurationMs / txPeriodMs).clamp(0.0, 1.0);
    final pulseTotal = txPulseOnMs + txPulseOffMs;
    final numPulses = pulseTotal > 0 ? (txDurationMs / pulseTotal) : 0.0;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text('TX Signal Visualization',
            style: TextStyle(fontSize: 11, color: AppColors.textSub)),
        const SizedBox(height: 6),
        ClipRRect(
          borderRadius: BorderRadius.circular(6),
          child: CustomPaint(
            size: const Size(double.infinity, 90),
            painter: _PulsePainter(
              burstFrac: burstFrac,
              pulseOnFrac: pulseTotal > 0 ? txPulseOnMs / pulseTotal : 1.0,
              numPulses: numPulses,
            ),
          ),
        ),
        const SizedBox(height: 6),
        // Legend row
        Row(children: [
          _legend(AppColors.primary.withOpacity(0.3), 'Period: ${BeaconConfig.fmtMs(txPeriodMs)}'),
          const SizedBox(width: 12),
          _legend(AppColors.secondary, 'TX burst: ${BeaconConfig.fmtMs(txDurationMs)}'),
          const SizedBox(width: 12),
          _legend(AppColors.warning, 'ON: ${BeaconConfig.fmtMs(txPulseOnMs)}'),
          const SizedBox(width: 12),
          Text('×${numPulses.toStringAsFixed(numPulses < 10 ? 1 : 0)} pulses',
              style: const TextStyle(fontSize: 10, color: AppColors.textSub)),
        ]),
      ],
    );
  }

  Widget _legend(Color color, String label) => Row(mainAxisSize: MainAxisSize.min, children: [
    Container(width: 10, height: 10, decoration: BoxDecoration(color: color, borderRadius: BorderRadius.circular(2))),
    const SizedBox(width: 4),
    Text(label, style: const TextStyle(fontSize: 10, color: AppColors.textSub)),
  ]);
}

class _PulsePainter extends CustomPainter {
  final double burstFrac;
  final double pulseOnFrac;
  final double numPulses;

  _PulsePainter({
    required this.burstFrac,
    required this.pulseOnFrac,
    required this.numPulses,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final W = size.width;
    final H = size.height;

    // Background
    canvas.drawRRect(
      RRect.fromRectAndRadius(Rect.fromLTWH(0, 0, W, H), const Radius.circular(0)),
      Paint()..color = const Color(0xFF0D0D1A),
    );

    final midY = H * 0.55;
    final highY = H * 0.1;
    final lowY = H * 0.75;
    final lineH = lowY - highY;

    final burstW = (W * burstFrac).clamp(0.0, W);

    // Sleep zone (right of burst) — subtle highlight
    if (burstW < W) {
      canvas.drawRect(
        Rect.fromLTWH(burstW, 0, W - burstW, H),
        Paint()..color = const Color(0xFF161622),
      );
    }

    // Burst zone background
    canvas.drawRect(
      Rect.fromLTWH(0, 0, burstW, H),
      Paint()..color = AppColors.primary.withOpacity(0.08),
    );

    // Baseline
    final basePaint = Paint()
      ..color = AppColors.divider
      ..strokeWidth = 1;
    canvas.drawLine(Offset(0, lowY), Offset(W, lowY), basePaint);

    // Rising edge at start
    if (burstW > 2) {
      // Draw pulses within burst
      final maxDisplay = min(numPulses.ceil(), 60);
      if (numPulses <= 0 || burstFrac <= 0) {
        // No pulse config: draw solid high block
        _drawBlock(canvas, 0, highY, burstW, lineH, AppColors.secondary);
      } else {
        final pulseCycleW = burstW / numPulses;
        final onW = pulseCycleW * pulseOnFrac;

        for (int i = 0; i < maxDisplay; i++) {
          final x0 = pulseCycleW * i;
          if (x0 + onW > burstW) break;
          _drawBlock(canvas, x0, highY, onW, lineH, AppColors.secondary);
          // OFF gap (implicit — background)
        }

        // Vertical edges
        final edgePaint = Paint()
          ..color = AppColors.secondary.withOpacity(0.6)
          ..strokeWidth = 1.5;
        canvas.drawLine(Offset(0, lowY), Offset(0, highY), edgePaint);
        canvas.drawLine(Offset(burstW, highY), Offset(burstW, lowY), edgePaint);
      }
    }

    // Sleep line (low level after burst)
    final sleepPaint = Paint()
      ..color = AppColors.divider
      ..strokeWidth = 1.5;
    if (burstW < W - 1) {
      canvas.drawLine(Offset(burstW, lowY), Offset(W, lowY), sleepPaint);
    }

    // Period boundary marker (dashed at right edge)
    final markerPaint = Paint()
      ..color = AppColors.primary.withOpacity(0.4)
      ..strokeWidth = 1
      ..style = PaintingStyle.stroke;
    const dashH = 4.0;
    for (double y = 0; y < H; y += dashH * 2) {
      canvas.drawLine(Offset(W - 0.5, y), Offset(W - 0.5, y + dashH), markerPaint);
    }

    // Labels inside canvas
    final tp = TextPainter(textDirection: TextDirection.ltr);
    if (burstW > 30) {
      _drawLabel(canvas, tp, 'TX', burstW / 2, highY - 10, AppColors.secondary);
    }
    if (W - burstW > 30) {
      _drawLabel(canvas, tp, 'sleep', burstW + (W - burstW) / 2, midY, AppColors.textSub);
    }
  }

  void _drawBlock(Canvas canvas, double x, double y, double w, double h, Color color) {
    if (w < 1) return;
    canvas.drawRect(
      Rect.fromLTWH(x, y, w, h),
      Paint()..color = color,
    );
  }

  void _drawLabel(Canvas canvas, TextPainter tp, String text, double cx, double cy, Color color) {
    tp.text = TextSpan(
      text: text,
      style: TextStyle(fontSize: 9, color: color),
    );
    tp.layout();
    tp.paint(canvas, Offset(cx - tp.width / 2, cy - tp.height / 2));
  }

  @override
  bool shouldRepaint(_PulsePainter old) =>
      old.burstFrac != burstFrac ||
      old.pulseOnFrac != pulseOnFrac ||
      old.numPulses != numPulses;
}
