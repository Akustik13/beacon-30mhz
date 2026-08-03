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
      body: Column(children: [
        if (!connected) const _OfflineBanner(),
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
      ]),
    );
  }

  Future<void> _confirmClearAll(BuildContext ctx, BeaconProvider beacon) async {
    final ok = await showDialog<bool>(
      context: ctx,
      builder: (_) => AlertDialog(
        title: const Text('Clear all events?'),
        content: const Text('All 7 event slots will be reset to disabled.'),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(_, false),
              child: const Text('Cancel')),
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
        Icon(Icons.bluetooth_disabled,
            size: 16, color: Theme.of(context).colorScheme.onSurfaceVariant),
        const SizedBox(width: 8),
        Text('Not connected — edits saved locally',
            style: Theme.of(context).textTheme.bodySmall),
      ]),
    );
  }
}

// ── Event card (read-only summary + edit button) ───────────────────────────────

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
      child: ListTile(
        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
        leading: Container(
          width: 28, height: 28,
          decoration: BoxDecoration(
            color: badgeColor,
            borderRadius: BorderRadius.circular(6),
          ),
          alignment: Alignment.center,
          child: Text(
            '${index + 1}',
            style: TextStyle(
                fontWeight: FontWeight.bold, fontSize: 13, color: badgeText),
          ),
        ),
        title: isEmpty
            ? Text('Empty slot — tap to configure',
                style: TextStyle(
                    color: cs.onSurfaceVariant,
                    fontStyle: FontStyle.italic,
                    fontSize: 13))
            : Text('IF ${ev.condSummary}',
                style: TextStyle(
                    fontSize: 12,
                    color: ev.enabled ? cs.onSurface : cs.onSurfaceVariant)),
        subtitle: isEmpty
            ? null
            : Text('→ ${ev.actSummary}',
                style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                    color: ev.enabled ? cs.primary : cs.onSurfaceVariant)),
        trailing: IconButton(
          icon: const Icon(Icons.edit_outlined),
          tooltip: 'Edit event',
          onPressed: () => _openEditor(context),
        ),
        onTap: () => _openEditor(context),
      ),
    );
  }

  Future<void> _openEditor(BuildContext context) async {
    final result = await showDialog<_EditorResult>(
      context: context,
      barrierDismissible: false,
      builder: (_) => _EventEditorDialog(index: index, initial: event),
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

// ── Editor result (returned from dialog) ──────────────────────────────────────

class _EditorResult {
  final bool        clear;
  final BeaconEvent event;
  const _EditorResult({this.clear = false, required this.event});
}

// ── Event editor dialog ────────────────────────────────────────────────────────

class _EventEditorDialog extends StatefulWidget {
  final int         index;
  final BeaconEvent initial;
  const _EventEditorDialog({required this.index, required this.initial});

  @override
  State<_EventEditorDialog> createState() => _EventEditorDialogState();
}

class _EventEditorDialogState extends State<_EventEditorDialog> {
  late BeaconEvent _ev;
  late TextEditingController _condValCtrl;
  late TextEditingController _p1Ctrl;
  late TextEditingController _p2Ctrl;
  late TextEditingController _coolCtrl;

  @override
  void initState() {
    super.initState();
    _ev          = widget.initial.copy();
    _condValCtrl = TextEditingController(text: _ev.condVal.toString());
    _p1Ctrl      = TextEditingController(text: _ev.actParam1.toString());
    _p2Ctrl      = TextEditingController(text: _ev.actParam2.toString());
    _coolCtrl    = TextEditingController(text: _ev.cooldown.toString());
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
    return AlertDialog(
      title: Text('Event ${widget.index + 1}'),
      contentPadding: const EdgeInsets.fromLTRB(20, 12, 20, 0),
      content: SizedBox(
        width: double.maxFinite,
        child: SingleChildScrollView(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisSize: MainAxisSize.min,
            children: [
              // ── Condition ─────────────────────────────────────────────────
              _SectionLabel('Condition'),
              const SizedBox(height: 6),
              DropdownButtonFormField<int>(
                value: _ev.condType,
                isExpanded: true,
                decoration: const InputDecoration(
                    labelText: 'Trigger',
                    isDense: true,
                    border: OutlineInputBorder()),
                items: condLabel.entries
                    .map((e) => DropdownMenuItem(
                          value: e.key,
                          child: Text(e.value,
                              overflow: TextOverflow.ellipsis),
                        ))
                    .toList(),
                onChanged: (v) => setState(() {
                  _ev.condType = v!;
                  if (condNoValue.contains(v)) {
                    _condValCtrl.text = '0';
                    _ev.condVal = 0;
                  }
                }),
              ),
              if (!condNoValue.contains(_ev.condType)) ...[
                const SizedBox(height: 8),
                TextField(
                  controller: _condValCtrl,
                  decoration: InputDecoration(
                    labelText: _condValueLabel(_ev.condType),
                    isDense: true,
                    border: const OutlineInputBorder(),
                  ),
                  keyboardType:
                      const TextInputType.numberWithOptions(signed: true),
                  inputFormatters: [
                    FilteringTextInputFormatter.allow(RegExp(r'-?\d*'))
                  ],
                  onChanged: (s) => _ev.condVal = int.tryParse(s) ?? 0,
                ),
              ],
              const SizedBox(height: 4),
              CheckboxListTile(
                dense: true,
                contentPadding: EdgeInsets.zero,
                controlAffinity: ListTileControlAffinity.leading,
                value: _ev.invertCond,
                onChanged: (v) => setState(() => _ev.invertCond = v!),
                title: const Text('Invert condition (NOT)',
                    style: TextStyle(fontSize: 13)),
              ),

              const SizedBox(height: 12),

              // ── Action ────────────────────────────────────────────────────
              _SectionLabel('Action'),
              const SizedBox(height: 6),
              DropdownButtonFormField<int>(
                value: _ev.actType,
                isExpanded: true,
                decoration: const InputDecoration(
                    labelText: 'Do',
                    isDense: true,
                    border: OutlineInputBorder()),
                items: actLabel.entries
                    .map((e) => DropdownMenuItem(
                          value: e.key,
                          child: Text(e.value,
                              overflow: TextOverflow.ellipsis),
                        ))
                    .toList(),
                onChanged: (v) => setState(() => _ev.actType = v!),
              ),
              const SizedBox(height: 8),
              _buildActionParams(),

              const SizedBox(height: 12),

              // ── Options ───────────────────────────────────────────────────
              _SectionLabel('Options'),
              const SizedBox(height: 6),
              TextField(
                controller: _coolCtrl,
                decoration: const InputDecoration(
                  labelText: 'Cooldown (TX cycles)',
                  hintText: '0 = always trigger',
                  isDense: true,
                  border: OutlineInputBorder(),
                ),
                keyboardType: TextInputType.number,
                inputFormatters: [FilteringTextInputFormatter.digitsOnly],
                onChanged: (s) => _ev.cooldown = int.tryParse(s) ?? 0,
              ),
              const SizedBox(height: 4),
              CheckboxListTile(
                dense: true,
                contentPadding: EdgeInsets.zero,
                controlAffinity: ListTileControlAffinity.leading,
                value: _ev.enabled,
                onChanged: (v) => setState(() => _ev.enabled = v!),
                title: const Text('Enabled', style: TextStyle(fontSize: 13)),
              ),
              CheckboxListTile(
                dense: true,
                contentPadding: EdgeInsets.zero,
                controlAffinity: ListTileControlAffinity.leading,
                value: _ev.oneShot,
                onChanged: (v) => setState(() => _ev.oneShot = v!),
                title: const Text('One-shot (fire once, then disable)',
                    style: TextStyle(fontSize: 13)),
              ),
              const SizedBox(height: 4),
            ],
          ),
        ),
      ),
      actionsAlignment: MainAxisAlignment.spaceBetween,
      actions: [
        TextButton.icon(
          style: TextButton.styleFrom(foregroundColor: Colors.red),
          onPressed: () => Navigator.pop(
              context, _EditorResult(clear: true, event: _ev)),
          icon: const Icon(Icons.delete_outline, size: 18),
          label: const Text('Clear slot'),
        ),
        Row(mainAxisSize: MainAxisSize.min, children: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Cancel'),
          ),
          const SizedBox(width: 8),
          FilledButton.icon(
            onPressed: _submit,
            icon: const Icon(Icons.check, size: 18),
            label: const Text('Save'),
          ),
        ]),
      ],
    );
  }

  Widget _buildActionParams() {
    switch (_ev.actType) {
      case actSetPower:
        return _inlineDropdown(
          label: 'Power level',
          ctrl: _p1Ctrl,
          options: const {0: 'Low (0)', 1: 'Mid (1)', 2: 'High (2)'},
          onSel: (v) => _ev.actParam1 = v,
        );

      case actSetChannel:
        return _inlineDropdown(
          label: 'Channel',
          ctrl: _p1Ctrl,
          options: const {1: 'CH1', 2: 'CH2', 3: 'Both'},
          onSel: (v) => _ev.actParam1 = v,
        );

      case actTxPulses:
        return Row(children: [
          Expanded(child: _numField(_p1Ctrl, 'Count', '1',
              (v) => _ev.actParam1 = v)),
          const SizedBox(width: 8),
          Expanded(child: _numField(_p2Ctrl, 'Gap (ms)', '200',
              (v) => _ev.actParam2 = v)),
        ]);

      case actTxPattern:
        return Row(children: [
          Expanded(child: _numField(_p1Ctrl, 'ON (ms)', '100',
              (v) => _ev.actParam1 = v)),
          const SizedBox(width: 8),
          Expanded(child: _numField(_p2Ctrl, 'OFF (ms)', '900',
              (v) => _ev.actParam2 = v)),
        ]);

      case actSetPeriod:
        return _numField(_p1Ctrl, 'Period (s)', '60',
            (v) => _ev.actParam1 = v);

      case actLogMarker:
        return _numField(_p1Ctrl, 'Marker code', '1',
            (v) => _ev.actParam1 = v);

      default:
        return const SizedBox.shrink();
    }
  }

  Widget _inlineDropdown({
    required String label,
    required TextEditingController ctrl,
    required Map<int, String> options,
    required void Function(int) onSel,
  }) {
    final current = int.tryParse(ctrl.text) ?? options.keys.first;
    final val = options.containsKey(current) ? current : options.keys.first;

    return DropdownButtonFormField<int>(
      value: val,
      isExpanded: true,
      decoration: InputDecoration(
          labelText: label, isDense: true, border: const OutlineInputBorder()),
      items: options.entries
          .map((e) => DropdownMenuItem(value: e.key, child: Text(e.value)))
          .toList(),
      onChanged: (v) => setState(() {
        ctrl.text = v.toString();
        onSel(v!);
      }),
    );
  }

  Widget _numField(TextEditingController ctrl, String label, String hint,
      void Function(int) onVal) {
    return TextField(
      controller: ctrl,
      decoration: InputDecoration(
          labelText: label,
          hintText: hint,
          isDense: true,
          border: const OutlineInputBorder()),
      keyboardType: TextInputType.number,
      inputFormatters: [FilteringTextInputFormatter.digitsOnly],
      onChanged: (s) => onVal(int.tryParse(s) ?? 0),
    );
  }

  void _submit() {
    _ev.condVal   = int.tryParse(_condValCtrl.text) ?? _ev.condVal;
    _ev.actParam1 = int.tryParse(_p1Ctrl.text)      ?? _ev.actParam1;
    _ev.actParam2 = int.tryParse(_p2Ctrl.text)      ?? _ev.actParam2;
    _ev.cooldown  = int.tryParse(_coolCtrl.text)    ?? _ev.cooldown;
    Navigator.pop(context, _EditorResult(event: _ev));
  }

  static String _condValueLabel(int cond) {
    switch (cond) {
      case condBatteryBelow:
      case condBatteryAbove:  return '% (0–100)';
      case condTempAbove:
      case condTempBelow:     return '°C';
      case condNoMotion:      return 'N cycles';
      case condLightBelow:
      case condLightAbove:    return '% (0–100)';
      case condEveryNcycles:  return 'N TX cycles';
      case condEveryNhours:   return 'N hours';
      default:                return 'Value';
    }
  }
}

// ── Section label ─────────────────────────────────────────────────────────────

class _SectionLabel extends StatelessWidget {
  final String text;
  const _SectionLabel(this.text);
  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: TextStyle(
        fontSize: 11,
        fontWeight: FontWeight.w600,
        letterSpacing: 0.8,
        color: Theme.of(context).colorScheme.primary,
      ),
    );
  }
}
