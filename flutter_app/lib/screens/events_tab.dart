import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import '../protocol/event_model.dart';

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
      body: Column(
        children: [
          if (!connected)
            const _OfflineBanner(),
          Expanded(
            child: ListView.builder(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              itemCount: maxEvents,
              itemBuilder: (ctx, i) => _EventCard(
                key: ValueKey(i),
                index: i,
                event: beacon.events[i],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Future<void> _confirmClearAll(BuildContext ctx, BeaconProvider beacon) async {
    final ok = await showDialog<bool>(
      context: ctx,
      builder: (_) => AlertDialog(
        title: const Text('Clear all events?'),
        content: const Text('All 7 event slots will be reset to disabled.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(_, false),
              child: const Text('Cancel')),
          FilledButton(
            style: FilledButton.styleFrom(backgroundColor: Colors.red),
            onPressed: () => Navigator.pop(_, true),
            child: const Text('Clear all'),
          ),
        ],
      ),
    );
    if (ok == true) await beacon.clearAllEvents();
  }
}

// ── Offline banner ─────────────────────────────────────────────────────────────

class _OfflineBanner extends StatelessWidget {
  const _OfflineBanner();
  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      color: Theme.of(context).colorScheme.surfaceContainerHighest,
      padding: const EdgeInsets.symmetric(vertical: 8, horizontal: 16),
      child: Row(children: [
        Icon(Icons.bluetooth_disabled, size: 16,
            color: Theme.of(context).colorScheme.onSurfaceVariant),
        const SizedBox(width: 8),
        Text('Not connected — edits saved locally',
            style: Theme.of(context).textTheme.bodySmall),
      ]),
    );
  }
}

// ── Event card ────────────────────────────────────────────────────────────────

class _EventCard extends StatelessWidget {
  final int         index;
  final BeaconEvent event;
  const _EventCard({super.key, required this.index, required this.event});

  @override
  Widget build(BuildContext context) {
    final ev      = event;
    final isEmpty = ev.condType == condDisabled && ev.actType == actNone;
    final cs      = Theme.of(context).colorScheme;

    final badgeColor = ev.enabled ? cs.primaryContainer : cs.surfaceContainerHighest;
    final badgeText  = ev.enabled ? cs.onPrimaryContainer : cs.onSurfaceVariant;

    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      clipBehavior: Clip.antiAlias,
      child: ExpansionTile(
        tilePadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 2),
        childrenPadding: EdgeInsets.zero,
        leading: Container(
          width: 28, height: 28,
          decoration: BoxDecoration(
            color: badgeColor,
            borderRadius: BorderRadius.circular(6),
          ),
          alignment: Alignment.center,
          child: Text(
            '${index + 1}',
            style: TextStyle(fontWeight: FontWeight.bold, fontSize: 13,
                color: badgeText),
          ),
        ),
        title: isEmpty
            ? Text('Empty — tap to configure',
                style: TextStyle(color: cs.onSurfaceVariant,
                    fontStyle: FontStyle.italic, fontSize: 13))
            : Text('IF ${ev.condSummary}',
                style: TextStyle(fontSize: 12,
                    color: ev.enabled ? cs.onSurface : cs.onSurfaceVariant)),
        subtitle: isEmpty
            ? null
            : Text('→ ${ev.actSummary}',
                style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600,
                    color: ev.enabled ? cs.primary : cs.onSurfaceVariant)),
        children: [
          Divider(height: 1, color: cs.outlineVariant),
          _EventEditor(
            index: index,
            initial: event,
            onSave:  (e) => context.read<BeaconProvider>().saveEvent(index, e),
            onClear: ()  => context.read<BeaconProvider>().clearEvent(index),
          ),
        ],
      ),
    );
  }
}

// ── Event editor ──────────────────────────────────────────────────────────────

class _EventEditor extends StatefulWidget {
  final int           index;
  final BeaconEvent   initial;
  final void Function(BeaconEvent) onSave;
  final VoidCallback  onClear;
  const _EventEditor({required this.index, required this.initial,
      required this.onSave, required this.onClear});
  @override
  State<_EventEditor> createState() => _EventEditorState();
}

class _EventEditorState extends State<_EventEditor> {
  late BeaconEvent _ev;
  final _condValCtrl  = TextEditingController();
  final _p1Ctrl       = TextEditingController();
  final _p2Ctrl       = TextEditingController();
  final _coolCtrl     = TextEditingController();

  @override
  void initState() {
    super.initState();
    _ev = widget.initial.copy();
    _condValCtrl.text = _ev.condVal.toString();
    _p1Ctrl.text      = _ev.actParam1.toString();
    _p2Ctrl.text      = _ev.actParam2.toString();
    _coolCtrl.text    = _ev.cooldown.toString();
  }

  @override
  void dispose() {
    _condValCtrl.dispose();
    _p1Ctrl.dispose();
    _p2Ctrl.dispose();
    _coolCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;

    return Container(
      decoration: BoxDecoration(
        border: Border(top: BorderSide(color: cs.outlineVariant)),
      ),
      padding: const EdgeInsets.fromLTRB(16, 12, 16, 16),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [

        // ── Condition ─────────────────────────────────────────────────────────
        _SectionLabel('Condition'),
        const SizedBox(height: 6),
        Row(children: [
          Expanded(
            flex: 2,
            child: DropdownButtonFormField<int>(
              value: _ev.condType,
              decoration: const InputDecoration(
                  labelText: 'Trigger', isDense: true, border: OutlineInputBorder()),
              items: condLabel.entries.map((e) => DropdownMenuItem(
                value: e.key,
                child: Text(e.value, overflow: TextOverflow.ellipsis),
              )).toList(),
              onChanged: (v) => setState(() {
                _ev.condType = v!;
                if (condNoValue.contains(v)) {
                  _condValCtrl.text = '0';
                  _ev.condVal = 0;
                }
              }),
            ),
          ),
          if (!condNoValue.contains(_ev.condType)) ...[
            const SizedBox(width: 8),
            Expanded(
              child: TextField(
                controller: _condValCtrl,
                decoration: InputDecoration(
                  labelText: _condValueLabel(_ev.condType),
                  isDense: true,
                  border: const OutlineInputBorder(),
                ),
                keyboardType:
                    const TextInputType.numberWithOptions(signed: true),
                inputFormatters: [FilteringTextInputFormatter.allow(
                    RegExp(r'-?\d*'))],
                onChanged: (s) => _ev.condVal = int.tryParse(s) ?? 0,
              ),
            ),
          ],
        ]),
        const SizedBox(height: 4),
        Row(children: [
          Checkbox(
            value: _ev.invertCond,
            onChanged: (v) => setState(() => _ev.invertCond = v!),
          ),
          const Text('Invert condition (NOT)', style: TextStyle(fontSize: 13)),
        ]),

        const SizedBox(height: 12),

        // ── Action ────────────────────────────────────────────────────────────
        _SectionLabel('Action'),
        const SizedBox(height: 6),
        DropdownButtonFormField<int>(
          value: _ev.actType,
          decoration: const InputDecoration(
              labelText: 'Do', isDense: true, border: OutlineInputBorder()),
          items: actLabel.entries.map((e) => DropdownMenuItem(
            value: e.key,
            child: Text(e.value),
          )).toList(),
          onChanged: (v) => setState(() => _ev.actType = v!),
        ),
        const SizedBox(height: 8),
        _ActionParams(actType: _ev.actType, p1: _p1Ctrl, p2: _p2Ctrl,
            onChanged: () {
              _ev.actParam1 = int.tryParse(_p1Ctrl.text) ?? 0;
              _ev.actParam2 = int.tryParse(_p2Ctrl.text) ?? 0;
            }),

        const SizedBox(height: 12),

        // ── Options ───────────────────────────────────────────────────────────
        _SectionLabel('Options'),
        const SizedBox(height: 6),
        Row(children: [
          Expanded(
            child: TextField(
              controller: _coolCtrl,
              decoration: const InputDecoration(
                labelText: 'Cooldown (TX cycles)',
                isDense: true,
                border: OutlineInputBorder(),
                hintText: '0 = always',
              ),
              keyboardType: TextInputType.number,
              inputFormatters: [FilteringTextInputFormatter.digitsOnly],
              onChanged: (s) => _ev.cooldown = int.tryParse(s) ?? 0,
            ),
          ),
          const SizedBox(width: 16),
          Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Row(children: [
              Checkbox(
                value: _ev.enabled,
                onChanged: (v) => setState(() => _ev.enabled = v!),
              ),
              const Text('Enabled', style: TextStyle(fontSize: 13)),
            ]),
            Row(children: [
              Checkbox(
                value: _ev.oneShot,
                onChanged: (v) => setState(() => _ev.oneShot = v!),
              ),
              const Text('One-shot', style: TextStyle(fontSize: 13)),
            ]),
          ]),
        ]),

        const SizedBox(height: 16),

        // ── Buttons ───────────────────────────────────────────────────────────
        Row(children: [
          OutlinedButton.icon(
            style: OutlinedButton.styleFrom(foregroundColor: Colors.red),
            onPressed: widget.onClear,
            icon: const Icon(Icons.delete_outline, size: 18),
            label: const Text('Clear slot'),
          ),
          const Spacer(),
          FilledButton.icon(
            onPressed: _submit,
            icon: const Icon(Icons.check, size: 18),
            label: const Text('Save'),
          ),
        ]),
      ]),
    );
  }

  void _submit() {
    _ev.condVal   = int.tryParse(_condValCtrl.text) ?? _ev.condVal;
    _ev.actParam1 = int.tryParse(_p1Ctrl.text)      ?? _ev.actParam1;
    _ev.actParam2 = int.tryParse(_p2Ctrl.text)      ?? _ev.actParam2;
    _ev.cooldown  = int.tryParse(_coolCtrl.text)    ?? _ev.cooldown;
    widget.onSave(_ev);
  }

  static String _condValueLabel(int cond) {
    switch (cond) {
      case condBatteryBelow:
      case condBatteryAbove:  return '% (0-100)';
      case condTempAbove:
      case condTempBelow:     return '°C';
      case condNoMotion:      return 'N cycles';
      case condLightBelow:
      case condLightAbove:    return '% (0-100)';
      case condEveryNcycles:  return 'N cycles';
      case condEveryNhours:   return 'N hours';
      default:                return 'Value';
    }
  }
}

// ── Action-specific parameter rows ────────────────────────────────────────────

class _ActionParams extends StatelessWidget {
  final int actType;
  final TextEditingController p1, p2;
  final VoidCallback onChanged;
  const _ActionParams({required this.actType, required this.p1,
      required this.p2, required this.onChanged});

  @override
  Widget build(BuildContext context) {
    switch (actType) {
      case actSetPower:
        return _DropdownParam(
          label: 'Power level',
          ctrl: p1,
          options: const {0: 'Low (0)', 1: 'Mid (1)', 2: 'High (2)'},
          onChanged: onChanged,
        );

      case actTxPulses:
        return Row(children: [
          Expanded(child: _NumField(ctrl: p1, label: 'Count',   hint: '1',    onChange: onChanged)),
          const SizedBox(width: 8),
          Expanded(child: _NumField(ctrl: p2, label: 'Gap (ms)', hint: '200', onChange: onChanged)),
        ]);

      case actTxPattern:
        return Row(children: [
          Expanded(child: _NumField(ctrl: p1, label: 'ON (ms)',  hint: '100', onChange: onChanged)),
          const SizedBox(width: 8),
          Expanded(child: _NumField(ctrl: p2, label: 'OFF (ms)', hint: '900', onChange: onChanged)),
        ]);

      case actSetChannel:
        return _DropdownParam(
          label: 'Channel',
          ctrl: p1,
          options: const {1: 'CH1', 2: 'CH2', 3: 'Both'},
          onChanged: onChanged,
        );

      case actSetPeriod:
        return _NumField(ctrl: p1, label: 'Period (s)', hint: '60',
            onChange: onChanged);

      case actLogMarker:
        return _NumField(ctrl: p1, label: 'Marker code', hint: '1',
            onChange: onChanged);

      case actBleStart:
      case actNone:
      default:
        return const SizedBox.shrink();
    }
  }
}

// ── Small helper widgets ──────────────────────────────────────────────────────

class _NumField extends StatelessWidget {
  final TextEditingController ctrl;
  final String label, hint;
  final VoidCallback onChange;
  const _NumField({required this.ctrl, required this.label,
      required this.hint, required this.onChange});

  @override
  Widget build(BuildContext context) {
    return TextField(
      controller: ctrl,
      decoration: InputDecoration(
          labelText: label, hintText: hint,
          isDense: true, border: const OutlineInputBorder()),
      keyboardType: TextInputType.number,
      inputFormatters: [FilteringTextInputFormatter.digitsOnly],
      onChanged: (_) => onChange(),
    );
  }
}

class _DropdownParam extends StatefulWidget {
  final String label;
  final TextEditingController ctrl;
  final Map<int, String> options;
  final VoidCallback onChanged;
  const _DropdownParam({required this.label, required this.ctrl,
      required this.options, required this.onChanged});
  @override
  State<_DropdownParam> createState() => _DropdownParamState();
}

class _DropdownParamState extends State<_DropdownParam> {
  late int _val;
  @override
  void initState() {
    super.initState();
    _val = int.tryParse(widget.ctrl.text) ?? widget.options.keys.first;
    if (!widget.options.containsKey(_val)) _val = widget.options.keys.first;
  }

  @override
  Widget build(BuildContext context) {
    return DropdownButtonFormField<int>(
      value: _val,
      decoration: InputDecoration(
          labelText: widget.label, isDense: true,
          border: const OutlineInputBorder()),
      items: widget.options.entries
          .map((e) => DropdownMenuItem(value: e.key, child: Text(e.value)))
          .toList(),
      onChanged: (v) {
        setState(() => _val = v!);
        widget.ctrl.text = v.toString();
        widget.onChanged();
      },
    );
  }
}

class _SectionLabel extends StatelessWidget {
  final String text;
  const _SectionLabel(this.text);
  @override
  Widget build(BuildContext context) {
    return Text(text,
        style: TextStyle(
          fontSize: 11,
          fontWeight: FontWeight.w600,
          letterSpacing: 0.8,
          color: Theme.of(context).colorScheme.primary,
        ));
  }
}
