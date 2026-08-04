import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import '../protocol/event_model.dart';

// ── Events list tab ───────────────────────────────────────────────────────────

class EventsTab extends StatelessWidget {
  const EventsTab({super.key});

  @override
  Widget build(BuildContext context) {
    final ble       = context.watch<BleProvider>();
    final beacon    = context.watch<BeaconProvider>();
    final connected = ble.isConnected;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Events'),
        actions: [
          if (connected)
            IconButton(
              icon: const Icon(Icons.refresh),
              tooltip: 'Reload from device',
              onPressed: beacon.loadEvents,
            ),
          if (connected)
            IconButton(
              icon: const Icon(Icons.delete_sweep_outlined),
              tooltip: 'Clear all events',
              onPressed: () => _confirmClearAll(context, beacon),
            ),
        ],
      ),
      body: Column(children: [
        if (!connected)
          Container(
            width: double.infinity,
            color: Theme.of(context).colorScheme.surfaceContainerHighest,
            padding: const EdgeInsets.symmetric(vertical: 8, horizontal: 16),
            child: Row(children: [
              Icon(Icons.bluetooth_disabled,
                  size: 16,
                  color: Theme.of(context).colorScheme.onSurfaceVariant),
              const SizedBox(width: 8),
              Text('Not connected — edits saved locally',
                  style: Theme.of(context).textTheme.bodySmall),
            ]),
          ),
        Expanded(
          child: ListView.builder(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            itemCount: maxEvents,
            itemBuilder: (_, i) => _EventCard(
              key: ValueKey(i),
              index: i,
              event: beacon.events[i],
            ),
          ),
        ),
      ]),
    );
  }

  Future<void> _confirmClearAll(BuildContext ctx, BeaconProvider beacon) async {
    final ok = await showDialog<bool>(
      context: ctx,
      builder: (_) => AlertDialog(
        title: const Text('Clear all events?'),
        content: const Text('All 4 event slots will be reset to disabled.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(_, false), child: const Text('Cancel')),
          FilledButton(
            style: FilledButton.styleFrom(backgroundColor: Colors.red),
            onPressed: () => Navigator.pop(_, true),
            child: const Text('Clear all'),
          ),
        ],
      ),
    );
    if (ok == true && ctx.mounted) await beacon.clearAllEvents();
  }
}

// ── Event list card ───────────────────────────────────────────────────────────

class _EventCard extends StatelessWidget {
  final int         index;
  final BeaconEvent event;
  const _EventCard({super.key, required this.index, required this.event});

  @override
  Widget build(BuildContext context) {
    final ev      = event;
    final isEmpty = ev.isEmpty;
    final cs      = Theme.of(context).colorScheme;

    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      clipBehavior: Clip.antiAlias,
      child: ListTile(
        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
        leading: Container(
          width: 28, height: 28,
          decoration: BoxDecoration(
            color: ev.enabled ? cs.primaryContainer : cs.surfaceContainerHighest,
            borderRadius: BorderRadius.circular(6),
          ),
          alignment: Alignment.center,
          child: Text('${index + 1}',
            style: TextStyle(
              fontWeight: FontWeight.bold, fontSize: 13,
              color: ev.enabled ? cs.onPrimaryContainer : cs.onSurfaceVariant,
            )),
        ),
        title: isEmpty
          ? Text('Empty — tap to configure',
              style: TextStyle(
                color: cs.onSurfaceVariant,
                fontStyle: FontStyle.italic, fontSize: 13))
          : Text('IF  ${ev.condSummary}',
              style: TextStyle(
                fontSize: 12,
                color: ev.enabled ? cs.onSurface : cs.onSurfaceVariant)),
        subtitle: isEmpty ? null
          : Text('→  ${ev.actSummary}',
              style: TextStyle(
                fontSize: 13, fontWeight: FontWeight.w600,
                color: ev.enabled ? cs.primary : cs.onSurfaceVariant)),
        trailing: const Icon(Icons.chevron_right),
        onTap: () { HapticFeedback.lightImpact(); _openEditor(context); },
      ),
    );
  }

  Future<void> _openEditor(BuildContext context) async {
    final result = await Navigator.of(context).push<_EditorResult>(
      MaterialPageRoute(builder: (_) =>
          _EventEditorPage(index: index, initial: event)),
    );
    if (result == null || !context.mounted) return;
    final beacon = context.read<BeaconProvider>();
    if (result.clear) {
      await beacon.clearEvent(index);
    } else {
      await beacon.saveEvent(index, result.event);
    }
  }
}

class _EditorResult {
  final bool        clear;
  final BeaconEvent event;
  const _EditorResult({this.clear = false, required this.event});
}

// ── Full-page event editor ────────────────────────────────────────────────────

class _EventEditorPage extends StatefulWidget {
  final int         index;
  final BeaconEvent initial;
  const _EventEditorPage({required this.index, required this.initial});

  @override
  State<_EventEditorPage> createState() => _EventEditorPageState();
}

class _EventEditorPageState extends State<_EventEditorPage> {
  late BeaconEvent _ev;

  @override
  void initState() {
    super.initState();
    _ev = widget.initial.copy();
    if (_ev.conds.isEmpty) _ev.conds.add(EvCond());
  }

  void _save()  => Navigator.pop(context, _EditorResult(event: _ev));
  void _clear() => Navigator.pop(context, _EditorResult(clear: true, event: _ev));

  void _addCond() {
    if (_ev.conds.length < maxConds) {
      setState(() => _ev.conds.add(EvCond()));
    }
  }

  void _removeCond(int i) {
    if (_ev.conds.length > 1) {
      setState(() => _ev.conds.removeAt(i));
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        leading: IconButton(
          icon: const Icon(Icons.close),
          tooltip: 'Cancel',
          onPressed: () => Navigator.pop(context),
        ),
        title: Text('Event ${widget.index + 1}'),
      ),
      bottomNavigationBar: SafeArea(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 12),
          child: Row(children: [
            OutlinedButton.icon(
              style: OutlinedButton.styleFrom(foregroundColor: Colors.red),
              onPressed: () { HapticFeedback.lightImpact(); _clear(); },
              icon: const Icon(Icons.delete_outline, size: 18),
              label: const Text('Clear slot'),
            ),
            const Spacer(),
            FilledButton.icon(
              onPressed: () { HapticFeedback.lightImpact(); _save(); },
              icon: const Icon(Icons.check, size: 18),
              label: const Text('Save'),
            ),
          ]),
        ),
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(16, 16, 16, 8),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [

          // ── Conditions ────────────────────────────────────────────────
          _SectionLabel('Conditions  (AND logic)'),
          const SizedBox(height: 8),

          for (int i = 0; i < _ev.conds.length; i++) ...[
            _CondRow(
              index: i,
              cond:  _ev.conds[i],
              canRemove: _ev.conds.length > 1,
              onChanged: (c) => setState(() => _ev.conds[i] = c),
              onRemove:  () => _removeCond(i),
            ),
            const SizedBox(height: 8),
          ],

          if (_ev.conds.length < maxConds)
            TextButton.icon(
              onPressed: () { HapticFeedback.lightImpact(); _addCond(); },
              icon: const Icon(Icons.add, size: 18),
              label: const Text('Add condition'),
            ),

          const SizedBox(height: 16),

          // ── Action ───────────────────────────────────────────────────
          _SectionLabel('Action'),
          const SizedBox(height: 8),
          _ActionWidget(
            actType: _ev.actType,
            actP1:   _ev.actP1,
            actP2:   _ev.actP2,
            onChanged: (t, p1, p2) => setState(() {
              _ev.actType = t; _ev.actP1 = p1; _ev.actP2 = p2;
            }),
          ),

          const SizedBox(height: 16),

          // ── Options ──────────────────────────────────────────────────
          _SectionLabel('Options'),
          const SizedBox(height: 8),
          _numField(
            label: 'Cooldown (TX cycles)',
            hint:  '0 = fire every cycle',
            value: _ev.cooldown,
            onChanged: (v) => setState(() => _ev.cooldown = v),
          ),
          CheckboxListTile(
            contentPadding: EdgeInsets.zero,
            controlAffinity: ListTileControlAffinity.leading,
            value: _ev.enabled,
            onChanged: (v) { HapticFeedback.lightImpact(); setState(() => _ev.enabled = v!); },
            title: const Text('Enabled'),
          ),
          CheckboxListTile(
            contentPadding: EdgeInsets.zero,
            controlAffinity: ListTileControlAffinity.leading,
            value: _ev.oneShot,
            onChanged: (v) { HapticFeedback.lightImpact(); setState(() => _ev.oneShot = v!); },
            title: const Text('One-shot (fire once, then disable)'),
          ),
        ]),
      ),
    );
  }
}

// ── Condition row ─────────────────────────────────────────────────────────────

class _CondRow extends StatefulWidget {
  final int     index;
  final EvCond  cond;
  final bool    canRemove;
  final void Function(EvCond) onChanged;
  final VoidCallback           onRemove;

  const _CondRow({
    required this.index,
    required this.cond,
    required this.canRemove,
    required this.onChanged,
    required this.onRemove,
  });

  @override
  State<_CondRow> createState() => _CondRowState();
}

class _CondRowState extends State<_CondRow> {
  late EvCond _c;
  late TextEditingController _v1Ctrl;
  late TextEditingController _v2Ctrl;

  @override
  void initState() {
    super.initState();
    _c = widget.cond.copy();
    _v1Ctrl = TextEditingController(text: _c.val1.toString());
    _v2Ctrl = TextEditingController(text: _c.val2.toString());
  }

  @override
  void dispose() {
    _v1Ctrl.dispose();
    _v2Ctrl.dispose();
    super.dispose();
  }

  void _setType(int t) {
    setState(() { _c.type = t; });
    widget.onChanged(_c.copy());
  }

  void _setV1(int v) { _c.val1 = v; widget.onChanged(_c.copy()); }
  void _setV2(int v) { _c.val2 = v; widget.onChanged(_c.copy()); }

  @override
  Widget build(BuildContext context) {
    final label = widget.index == 0 ? 'IF' : 'AND';
    return Card(
      elevation: 0,
      color: Theme.of(context).colorScheme.surfaceContainerLow,
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Row(children: [
            Text(label,
              style: TextStyle(
                fontWeight: FontWeight.w700, fontSize: 11,
                color: Theme.of(context).colorScheme.primary)),
            const SizedBox(width: 8),
            Expanded(
              child: _popup<int>(
                label: 'Condition',
                value: _c.type,
                options: condLabel,
                onChanged: _setType,
              ),
            ),
            if (widget.canRemove)
              IconButton(
                icon: const Icon(Icons.remove_circle_outline, size: 20),
                tooltip: 'Remove condition',
                onPressed: () { HapticFeedback.lightImpact(); widget.onRemove(); },
              ),
          ]),
          const SizedBox(height: 8),
          _buildParams(),
        ]),
      ),
    );
  }

  Widget _buildParams() {
    switch (_c.type) {
      case condBattBelow:
      case condBattAbove:
        return _intField(_v1Ctrl, 'Threshold (%)', '50', _setV1);

      case condTempAbove:
      case condTempBelow:
        return _intField(_v1Ctrl, 'Temperature (°C)', '36', _setV1, signed: true);

      case condNoMotion:
        return _intField(_v1Ctrl, 'Consecutive no-motion cycles', '10', _setV1);

      case condLightBelow:
      case condLightAbove:
        return _intField(_v1Ctrl, 'Light level (%)', '50', _setV1);

      case condEveryNcycles:
        return _intField(_v1Ctrl, 'N (TX cycles)', '10', _setV1);

      case condEveryNhrs:
        return Row(children: [
          Expanded(child: _intField(_v1Ctrl, 'Hours × 60 + Min', '60', _setV1)),
          const SizedBox(width: 8),
          Expanded(child: _intField(_v2Ctrl, 'Extra seconds', '0', _setV2)),
        ]);

      default:
        return const SizedBox.shrink();
    }
  }

  Widget _intField(TextEditingController ctrl, String label, String hint,
      void Function(int) onVal, {bool signed = false}) {
    return TextField(
      controller: ctrl,
      decoration: InputDecoration(
        labelText: label, hintText: hint,
        border: const OutlineInputBorder(),
        isDense: true,
      ),
      keyboardType: TextInputType.numberWithOptions(signed: signed),
      inputFormatters: [FilteringTextInputFormatter.allow(
          signed ? RegExp(r'-?\d*') : RegExp(r'\d*'))],
      onChanged: (s) => onVal(int.tryParse(s) ?? 0),
    );
  }
}

// ── Action widget ─────────────────────────────────────────────────────────────

class _ActionWidget extends StatefulWidget {
  final int    actType;
  final int    actP1;
  final int    actP2;
  final void Function(int type, int p1, int p2) onChanged;

  const _ActionWidget({
    required this.actType, required this.actP1, required this.actP2,
    required this.onChanged,
  });

  @override
  State<_ActionWidget> createState() => _ActionWidgetState();
}

class _ActionWidgetState extends State<_ActionWidget> {
  late int _t;
  late TextEditingController _p1Ctrl;
  late TextEditingController _p2Ctrl;

  @override
  void initState() {
    super.initState();
    _t      = widget.actType;
    _p1Ctrl = TextEditingController(text: widget.actP1.toString());
    _p2Ctrl = TextEditingController(text: widget.actP2.toString());
  }

  @override
  void dispose() { _p1Ctrl.dispose(); _p2Ctrl.dispose(); super.dispose(); }

  void _emit(int t, int p1, int p2) { _t = t; widget.onChanged(t, p1, p2); }
  int _p1() => int.tryParse(_p1Ctrl.text) ?? 0;
  int _p2() => int.tryParse(_p2Ctrl.text) ?? 0;

  @override
  Widget build(BuildContext context) {
    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      _popup<int>(
        label: 'Action',
        value: _t,
        options: actLabel,
        onChanged: (v) {
          HapticFeedback.lightImpact();
          setState(() { _t = v; });
          _emit(v, _p1(), _p2());
        },
      ),
      const SizedBox(height: 8),
      _buildParams(),
    ]);
  }

  Widget _buildParams() {
    switch (_t) {
      case actSetPower:
        return _popup<int>(
          label: 'Power level',
          value: _p1().clamp(0, 2),
          options: const {0: 'Low (0)', 1: 'Mid (1)', 2: 'High (2)'},
          onChanged: (v) {
            _p1Ctrl.text = v.toString();
            _emit(_t, v, _p2());
          },
        );

      case actSetChannel:
        return _popup<int>(
          label: 'Channel',
          value: _p1().clamp(1, 3),
          options: const {1: 'CH1', 2: 'CH2', 3: 'Both'},
          onChanged: (v) {
            _p1Ctrl.text = v.toString();
            _emit(_t, v, _p2());
          },
        );

      case actTxPulses:
        return Row(children: [
          Expanded(child: _numField(_p1Ctrl, 'Count', '1')),
          const SizedBox(width: 12),
          Expanded(child: _numField(_p2Ctrl, 'Gap (ms)', '200')),
        ]);

      case actTxPattern:
        return Row(children: [
          Expanded(child: _numField(_p1Ctrl, 'ON (ms)', '100')),
          const SizedBox(width: 12),
          Expanded(child: _numField(_p2Ctrl, 'OFF (ms)', '900')),
        ]);

      case actSetPeriod:
        return _numField(_p1Ctrl, 'Period (s)', '60');

      case actLogMarker:
        return _numField(_p1Ctrl, 'Marker code', '1');

      case actLedBlink:
        return Row(children: [
          Expanded(child: _numField(_p1Ctrl, 'Count', '3')),
          const SizedBox(width: 12),
          Expanded(child: _numField(_p2Ctrl, 'Period (ms)', '200')),
        ]);

      default:
        return const SizedBox.shrink(); // actNone / actBleStart / actLedOn / actLedOff
    }
  }

  Widget _numField(TextEditingController ctrl, String label, String hint) {
    return TextField(
      controller: ctrl,
      decoration: InputDecoration(
        labelText: label, hintText: hint,
        border: const OutlineInputBorder(),
        isDense: true,
      ),
      keyboardType: TextInputType.number,
      inputFormatters: [FilteringTextInputFormatter.digitsOnly],
      onChanged: (_) => _emit(_t, _p1(), _p2()),
    );
  }
}

// ── Shared popup dropdown ─────────────────────────────────────────────────────

Widget _popup<T>({
  required String labelText,
  required T value,
  required Map<T, String> options,
  required void Function(T) onChanged,
}) {
  final safeVal = options.containsKey(value) ? value : options.keys.first;
  return PopupMenuButton<T>(
    initialValue: safeVal,
    onSelected: (v) { HapticFeedback.lightImpact(); onChanged(v); },
    constraints: const BoxConstraints(maxHeight: 320),
    itemBuilder: (ctx) => options.entries
        .map((e) => PopupMenuItem<T>(value: e.key, child: Text(e.value)))
        .toList(),
    child: InputDecorator(
      decoration: InputDecoration(
        labelText: labelText,
        border: const OutlineInputBorder(),
        contentPadding: const EdgeInsets.fromLTRB(12, 16, 8, 16),
        isDense: true,
      ),
      child: Row(children: [
        Expanded(
          child: Text(options[safeVal] ?? '',
              overflow: TextOverflow.ellipsis, style: const TextStyle(fontSize: 14))),
        const Icon(Icons.arrow_drop_down, size: 22),
      ]),
    ),
  );
}

// ── Section label ─────────────────────────────────────────────────────────────

class _SectionLabel extends StatelessWidget {
  final String text;
  const _SectionLabel(this.text);
  @override
  Widget build(BuildContext context) => Text(
    text,
    style: TextStyle(
      fontSize: 11, fontWeight: FontWeight.w600, letterSpacing: 0.8,
      color: Theme.of(context).colorScheme.primary,
    ),
  );
}
