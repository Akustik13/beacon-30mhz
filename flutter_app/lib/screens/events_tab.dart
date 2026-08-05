import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import '../protocol/event_model.dart';
import '../protocol/opcodes.dart';

// ── Events list tab ───────────────────────────────────────────────────────────

class EventsTab extends StatefulWidget {
  const EventsTab({super.key});
  @override
  State<EventsTab> createState() => _EventsTabState();
}

class _EventsTabState extends State<EventsTab> {
  bool? _markersExpanded; // null = auto: expand when markers exist

  @override
  Widget build(BuildContext context) {
    final ble       = context.watch<BleProvider>();
    final beacon    = context.watch<BeaconProvider>();
    final connected = ble.isConnected;
    final markers   = beacon.logEntries.where((r) => r.containsKey('marker')).toList();
    final showMarkers = _markersExpanded ?? markers.isNotEmpty;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Events'),
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
        // ── Action toolbar ─────────────────────────────────────────────
        Padding(
          padding: const EdgeInsets.fromLTRB(12, 8, 12, 4),
          child: Wrap(
            spacing: 8,
            runSpacing: 4,
            children: [
              OutlinedButton.icon(
                onPressed: connected ? () { HapticFeedback.lightImpact(); beacon.loadEvents(); } : null,
                icon: const Icon(Icons.refresh, size: 15),
                label: const Text('Read', style: TextStyle(fontSize: 13)),
                style: OutlinedButton.styleFrom(
                  minimumSize: const Size(0, 34),
                  padding: const EdgeInsets.symmetric(horizontal: 12),
                  visualDensity: VisualDensity.compact,
                ),
              ),
              OutlinedButton.icon(
                onPressed: (connected && !beacon.isDownloadingLog)
                    ? () { HapticFeedback.lightImpact(); beacon.downloadLog(); }
                    : null,
                icon: const Icon(Icons.download, size: 15),
                label: Text(
                  beacon.isDownloadingLog
                      ? 'Loading… ${(beacon.downloadProgress * 100).round()}%'
                      : 'Load Markers',
                  style: const TextStyle(fontSize: 13),
                ),
                style: OutlinedButton.styleFrom(
                  minimumSize: const Size(0, 34),
                  padding: const EdgeInsets.symmetric(horizontal: 12),
                  visualDensity: VisualDensity.compact,
                ),
              ),
              OutlinedButton.icon(
                onPressed: connected
                    ? () { HapticFeedback.lightImpact(); _confirmClearAll(context, beacon); }
                    : null,
                icon: const Icon(Icons.delete_sweep_outlined, size: 15),
                label: const Text('Clear All', style: TextStyle(fontSize: 13)),
                style: OutlinedButton.styleFrom(
                  foregroundColor: Colors.red,
                  minimumSize: const Size(0, 34),
                  padding: const EdgeInsets.symmetric(horizontal: 12),
                  visualDensity: VisualDensity.compact,
                ),
              ),
            ],
          ),
        ),
        Expanded(
          child: ListView(
            padding: const EdgeInsets.only(top: 8, bottom: 8),
            children: [
              // ── 4 event cards ─────────────────────────────────────────
              for (int i = 0; i < maxEvents; i++)
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 12),
                  child: _EventCard(
                    key: ValueKey(i),
                    index: i,
                    event: beacon.events[i],
                  ),
                ),
              // ── Wake-on-Motion (accelerometer) ────────────────────────
              const _WomSection(),
              // ── Markers panel ─────────────────────────────────────────
              _MarkersPanel(
                markers: markers,
                expanded: showMarkers,
                loading: beacon.isDownloadingLog,
                connected: connected,
                onToggle: () => setState(() => _markersExpanded = !showMarkers),
                onLoad: beacon.downloadLog,
                downloadProgress: beacon.downloadProgress,
                downloadTotal: beacon.logTotal,
              ),
              SizedBox(height: MediaQuery.of(context).padding.bottom + 12),
            ],
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
  late TextEditingController _cooldownCtrl;

  @override
  void initState() {
    super.initState();
    _ev = widget.initial.copy();
    if (_ev.conds.isEmpty) _ev.conds.add(EvCond());
    _cooldownCtrl = TextEditingController(text: _ev.cooldown.toString());
  }

  @override
  void dispose() {
    _cooldownCtrl.dispose();
    super.dispose();
  }

  Widget _numField({required String label, required String hint, required int value, required void Function(int) onChanged}) {
    return TextField(
      controller: _cooldownCtrl,
      keyboardType: TextInputType.number,
      decoration: InputDecoration(
        labelText: label,
        hintText: hint,
        border: const OutlineInputBorder(),
        isDense: true,
        contentPadding: const EdgeInsets.fromLTRB(12, 16, 12, 16),
      ),
      onChanged: (s) { final v = int.tryParse(s); if (v != null) onChanged(v); },
    );
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
              style: FilledButton.styleFrom(minimumSize: const Size(0, 48)),
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
            label: 'Cooldown (s)',
            hint:  '0 = every wake',
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
        return _intField(_v1Ctrl, 'No-motion cycles', '10', _setV1);

      case condLightBelow:
      case condLightAbove:
        return _intField(_v1Ctrl, 'Light level (%)', '50', _setV1);

      case condEveryNcycles:
        return _intField(_v1Ctrl, 'N (wake cycles)', '10', _setV1);

      case condEveryNhrs:
        return Row(children: [
          Expanded(child: _intField(_v1Ctrl, 'Minutes', '60', _setV1)),
          const SizedBox(width: 8),
          Expanded(child: _intField(_v2Ctrl, 'Extra sec', '0', _setV2)),
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
        contentPadding: const EdgeInsets.fromLTRB(12, 18, 12, 10),
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
        contentPadding: const EdgeInsets.fromLTRB(12, 18, 12, 10),
      ),
      keyboardType: TextInputType.number,
      inputFormatters: [FilteringTextInputFormatter.digitsOnly],
      onChanged: (_) => _emit(_t, _p1(), _p2()),
    );
  }
}

// ── Shared popup dropdown ─────────────────────────────────────────────────────

Widget _popup<T>({
  required String label,
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
        labelText: label,
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

// ── Wake-on-Motion section ────────────────────────────────────────────────────

class _WomSection extends StatefulWidget {
  const _WomSection();
  @override
  State<_WomSection> createState() => _WomSectionState();
}

class _WomSectionState extends State<_WomSection> {
  bool _expanded   = false;
  bool _enable     = false;
  int  _threshMg   = 200;
  int  _durMs      = 500;
  int  _action     = wakeActionWakeMcu;
  String _status   = '';

  Future<void> _load() async {
    final w = context.read<BeaconProvider>().wakeCfg;
    if (w == null) return;
    setState(() {
      _enable   = (w['enable'] as int) != 0;
      _threshMg = w['threshold_mg'] as int;
      _durMs    = w['duration_ms']  as int;
      _action   = w['action']       as int;
    });
  }

  Future<void> _save() async {
    final ok = await context.read<BeaconProvider>().writeWakeCfg({
      'enable': _enable ? 1 : 0,
      'threshold_mg': _threshMg,
      'duration_ms': _durMs,
      'action': _action,
    });
    if (mounted) setState(() => _status = ok ? '✓ Saved' : '✗ Failed');
  }

  @override
  Widget build(BuildContext context) {
    final connected = context.watch<BleProvider>().isConnected;
    final cs = Theme.of(context).colorScheme;

    return Container(
      decoration: BoxDecoration(
        border: Border(top: BorderSide(color: cs.outlineVariant)),
      ),
      child: Column(mainAxisSize: MainAxisSize.min, children: [
        InkWell(
          onTap: () {
            setState(() => _expanded = !_expanded);
            if (_expanded) _load();
          },
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
            child: Row(children: [
              const Icon(Icons.motion_photos_on_outlined, size: 16),
              const SizedBox(width: 8),
              const Text('Wake-on-Motion',
                  style: TextStyle(fontWeight: FontWeight.w600, fontSize: 13)),
              if (_enable) ...[
                const SizedBox(width: 8),
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 1),
                  decoration: BoxDecoration(
                    color: Colors.green.withOpacity(0.15),
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: const Text('Armed',
                      style: TextStyle(fontSize: 11, color: Colors.green,
                          fontWeight: FontWeight.w600)),
                ),
              ],
              const Spacer(),
              Icon(_expanded ? Icons.expand_less : Icons.expand_more,
                  size: 18, color: cs.onSurfaceVariant),
            ]),
          ),
        ),
        if (_expanded)
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text('⚠  Sensor must stay powered while WoM is armed.',
                  style: TextStyle(fontSize: 11, color: Colors.orange.shade800)),
              const SizedBox(height: 8),
              SwitchListTile(
                dense: true,
                contentPadding: EdgeInsets.zero,
                title: const Text('Enable', style: TextStyle(fontSize: 13)),
                value: _enable,
                onChanged: (v) => setState(() => _enable = v),
              ),
              Row(children: [
                const Text('Threshold:', style: TextStyle(fontSize: 13)),
                const SizedBox(width: 8),
                Expanded(child: Slider(
                  value: _threshMg.toDouble().clamp(10, 2000),
                  min: 10, max: 2000, divisions: 199,
                  label: '$_threshMg mg',
                  onChanged: (v) => setState(() => _threshMg = v.round()),
                )),
                SizedBox(width: 52,
                    child: Text('$_threshMg mg',
                        style: const TextStyle(fontSize: 12), textAlign: TextAlign.end)),
              ]),
              Row(children: [
                const Text('Duration:', style: TextStyle(fontSize: 13)),
                const SizedBox(width: 8),
                Expanded(child: Slider(
                  value: _durMs.toDouble().clamp(50, 5000),
                  min: 50, max: 5000, divisions: 99,
                  label: '$_durMs ms',
                  onChanged: (v) => setState(() => _durMs = v.round()),
                )),
                SizedBox(width: 52,
                    child: Text('$_durMs ms',
                        style: const TextStyle(fontSize: 12), textAlign: TextAlign.end)),
              ]),
              const SizedBox(height: 4),
              const Text('Action:', style: TextStyle(fontSize: 13)),
              CheckboxListTile(
                dense: true, contentPadding: EdgeInsets.zero,
                title: const Text('Wake MCU', style: TextStyle(fontSize: 13)),
                value: (_action & wakeActionWakeMcu) != 0,
                onChanged: (v) => setState(() =>
                    _action = v! ? _action | wakeActionWakeMcu : _action & ~wakeActionWakeMcu),
              ),
              CheckboxListTile(
                dense: true, contentPadding: EdgeInsets.zero,
                title: const Text('TX burst', style: TextStyle(fontSize: 13)),
                value: (_action & wakeActionTxBurst) != 0,
                onChanged: (v) => setState(() =>
                    _action = v! ? _action | wakeActionTxBurst : _action & ~wakeActionTxBurst),
              ),
              CheckboxListTile(
                dense: true, contentPadding: EdgeInsets.zero,
                title: const Text('Log marker', style: TextStyle(fontSize: 13)),
                value: (_action & wakeActionLogMarker) != 0,
                onChanged: (v) => setState(() =>
                    _action = v! ? _action | wakeActionLogMarker : _action & ~wakeActionLogMarker),
              ),
              const SizedBox(height: 8),
              Row(children: [
                FilledButton(
                  onPressed: connected ? _save : null,
                  style: FilledButton.styleFrom(minimumSize: const Size(0, 40)),
                  child: const Text('Apply'),
                ),
                const SizedBox(width: 12),
                if (_status.isNotEmpty)
                  Text(_status,
                      style: TextStyle(
                          fontSize: 12,
                          color: _status.startsWith('✓') ? Colors.green : Colors.red)),
              ]),
            ]),
          ),
      ]),
    );
  }
}

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

// ── Markers panel ─────────────────────────────────────────────────────────────

class _MarkersPanel extends StatelessWidget {
  final List<Map<String, dynamic>> markers;
  final bool expanded;
  final bool loading;
  final bool connected;
  final VoidCallback onToggle;
  final VoidCallback onLoad;
  final double downloadProgress;
  final int downloadTotal;

  const _MarkersPanel({
    required this.markers,
    required this.expanded,
    required this.loading,
    required this.connected,
    required this.onToggle,
    required this.onLoad,
    required this.downloadProgress,
    required this.downloadTotal,
  });

  String _fmtTime(int ts) {
    if (ts == 0) return '—';
    try {
      final dt = DateTime.fromMillisecondsSinceEpoch(ts * 1000).toLocal();
      return '${dt.year}-${dt.month.toString().padLeft(2,'0')}-${dt.day.toString().padLeft(2,'0')}'
             ' ${dt.hour.toString().padLeft(2,'0')}:${dt.minute.toString().padLeft(2,'0')}:${dt.second.toString().padLeft(2,'0')}';
    } catch (_) { return ts.toString(); }
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final markerColor = const Color(0xFFE67E00);

    return Container(
      decoration: BoxDecoration(
        border: Border(top: BorderSide(color: cs.outlineVariant)),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          // ── Header row ───────────────────────────────────────────────
          InkWell(
            onTap: onToggle,
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
              child: Row(children: [
                Icon(Icons.bookmark_outline, size: 16, color: markerColor),
                const SizedBox(width: 8),
                Text(
                  markers.isEmpty ? 'Log Markers' : 'Log Markers  (${markers.length})',
                  style: TextStyle(
                    fontWeight: FontWeight.w600,
                    fontSize: 13,
                    color: markers.isEmpty ? cs.onSurfaceVariant : markerColor,
                  ),
                ),
                const Spacer(),
                if (loading) ...[
                  SizedBox(
                    width: 120,
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.end,
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Text(
                          downloadTotal > 0
                              ? '${(downloadProgress * downloadTotal).round()}/$downloadTotal'
                              : 'Loading…',
                          style: TextStyle(fontSize: 11, color: cs.onSurfaceVariant),
                        ),
                        const SizedBox(height: 3),
                        LinearProgressIndicator(
                          value: downloadProgress > 0 ? downloadProgress : null,
                          minHeight: 3,
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(width: 8),
                ] else if (connected) ...[
                  TextButton.icon(
                    style: TextButton.styleFrom(
                      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                      minimumSize: Size.zero,
                      tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    ),
                    onPressed: onLoad,
                    icon: const Icon(Icons.download, size: 15),
                    label: const Text('Load', style: TextStyle(fontSize: 12)),
                  ),
                  const SizedBox(width: 4),
                ],
                Icon(
                  expanded ? Icons.expand_less : Icons.expand_more,
                  size: 18,
                  color: cs.onSurfaceVariant,
                ),
              ]),
            ),
          ),
          // ── Expanded content ─────────────────────────────────────────
          if (expanded)
            markers.isEmpty
                ? Padding(
                    padding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
                    child: Text(
                      connected
                          ? 'No markers in log. Press "Load" to download.'
                          : 'Connect device and press "Load" to see markers.',
                      style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant),
                    ),
                  )
                : ListView.builder(
                    shrinkWrap: true,
                    physics: const NeverScrollableScrollPhysics(),
                    padding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
                    itemCount: markers.length,
                    itemBuilder: (_, i) {
                        final r = markers[i];
                        return Padding(
                          padding: const EdgeInsets.symmetric(vertical: 3),
                          child: Row(children: [
                            Text('${i + 1}.',
                                style: TextStyle(fontSize: 11, color: cs.onSurfaceVariant)),
                            const SizedBox(width: 8),
                            Expanded(
                              child: Text(
                                _fmtTime(r['ts'] as int? ?? 0),
                                style: const TextStyle(fontSize: 12,
                                    fontFeatures: [FontFeature.tabularFigures()]),
                              ),
                            ),
                            Container(
                              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                              decoration: BoxDecoration(
                                color: markerColor.withOpacity(0.12),
                                borderRadius: BorderRadius.circular(10),
                              ),
                              child: Text(
                                'Marker #${r['marker']}',
                                style: TextStyle(
                                  fontSize: 12,
                                  fontWeight: FontWeight.w600,
                                  color: markerColor,
                                ),
                              ),
                            ),
                          ]),
                        );
                      },
                    ),
        ],
      ),
    );
  }
}
