import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import '../protocol/config_blob.dart';

class BeaconTab extends StatefulWidget {
  const BeaconTab({super.key});
  @override
  State<BeaconTab> createState() => _BeaconTabState();
}

class _BeaconTabState extends State<BeaconTab> {
  // Local form state — populated from ConfigBlob when connected
  int  _rfMode      = 0;
  int  _rfChannel   = 0;
  int  _rfPower     = 0;
  int  _rfPulseMs   = 50;
  int  _rfPeriodMs  = 5000;
  int  _schedEn     = 0;
  int  _schedHours  = 0x00FFFF00; // 8:00–23:59 default
  int  _schedDays   = 0x7F;       // all days
  int  _schedMonths = 0x0FFF;     // all months
  bool _dirty = false;
  bool _loaded = false;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final cfg = context.watch<BeaconProvider>().config;
    if (cfg != null && !_loaded) {
      _loadFromConfig(cfg);
      _loaded = true;
    }
    // Reset loaded flag when disconnected
    if (!context.watch<BleProvider>().isConnected) _loaded = false;
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

  ConfigBlob _buildConfig(ConfigBlob base) => ConfigBlob(
    protoVer: base.protoVer,
    rfMode:     _rfMode,
    rfChannel:  _rfChannel,
    rfPower:    _rfPower,
    rfPulseMs:  _rfPulseMs,
    rfPeriodMs: _rfPeriodMs,
    tempIvS:    base.tempIvS,
    lightIvS:   base.lightIvS,
    batIvS:     base.batIvS,
    tempOffset01c: base.tempOffset01c,
    schedEn:    _schedEn,
    schedHours: _schedHours,
    schedDays:  _schedDays,
    schedMonths: _schedMonths,
    uptimeSaveMin: base.uptimeSaveMin,
    logMask:    base.logMask,
  );

  Future<void> _apply() async {
    final beacon = context.read<BeaconProvider>();
    final base   = beacon.config;
    if (base == null) return;
    final newCfg = _buildConfig(base);
    final ok     = await beacon.writeConfig(newCfg);
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text(ok ? 'Config applied' : beacon.lastError ?? 'Failed'),
        backgroundColor: ok ? Colors.green : Colors.red,
      ));
      if (ok) setState(() => _dirty = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final ble    = context.watch<BleProvider>();
    final beacon = context.watch<BeaconProvider>();

    if (!ble.isConnected) {
      return const Scaffold(
        body: Center(child: Text('Connect to a beacon first')));
    }

    if (beacon.config == null) {
      return const Scaffold(
        body: Center(child: CircularProgressIndicator()));
    }

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
        child: Column(children: [
          _section('Transmitter', [
            _labelDropdown('Mode', _rfMode, [
              const DropdownMenuItem(value: 0, child: Text('Off')),
              const DropdownMenuItem(value: 1, child: Text('Pulse')),
              const DropdownMenuItem(value: 2, child: Text('Continuous')),
              const DropdownMenuItem(value: 3, child: Text('Eco')),
            ], (v) => setState(() { _rfMode = v!; _dirty = true; })),
            _labelDropdown('Channel', _rfChannel, [
              const DropdownMenuItem(value: 0, child: Text('CH1')),
              const DropdownMenuItem(value: 1, child: Text('CH2')),
              const DropdownMenuItem(value: 2, child: Text('Both')),
            ], (v) => setState(() { _rfChannel = v!; _dirty = true; })),
            _labelDropdown('Power', _rfPower, [
              const DropdownMenuItem(value: 0, child: Text('Low')),
              const DropdownMenuItem(value: 1, child: Text('Mid')),
              const DropdownMenuItem(value: 2, child: Text('High')),
            ], (v) => setState(() { _rfPower = v!; _dirty = true; })),
            _intSlider('Pulse Duration (ms)', _rfPulseMs, 10, 500,
                (v) => setState(() { _rfPulseMs = v; _dirty = true; })),
            _intSlider('Period (ms)', _rfPeriodMs, 500, 30000,
                (v) => setState(() { _rfPeriodMs = v; _dirty = true; })),
          ]),
          const SizedBox(height: 8),
          _section('Schedule', [
            SwitchListTile(
              title: const Text('Enable Schedule'),
              value: _schedEn != 0,
              onChanged: (v) => setState(() { _schedEn = v ? 1 : 0; _dirty = true; }),
              contentPadding: EdgeInsets.zero,
            ),
            if (_schedEn != 0) ...[
              const SizedBox(height: 8),
              const Text('Active Hours', style: TextStyle(fontWeight: FontWeight.w600)),
              const SizedBox(height: 6),
              _HourGrid(mask: _schedHours,
                  onChanged: (m) => setState(() { _schedHours = m; _dirty = true; })),
              const SizedBox(height: 12),
              const Text('Active Days', style: TextStyle(fontWeight: FontWeight.w600)),
              const SizedBox(height: 6),
              _DayPicker(mask: _schedDays,
                  onChanged: (m) => setState(() { _schedDays = m; _dirty = true; })),
              const SizedBox(height: 12),
              const Text('Active Months', style: TextStyle(fontWeight: FontWeight.w600)),
              const SizedBox(height: 6),
              _MonthPicker(mask: _schedMonths,
                  onChanged: (m) => setState(() { _schedMonths = m; _dirty = true; })),
            ],
          ]),
          const SizedBox(height: 16),
          FilledButton.icon(
            onPressed: (!beacon.isBusy && _dirty) ? _apply : null,
            icon: const Icon(Icons.check),
            label: Text(_dirty ? 'Apply' : 'No changes'),
          ),
          const SizedBox(height: 8),
          OutlinedButton.icon(
            onPressed: beacon.config != null ? () {
              _loadFromConfig(beacon.config!);
            } : null,
            icon: const Icon(Icons.refresh),
            label: const Text('Revert'),
          ),
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
        SizedBox(width: 120, child: Text(label)),
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

  Widget _intSlider(String label, int value, int min, int max,
      ValueChanged<int> onChanged) =>
    Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(children: [
          Text(label), const Spacer(),
          Text(value.toString(), style: const TextStyle(fontWeight: FontWeight.bold)),
        ]),
        Slider(
          value: value.toDouble().clamp(min.toDouble(), max.toDouble()),
          min: min.toDouble(), max: max.toDouble(),
          divisions: (max - min).clamp(1, 200),
          onChanged: (v) => onChanged(v.round()),
        ),
      ],
    );
}

// ── Hour grid ──────────────────────────────────────────────────────────────

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
            child: Text('$h',
              style: TextStyle(
                fontSize: 11,
                color: on ? Colors.white : Theme.of(context).textTheme.bodySmall?.color,
              )),
          ),
        );
      },
    );
  }
}

// ── Day picker ─────────────────────────────────────────────────────────────

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
            child: Text(_days[i],
              style: TextStyle(
                fontSize: 11,
                color: on ? Colors.white : Theme.of(context).textTheme.bodySmall?.color,
              )),
          ),
        );
      }),
    );
  }
}

// ── Month picker ───────────────────────────────────────────────────────────

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
            child: Text(_months[m],
              style: TextStyle(
                fontSize: 10,
                color: on ? Colors.white : Theme.of(context).textTheme.bodySmall?.color,
              )),
          ),
        );
      },
    );
  }
}
