import 'package:flutter/material.dart';

/// H:M:S duration picker with scroll-wheel drums.
/// Shows hours only when maxSeconds >= 3600.
class DurationInput extends StatefulWidget {
  final int valueSeconds;
  final int minSeconds;
  final int maxSeconds;
  final ValueChanged<int> onChanged;

  const DurationInput({
    super.key,
    required this.valueSeconds,
    this.minSeconds = 1,
    this.maxSeconds = 86400 * 7,
    required this.onChanged,
  });

  @override
  State<DurationInput> createState() => _DurationInputState();
}

class _DurationInputState extends State<DurationInput> {
  late int _h, _m, _s;
  late FixedExtentScrollController _hCtrl, _mCtrl, _sCtrl;

  bool get _showHours => widget.maxSeconds >= 3600;
  int  get _maxH      => (widget.maxSeconds ~/ 3600).clamp(0, 99);

  @override
  void initState() {
    super.initState();
    _fromTotal(widget.valueSeconds);
    _hCtrl = FixedExtentScrollController(initialItem: _h);
    _mCtrl = FixedExtentScrollController(initialItem: _m);
    _sCtrl = FixedExtentScrollController(initialItem: _s);
  }

  void _fromTotal(int total) {
    _h = total ~/ 3600;
    _m = (total % 3600) ~/ 60;
    _s = total % 60;
  }

  @override
  void didUpdateWidget(DurationInput old) {
    super.didUpdateWidget(old);
    if (old.valueSeconds != widget.valueSeconds) {
      _fromTotal(widget.valueSeconds);
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (_hCtrl.hasClients) _hCtrl.jumpToItem(_h);
        if (_mCtrl.hasClients) _mCtrl.jumpToItem(_m);
        if (_sCtrl.hasClients) _sCtrl.jumpToItem(_s);
      });
    }
  }

  @override
  void dispose() {
    _hCtrl.dispose(); _mCtrl.dispose(); _sCtrl.dispose();
    super.dispose();
  }

  void _emit() {
    final total = (_h * 3600 + _m * 60 + _s)
        .clamp(widget.minSeconds, widget.maxSeconds);
    widget.onChanged(total);
  }

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.end,
      children: [
        if (_showHours) ...[
          _WheelColumn(
            controller: _hCtrl,
            count: _maxH + 1,
            label: 'h',
            onChanged: (v) { setState(() => _h = v); _emit(); },
          ),
          const _WheelSep(),
        ],
        _WheelColumn(
          controller: _mCtrl,
          count: 60,
          label: 'm',
          onChanged: (v) { setState(() => _m = v); _emit(); },
        ),
        const _WheelSep(),
        _WheelColumn(
          controller: _sCtrl,
          count: 60,
          label: 's',
          onChanged: (v) { setState(() => _s = v); _emit(); },
        ),
      ],
    );
  }
}

// ── Internal widgets ──────────────────────────────────────────────────────────

class _WheelColumn extends StatelessWidget {
  static const double _itemH  = 40.0;
  static const double _wheelH = _itemH * 3;   // 3 visible items

  final FixedExtentScrollController controller;
  final int    count;
  final String label;
  final ValueChanged<int> onChanged;

  const _WheelColumn({
    required this.controller,
    required this.count,
    required this.label,
    required this.onChanged,
  });

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;

    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        SizedBox(
          width: 54,
          height: _wheelH,
          child: Stack(
            clipBehavior: Clip.hardEdge,
            children: [
              // Center-row highlight
              Positioned.fill(
                child: Center(
                  child: IgnorePointer(
                    child: Container(
                      height: _itemH - 4,
                      decoration: BoxDecoration(
                        color: cs.primary.withValues(alpha: 0.13),
                        borderRadius: BorderRadius.circular(8),
                        border: Border.all(
                          color: cs.primary.withValues(alpha: 0.45),
                          width: 1.5,
                        ),
                      ),
                    ),
                  ),
                ),
              ),
              // Drum wheel
              ListWheelScrollView.useDelegate(
                controller: controller,
                itemExtent: _itemH,
                perspective: 0.003,
                diameterRatio: 2.8,
                physics: const FixedExtentScrollPhysics(),
                onSelectedItemChanged: onChanged,
                childDelegate: ListWheelChildBuilderDelegate(
                  builder: (ctx, i) {
                    if (i < 0 || i >= count) return null;
                    return Center(
                      child: Text(
                        i.toString().padLeft(2, '0'),
                        style: TextStyle(
                          fontSize: 21,
                          fontWeight: FontWeight.w600,
                          color: cs.onSurface,
                        ),
                      ),
                    );
                  },
                  childCount: count,
                ),
              ),
            ],
          ),
        ),
        Padding(
          padding: const EdgeInsets.only(top: 3),
          child: Text(
            label,
            style: TextStyle(
              fontSize: 10,
              fontWeight: FontWeight.w500,
              color: cs.primary,
            ),
          ),
        ),
      ],
    );
  }
}

class _WheelSep extends StatelessWidget {
  const _WheelSep();

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        SizedBox(
          height: _WheelColumn._wheelH,
          child: Center(
            child: Text(
              ':',
              style: TextStyle(
                fontSize: 22,
                fontWeight: FontWeight.bold,
                color: Theme.of(context).colorScheme.onSurface,
              ),
            ),
          ),
        ),
        const SizedBox(height: 16), // space for label row
      ],
    );
  }
}

/// Millisecond duration input (slider + text field). Used for TX pulse.
class DurationInputMs extends StatefulWidget {
  final int valueMs;
  final int minMs;
  final int maxMs;
  final ValueChanged<int> onChanged;

  const DurationInputMs({
    super.key,
    required this.valueMs,
    this.minMs = 1,
    this.maxMs = 5000,
    required this.onChanged,
  });

  @override
  State<DurationInputMs> createState() => _DurationInputMsState();
}

class _DurationInputMsState extends State<DurationInputMs> {
  late TextEditingController _ctrl;

  @override
  void initState() {
    super.initState();
    _ctrl = TextEditingController(text: widget.valueMs.toString());
  }

  @override
  void didUpdateWidget(DurationInputMs old) {
    super.didUpdateWidget(old);
    if (old.valueMs != widget.valueMs) {
      _ctrl.text = widget.valueMs.toString();
    }
  }

  @override
  void dispose() { _ctrl.dispose(); super.dispose(); }

  int get _current => (int.tryParse(_ctrl.text) ?? widget.valueMs)
      .clamp(widget.minMs, widget.maxMs);

  @override
  Widget build(BuildContext context) {
    return Row(children: [
      Expanded(child: Slider(
        value: widget.valueMs.toDouble().clamp(
            widget.minMs.toDouble(), widget.maxMs.toDouble()),
        min: widget.minMs.toDouble(),
        max: widget.maxMs.toDouble(),
        divisions: (widget.maxMs - widget.minMs).clamp(1, 500),
        onChanged: (v) {
          final ms = v.round();
          _ctrl.text = ms.toString();
          widget.onChanged(ms);
        },
      )),
      const SizedBox(width: 8),
      SizedBox(
        width: 72,
        child: TextField(
          controller: _ctrl,
          keyboardType: TextInputType.number,
          textAlign: TextAlign.center,
          decoration: const InputDecoration(
            labelText: 'ms',
            isDense: true,
            contentPadding: EdgeInsets.symmetric(horizontal: 8, vertical: 8),
            border: OutlineInputBorder(),
          ),
          onChanged: (_) => widget.onChanged(_current),
          onEditingComplete: () => widget.onChanged(_current),
        ),
      ),
    ]);
  }
}
