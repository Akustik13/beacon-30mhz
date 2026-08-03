import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import '../protocol/config_blob.dart';
import 'widgets/duration_input.dart';

class BeaconTab extends StatefulWidget {
  const BeaconTab({super.key});
  @override
  State<BeaconTab> createState() => _BeaconTabState();
}

class _BeaconTabState extends State<BeaconTab> {
  // ── Form state ─────────────────────────────────────────────────────────────
  int  _rfMode      = 1;
  int  _rfChannel   = 0;
  int  _rfPower     = 4;
  int  _rfPulseMs   = 23;
  int  _rfPeriodMs  = 2000;
  int  _schedEn     = 0;
  int  _schedHours  = 0;
  int  _schedDays   = 0x7F;
  int  _schedMonths = 0x0FFF;
  bool _dirty       = false;
  bool _loaded      = false;

  // ── Profiles ───────────────────────────────────────────────────────────────
  static const _prefsKey = 'beacon_profiles_v1';
  Map<String, Map<String, dynamic>> _profiles = {};
  String? _selectedProfile;

  static const _builtinProfiles = <String, Map<String, dynamic>>{
    'Always active': {
      'rfMode': 1, 'rfChannel': 0, 'rfPower': 4, 'rfPulseMs': 23,
      'rfPeriodMs': 2000, 'schedEn': 0, 'schedHours': 0xFFFFFF,
      'schedDays': 0x7F, 'schedMonths': 0x0FFF,
    },
    'Day (8:00–20:00)': {
      'rfMode': 1, 'rfChannel': 0, 'rfPower': 2, 'rfPulseMs': 23,
      'rfPeriodMs': 5000, 'schedEn': 1, 'schedHours': 0x0FFFFF,
      'schedDays': 0x7F, 'schedMonths': 0x0FFF,
    },
    'Night (20:00–8:00)': {
      'rfMode': 1, 'rfChannel': 0, 'rfPower': 2, 'rfPulseMs': 23,
      'rfPeriodMs': 5000, 'schedEn': 1, 'schedHours': 0xFF000001,
      'schedDays': 0x7F, 'schedMonths': 0x0FFF,
    },
    'Field season': {
      'rfMode': 1, 'rfChannel': 0, 'rfPower': 4, 'rfPulseMs': 23,
      'rfPeriodMs': 2000, 'schedEn': 1, 'schedHours': 0x0FFFFF,
      'schedDays': 0x7F, 'schedMonths': 0x03C0,  // Apr–Sep
    },
  };

  @override
  void initState() {
    super.initState();
    _loadProfiles();
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final cfg = context.watch<BeaconProvider>().config;
    if (cfg != null && !_loaded) {
      _loadFromConfig(cfg);
      _loaded = true;
      _selectedProfile = null;
    }
    if (!context.watch<BleProvider>().isConnected) _loaded = false;
  }

  Future<void> _loadProfiles() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_prefsKey);
    if (raw != null) {
      try {
        final decoded = jsonDecode(raw) as Map<String, dynamic>;
        setState(() {
          _profiles = decoded.map((k, v) => MapEntry(k, Map<String, dynamic>.from(v as Map)));
        });
      } catch (_) {}
    }
  }

  Future<void> _persistProfiles() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_prefsKey, jsonEncode(_profiles));
  }

  void _applyProfile(String name) {
    final src = _builtinProfiles[name] ?? _profiles[name];
    if (src == null) return;
    setState(() {
      _rfMode       = (src['rfMode']      as int?) ?? 1;
      _rfChannel    = (src['rfChannel']   as int?) ?? 0;
      _rfPower      = (src['rfPower']     as int?) ?? 4;
      _rfPulseMs    = (src['rfPulseMs']   as int?) ?? 23;
      _rfPeriodMs   = (src['rfPeriodMs']  as int?) ?? 2000;
      _schedEn      = (src['schedEn']     as int?) ?? 0;
      _schedHours   = (src['schedHours']  as int?) ?? 0;
      _schedDays    = (src['schedDays']   as int?) ?? 0x7F;
      _schedMonths  = (src['schedMonths'] as int?) ?? 0x0FFF;
      _selectedProfile = name;
      _dirty = false;
    });
  }

  void _loadFromConfig(ConfigBlob cfg) {
    setState(() {
      _rfMode      = cfg.rfMode;
      _rfChannel   = cfg.rfChannel;
      _rfPower     = cfg.rfPower;
      _rfPulseMs   = cfg.rfPulseMs;
      _rfPeriodMs  = cfg.rfPeriodMs;
      _schedEn     = cfg.schedEn;
      _schedHours  = cfg.schedHours;
      _schedDays   = cfg.schedDays;
      _schedMonths = cfg.schedMonths;
      _dirty = false;
    });
  }

  ConfigBlob _buildConfig() {
    final base = context.read<BeaconProvider>().config ?? ConfigBlob();
    return ConfigBlob(
      protoVer:    base.protoVer,
      rfMode:      _rfMode,
      rfChannel:   _rfChannel,
      rfPower:     _rfPower,
      ledMode:     base.ledMode,
      rfPulseMs:   _rfPulseMs,
      rfPeriodMs:  _rfPeriodMs,
      tempIvS:     base.tempIvS,
      lightIvS:    base.lightIvS,
      batIvS:      base.batIvS,
      tempOffset01c: base.tempOffset01c,
      batScaleX100:  base.batScaleX100,
      logMask:     base.logMask,
      logMode:     base.logMode,
      logOverflow: base.logOverflow,
      logCkpN:     base.logCkpN,
      logDt01c:    base.logDt01c,
      logDlPct:    base.logDlPct,
      logDbPct:    base.logDbPct,
      logTsSource: base.logTsSource,
      schedEn:     _schedEn,
      schedScope:  base.schedScope,
      schedHours:  _schedHours,
      schedDays:   _schedDays,
      schedMonths: _schedMonths,
      uptimeSaveMin: base.uptimeSaveMin,
    );
  }

  Future<void> _apply() async {
    final beacon = context.read<BeaconProvider>();
    if (beacon.config == null) {
      if (mounted) ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
        content: Text('Config not loaded yet — pull to refresh'),
        backgroundColor: Colors.orange,
      ));
      return;
    }
    final ok = await beacon.writeConfig(_buildConfig());
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text(ok ? 'Config applied' : beacon.lastError ?? 'Failed'),
        backgroundColor: ok ? Colors.green : Colors.red,
      ));
      if (ok) setState(() => _dirty = false);
    }
  }

  Future<void> _showSaveProfileDialog() async {
    final ctrl = TextEditingController();
    final name = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Save profile'),
        content: TextField(
          controller: ctrl,
          autofocus: true,
          decoration: const InputDecoration(labelText: 'Profile name'),
          textCapitalization: TextCapitalization.words,
          onSubmitted: (v) => Navigator.pop(ctx, v.trim()),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, ctrl.text.trim()),
            child: const Text('Save'),
          ),
        ],
      ),
    );
    if (name == null || name.isEmpty || !mounted) return;
    if (_builtinProfiles.containsKey(name)) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Cannot overwrite built-in profile')));
      return;
    }
    _profiles[name] = {
      'rfMode': _rfMode, 'rfChannel': _rfChannel, 'rfPower': _rfPower,
      'rfPulseMs': _rfPulseMs, 'rfPeriodMs': _rfPeriodMs,
      'schedEn': _schedEn, 'schedHours': _schedHours,
      'schedDays': _schedDays, 'schedMonths': _schedMonths,
    };
    await _persistProfiles();
    setState(() { _selectedProfile = name; _dirty = false; });
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Profile "$name" saved')));
    }
  }

  Future<void> _deleteProfile(String name) async {
    if (_builtinProfiles.containsKey(name)) return;
    _profiles.remove(name);
    await _persistProfiles();
    setState(() { if (_selectedProfile == name) _selectedProfile = null; });
  }

  @override
  Widget build(BuildContext context) {
    final ble    = context.watch<BleProvider>();
    final beacon = context.watch<BeaconProvider>();
    final connected = ble.isConnected;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Beacon Config'),
        actions: [
          if (beacon.isBusy)
            const Padding(padding: EdgeInsets.all(16),
              child: SizedBox(width: 20, height: 20,
                child: CircularProgressIndicator(strokeWidth: 2))),
        ],
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(12),
        child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [

          // ── Not-connected banner ─────────────────────────────────────────
          if (!connected)
            Container(
              margin: const EdgeInsets.only(bottom: 10),
              padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
              decoration: BoxDecoration(
                color: Colors.orange.withOpacity(0.15),
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: Colors.orange.withOpacity(0.4)),
              ),
              child: Row(children: [
                const Icon(Icons.bluetooth_disabled, size: 16, color: Colors.orange),
                const SizedBox(width: 8),
                const Expanded(child: Text('Not connected — changes saved locally only',
                    style: TextStyle(fontSize: 13, color: Colors.orange))),
              ]),
            ),

          // ── Profile selector bar ─────────────────────────────────────────
          _ProfileBar(
            profiles: {..._builtinProfiles, ..._profiles},
            builtins: _builtinProfiles.keys.toSet(),
            selected: _selectedProfile,
            onSelect: _applyProfile,
            onDelete: _deleteProfile,
          ),
          const SizedBox(height: 10),

          // ── Transmitter section ──────────────────────────────────────────
          _section('Transmitter', [
            _labelDropdown('Mode', _rfMode, [
              const DropdownMenuItem(value: 0, child: Text('Off')),
              const DropdownMenuItem(value: 1, child: Text('Pulse')),
              const DropdownMenuItem(value: 2, child: Text('Continuous')),
              const DropdownMenuItem(value: 3, child: Text('Eco')),
            ], (v) => setState(() { _rfMode = v!; _dirty = true; _selectedProfile = null; })),
            _labelDropdown('Channel', _rfChannel, [
              const DropdownMenuItem(value: 0, child: Text('CH0')),
              const DropdownMenuItem(value: 1, child: Text('CH1')),
              const DropdownMenuItem(value: 2, child: Text('CH2')),
              const DropdownMenuItem(value: 3, child: Text('CH3')),
            ], (v) { HapticFeedback.lightImpact(); setState(() { _rfChannel = v!; _dirty = true; _selectedProfile = null; }); }),
            _labelDropdown('Power', _rfPower, [
              const DropdownMenuItem(value: 1, child: Text('Low (1)')),
              const DropdownMenuItem(value: 2, child: Text('Mid (2)')),
              const DropdownMenuItem(value: 3, child: Text('High (3)')),
              const DropdownMenuItem(value: 4, child: Text('Max (4)')),
            ], (v) => setState(() { _rfPower = v!; _dirty = true; _selectedProfile = null; })),
            _labelWidget('Pulse', DurationInputMs(
              valueMs: _rfPulseMs, minMs: 1, maxMs: 5000,
              onChanged: (v) { HapticFeedback.lightImpact(); setState(() { _rfPulseMs = v; _dirty = true; _selectedProfile = null; }); },
            )),
            _labelWidget('Period', DurationInput(
              valueSeconds: _rfPeriodMs ~/ 1000, minSeconds: 1, maxSeconds: 600,
              onChanged: (v) { HapticFeedback.lightImpact(); setState(() { _rfPeriodMs = v * 1000; _dirty = true; _selectedProfile = null; }); },
            )),
          ]),
          const SizedBox(height: 8),

          // ── Schedule section ─────────────────────────────────────────────
          _section('Schedule', [
            SwitchListTile(
              title: const Text('Enable Schedule'),
              value: _schedEn != 0,
              onChanged: (v) => setState(() { _schedEn = v ? 1 : 0; _dirty = true; _selectedProfile = null; }),
              contentPadding: EdgeInsets.zero,
            ),
            if (_schedEn != 0) ...[
              const SizedBox(height: 8),
              const Text('Active Hours', style: TextStyle(fontWeight: FontWeight.w600)),
              const SizedBox(height: 6),
              _HourGrid(mask: _schedHours,
                  onChanged: (m) => setState(() { _schedHours = m; _dirty = true; _selectedProfile = null; })),
              const SizedBox(height: 12),
              const Text('Active Days', style: TextStyle(fontWeight: FontWeight.w600)),
              const SizedBox(height: 6),
              _DayPicker(mask: _schedDays,
                  onChanged: (m) => setState(() { _schedDays = m; _dirty = true; _selectedProfile = null; })),
              const SizedBox(height: 12),
              const Text('Active Months', style: TextStyle(fontWeight: FontWeight.w600)),
              const SizedBox(height: 6),
              _MonthPicker(mask: _schedMonths,
                  onChanged: (m) => setState(() { _schedMonths = m; _dirty = true; _selectedProfile = null; })),
            ],
          ]),
          const SizedBox(height: 16),

          // ── Footer buttons ───────────────────────────────────────────────
          if (connected)
            FilledButton.icon(
              onPressed: (!beacon.isBusy && _dirty) ? _apply : null,
              icon: const Icon(Icons.check),
              label: Text(_dirty ? 'Apply to beacon' : 'No changes'),
            )
          else
            FilledButton.icon(
              onPressed: _dirty ? _showSaveProfileDialog : null,
              icon: const Icon(Icons.save_outlined),
              label: Text(_dirty ? 'Save as profile…' : 'No changes'),
            ),
          const SizedBox(height: 8),
          // Bug 8: Save-as-profile also available when connected
          OutlinedButton.icon(
            onPressed: _showSaveProfileDialog,
            icon: const Icon(Icons.bookmark_add_outlined, size: 18),
            label: const Text('Save current as profile…'),
          ),
          const SizedBox(height: 8),
          OutlinedButton.icon(
            onPressed: _dirty ? () {
              final cfg = context.read<BeaconProvider>().config;
              if (cfg != null) { _loadFromConfig(cfg); setState(() => _selectedProfile = null); }
              else { _applyProfile('Always active'); }
            } : null,
            icon: const Icon(Icons.refresh),
            label: const Text('Revert'),
          ),
          // Bug 3: ensure last card isn't hidden behind nav bar
          SizedBox(height: MediaQuery.of(context).padding.bottom + 24),
        ]),
      ),
    );
  }

  Widget _section(String title, List<Widget> children) => Card(
    child: Padding(
      padding: const EdgeInsets.all(14),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(title, style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 15)),
          const SizedBox(height: 10),
          ...children,
        ],
      ),
    ),
  );

  Widget _labelDropdown<T>(String label, T value,
      List<DropdownMenuItem<T>> items, ValueChanged<T?> onChanged) =>
    Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(children: [
        SizedBox(width: 80, child: Text(label, style: const TextStyle(fontSize: 13))),
        Expanded(child: DropdownButtonFormField<T>(
          value: value,
          onChanged: onChanged,
          items: items,
          isDense: true,
          decoration: const InputDecoration(contentPadding:
              EdgeInsets.symmetric(horizontal: 12, vertical: 8)),
        )),
      ]),
    );

  Widget _labelWidget(String label, Widget child) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 6),
    child: Row(crossAxisAlignment: CrossAxisAlignment.center, children: [
      SizedBox(width: 60, child: Text(label, style: const TextStyle(fontSize: 13))),
      const SizedBox(width: 8),
      Expanded(child: child),
    ]),
  );
}

// ── Profile selector bar ────────────────────────────────────────────────────

class _ProfileBar extends StatelessWidget {
  final Map<String, Map<String, dynamic>> profiles;
  final Set<String> builtins;
  final String? selected;
  final void Function(String) onSelect;
  final void Function(String) onDelete;

  const _ProfileBar({
    required this.profiles,
    required this.builtins,
    required this.selected,
    required this.onSelect,
    required this.onDelete,
  });

  @override
  Widget build(BuildContext context) {
    if (profiles.isEmpty) return const SizedBox.shrink();

    return Card(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
        child: Row(children: [
          const Icon(Icons.bookmark_outline, size: 18),
          const SizedBox(width: 8),
          Expanded(
            child: DropdownButtonHideUnderline(
              child: DropdownButton<String>(
                isExpanded: true,
                hint: const Text('Load profile…', style: TextStyle(fontSize: 13)),
                value: selected,
                isDense: true,
                items: profiles.keys.map((name) => DropdownMenuItem(
                  value: name,
                  child: Row(children: [
                    if (builtins.contains(name))
                      const Icon(Icons.lock_outline, size: 13, color: Colors.grey)
                    else
                      const Icon(Icons.person_outline, size: 13),
                    const SizedBox(width: 6),
                    Text(name, style: const TextStyle(fontSize: 13)),
                  ]),
                )).toList(),
                onChanged: (v) { if (v != null) onSelect(v); },
              ),
            ),
          ),
          if (selected != null && !builtins.contains(selected))
            IconButton(
              icon: const Icon(Icons.delete_outline, size: 18, color: Colors.red),
              tooltip: 'Delete profile',
              onPressed: () => onDelete(selected!),
              constraints: const BoxConstraints(),
              padding: const EdgeInsets.symmetric(horizontal: 8),
            ),
        ]),
      ),
    );
  }
}

// ── Hour grid ───────────────────────────────────────────────────────────────

class _HourGrid extends StatelessWidget {
  final int mask;
  final ValueChanged<int> onChanged;
  const _HourGrid({required this.mask, required this.onChanged});

  @override
  Widget build(BuildContext context) {
    return GridView.builder(
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
        crossAxisCount: 6, childAspectRatio: 1.5, mainAxisSpacing: 4, crossAxisSpacing: 4),
      itemCount: 24,
      itemBuilder: (_, h) {
        final on = (mask >> h) & 1 == 1;
        return GestureDetector(
          onTap: () => onChanged(on ? mask & ~(1 << h) : mask | (1 << h)),
          child: Container(
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: on ? Theme.of(context).colorScheme.primary : Colors.transparent,
              borderRadius: BorderRadius.circular(4),
              border: Border.all(color: Theme.of(context).dividerColor),
            ),
            child: Text('$h', style: TextStyle(
              fontSize: 11,
              color: on ? Colors.white : Theme.of(context).textTheme.bodySmall?.color,
            )),
          ),
        );
      },
    );
  }
}

// ── Day picker ──────────────────────────────────────────────────────────────

class _DayPicker extends StatelessWidget {
  final int mask;
  final ValueChanged<int> onChanged;
  const _DayPicker({required this.mask, required this.onChanged});
  static const _days = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: List.generate(7, (i) {
        final on = (mask >> i) & 1 == 1;
        return GestureDetector(
          onTap: () => onChanged(on ? mask & ~(1 << i) : mask | (1 << i)),
          child: Container(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
            decoration: BoxDecoration(
              color: on ? Theme.of(context).colorScheme.primary : Colors.transparent,
              borderRadius: BorderRadius.circular(6),
              border: Border.all(color: Theme.of(context).dividerColor),
            ),
            child: Text(_days[i], style: TextStyle(
              fontSize: 11,
              color: on ? Colors.white : Theme.of(context).textTheme.bodySmall?.color,
            )),
          ),
        );
      }),
    );
  }
}

// ── Month picker ────────────────────────────────────────────────────────────

class _MonthPicker extends StatelessWidget {
  final int mask;
  final ValueChanged<int> onChanged;
  const _MonthPicker({required this.mask, required this.onChanged});
  static const _months = ['Jan','Feb','Mar','Apr','May','Jun',
                          'Jul','Aug','Sep','Oct','Nov','Dec'];

  @override
  Widget build(BuildContext context) {
    return GridView.builder(
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
        crossAxisCount: 6, childAspectRatio: 1.8, mainAxisSpacing: 4, crossAxisSpacing: 4),
      itemCount: 12,
      itemBuilder: (_, m) {
        final on = (mask >> m) & 1 == 1;
        return GestureDetector(
          onTap: () => onChanged(on ? mask & ~(1 << m) : mask | (1 << m)),
          child: Container(
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: on ? Theme.of(context).colorScheme.primary : Colors.transparent,
              borderRadius: BorderRadius.circular(4),
              border: Border.all(color: Theme.of(context).dividerColor),
            ),
            child: Text(_months[m], style: TextStyle(
              fontSize: 10,
              color: on ? Colors.white : Theme.of(context).textTheme.bodySmall?.color,
            )),
          ),
        );
      },
    );
  }
}
