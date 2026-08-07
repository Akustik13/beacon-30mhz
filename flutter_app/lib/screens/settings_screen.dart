import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/app_provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import '../services/update_service.dart';
import '../models/app_release.dart';
import 'widgets/update_dialog.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});
  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  final _updateService = UpdateService();
  bool _autoCheck = true;
  bool _checking = false;
  DateTime? _lastCheck;
  String? _checkResult;

  @override
  void initState() {
    super.initState();
    _loadUpdatePrefs();
  }

  Future<void> _loadUpdatePrefs() async {
    final auto = await _updateService.getAutoCheckEnabled();
    final last = await _updateService.lastCheckTime();
    if (mounted) setState(() { _autoCheck = auto; _lastCheck = last; });
  }

  Future<void> _checkNow() async {
    setState(() { _checking = true; _checkResult = null; });
    final release = await _updateService.checkForUpdate(ignoreSkipped: true);
    if (!mounted) return;
    setState(() { _checking = false; _lastCheck = DateTime.now(); });
    if (release == null) {
      setState(() => _checkResult = "You're up to date");
      return;
    }
    final current = await _updateService.currentVersion();
    if (!mounted) return;
    await UpdateDialog.show(context,
        currentVersion: current, release: release, service: _updateService);
  }

  String _relativeTime(DateTime t) {
    final diff = DateTime.now().difference(t);
    if (diff.inMinutes < 1) return 'just now';
    if (diff.inHours < 1) return '${diff.inMinutes} min ago';
    if (diff.inDays < 1) return '${diff.inHours} hours ago';
    return '${diff.inDays} days ago';
  }

  @override
  Widget build(BuildContext context) {
    final app    = context.watch<AppProvider>();
    final ble    = context.watch<BleProvider>();
    final beacon = context.watch<BeaconProvider>();

    return Scaffold(
      appBar: AppBar(title: const Text('Settings')),
      body: ListView(
        padding: const EdgeInsets.all(12),
        children: [

          // ── Display ────────────────────────────────────────────────────
          _SectionHeader('Display'),
          Card(
            child: Column(children: [
              SwitchListTile(
                title: const Text('Dark mode'),
                subtitle: const Text('Toggle light / dark theme'),
                secondary: Icon(
                  app.themeMode == ThemeMode.dark
                      ? Icons.dark_mode : Icons.light_mode,
                ),
                value: app.themeMode == ThemeMode.dark,
                onChanged: (v) => app.setThemeMode(
                    v ? ThemeMode.dark : ThemeMode.light),
              ),
            ]),
          ),
          const SizedBox(height: 8),

          // ── Home layout ────────────────────────────────────────────────
          _SectionHeader('Home layout'),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(14),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  _LayoutOption(
                    index: 0,
                    selected: app.layoutMode == 0,
                    icon: Icons.center_focus_strong_outlined,
                    title: 'Focused control',
                    subtitle: 'Full-detail view for one beacon at a time. '
                        'Best for field work with a single implant.',
                    onTap: () => app.setLayoutMode(0),
                  ),
                  const Divider(height: 16),
                  _LayoutOption(
                    index: 1,
                    selected: app.layoutMode == 1,
                    icon: Icons.grid_view_outlined,
                    title: 'Fleet dashboard',
                    subtitle: 'Compact card grid for monitoring multiple '
                        'beacons simultaneously. Best for lab review.',
                    onTap: () => app.setLayoutMode(1),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 8),

          // ── Scan / connect ─────────────────────────────────────────────
          _SectionHeader('BLE scanning'),
          Card(
            child: Column(children: [
              SwitchListTile(
                title: const Text('Show beacons only'),
                subtitle: const Text('Hide non-beacon devices in scan results'),
                secondary: const Icon(Icons.filter_alt_outlined),
                value: app.filterBeaconsOnly,
                onChanged: app.setFilterBeaconsOnly,
              ),
              const Divider(height: 1),
              SwitchListTile(
                title: const Text('Scan continuously'),
                subtitle: const Text('Continuously scan and auto-connect to known beacons'),
                secondary: const Icon(Icons.radar),
                value: app.continuousScan,
                onChanged: app.setContinuousScan,
              ),
              const Divider(height: 1),
              SwitchListTile(
                title: const Text('Auto-connect to known beacons'),
                subtitle: const Text('Connect automatically when a saved beacon is nearby'),
                secondary: const Icon(Icons.link),
                value: app.autoConnect,
                onChanged: app.setAutoConnect,
              ),
            ]),
          ),
          const SizedBox(height: 8),

          // ── Connection ────────────────────────────────────────────────
          _SectionHeader('Connection'),
          Card(
            child: Column(children: [
              SwitchListTile(
                title: const Text('Auto-disconnect after 1 min'),
                subtitle: const Text(
                    'If no BLE activity for 60 s, shows a 10 s countdown '
                    'then disconnects automatically'),
                secondary: const Icon(Icons.timer_off_outlined),
                value: app.autoDisconnect,
                onChanged: (v) async {
                  await app.setAutoDisconnect(v);
                  // Also apply to currently running BleProvider timer
                  await context.read<BleProvider>().setAutoDisconnectEnabled(v);
                },
              ),
            ]),
          ),
          const SizedBox(height: 8),

          // ── BLE settings shortcut ──────────────────────────────────────
          _SectionHeader('BLE beacon settings'),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(14),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(children: [
                    Icon(Icons.settings_bluetooth,
                        color: Theme.of(context).colorScheme.primary),
                    const SizedBox(width: 12),
                    Expanded(child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Text('Op mode, TX power, adv interval…',
                            style: TextStyle(fontWeight: FontWeight.w600)),
                        Text(
                          ble.isConnected
                              ? 'Connected to ${ble.connectedName ?? ble.connectedMac}'
                              : 'Connect a beacon to edit BLE settings',
                          style: Theme.of(context).textTheme.bodySmall,
                        ),
                      ],
                    )),
                  ]),
                  const SizedBox(height: 10),
                  OutlinedButton.icon(
                    onPressed: () => app.setTabIndex(4),
                    icon: const Icon(Icons.open_in_new, size: 16),
                    label: const Text('Open in Devices tab'),
                    style: OutlinedButton.styleFrom(
                      minimumSize: const Size(double.infinity, 44)),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 8),

          // ── About ──────────────────────────────────────────────────────
          _SectionHeader('About'),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(14),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text('Beacon Manager',
                      style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
                  const Text('v3.0  ·  Sevskiy GmbH, Munich',
                      style: TextStyle(fontSize: 12, color: Colors.grey)),
                  const SizedBox(height: 10),
                  if (beacon.fwVersion != null) ...[
                    _InfoRow('Firmware', beacon.fwVersion!),
                    const SizedBox(height: 4),
                  ],
                  if (beacon.info != null) ...[
                    _InfoRow('Device UID', beacon.info!.uidHex),
                    _InfoRow('Log capacity',
                        '${beacon.info!.logTotalEntries} records'),
                    const SizedBox(height: 4),
                  ],
                  _InfoRow('Protocol selftest',
                      context.read<AppProvider>().selftestOk ? '✓ PASS' : '✗ FAIL',
                      valueColor: context.read<AppProvider>().selftestOk
                          ? Colors.green : Colors.red),
                  if (!context.read<AppProvider>().selftestOk)
                    ...context.read<AppProvider>().selftestFailures
                        .map((f) => Padding(
                            padding: const EdgeInsets.only(top: 2, left: 16),
                            child: Text(f,
                                style: const TextStyle(
                                    fontSize: 11, color: Colors.red)))),
                ],
              ),
            ),
          ),
          // ── App Updates ────────────────────────────────────────────────
          _SectionHeader('App updates'),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(14),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  FutureBuilder<String>(
                    future: _updateService.currentVersion(),
                    builder: (ctx, snap) => _InfoRow(
                      'Current version',
                      snap.hasData ? snap.data! : '…',
                    ),
                  ),
                  if (_lastCheck != null) ...[
                    const SizedBox(height: 4),
                    _InfoRow('Last checked', _relativeTime(_lastCheck!)),
                  ],
                  const SizedBox(height: 10),
                  if (_checkResult != null) ...[
                    Text(_checkResult!,
                        style: TextStyle(
                          fontSize: 12,
                          color: Theme.of(context).colorScheme.primary,
                        )),
                    const SizedBox(height: 8),
                  ],
                  SizedBox(
                    width: double.infinity,
                    child: FilledButton.icon(
                      onPressed: _checking ? null : _checkNow,
                      icon: _checking
                          ? const SizedBox(
                              width: 16, height: 16,
                              child: CircularProgressIndicator(strokeWidth: 2))
                          : const Icon(Icons.system_update, size: 18),
                      label: Text(_checking ? 'Checking…' : 'Check for updates'),
                      style: FilledButton.styleFrom(
                          minimumSize: const Size(double.infinity, 44)),
                    ),
                  ),
                  const SizedBox(height: 4),
                  SwitchListTile(
                    contentPadding: EdgeInsets.zero,
                    title: const Text('Check automatically once a day',
                        style: TextStyle(fontSize: 13)),
                    value: _autoCheck,
                    onChanged: (v) async {
                      await _updateService.setAutoCheckEnabled(v);
                      if (mounted) setState(() => _autoCheck = v);
                    },
                  ),
                ],
              ),
            ),
          ),
          SizedBox(height: MediaQuery.of(context).padding.bottom + 96),
        ],
      ),
    );
  }
}

class _SectionHeader extends StatelessWidget {
  final String text;
  const _SectionHeader(this.text);
  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 6, left: 4, top: 4),
    child: Text(text.toUpperCase(),
        style: TextStyle(
          fontSize: 11,
          fontWeight: FontWeight.bold,
          letterSpacing: 1.0,
          color: Theme.of(context).colorScheme.primary,
        )),
  );
}

class _LayoutOption extends StatelessWidget {
  final int     index;
  final bool    selected;
  final IconData icon;
  final String  title;
  final String  subtitle;
  final VoidCallback onTap;
  const _LayoutOption({
    required this.index, required this.selected, required this.icon,
    required this.title, required this.subtitle, required this.onTap,
  });

  @override
  Widget build(BuildContext context) => InkWell(
    onTap: onTap,
    borderRadius: BorderRadius.circular(8),
    child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Radio<int>(
        value: index,
        groupValue: selected ? index : -1,
        onChanged: (_) => onTap(),
        visualDensity: VisualDensity.compact,
      ),
      const SizedBox(width: 6),
      Icon(icon, size: 20, color: selected
          ? Theme.of(context).colorScheme.primary : Colors.grey),
      const SizedBox(width: 10),
      Expanded(child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(title,
              style: TextStyle(
                fontWeight: FontWeight.w600,
                color: selected
                    ? Theme.of(context).colorScheme.primary : null,
              )),
          const SizedBox(height: 2),
          Text(subtitle, style: Theme.of(context).textTheme.bodySmall),
        ],
      )),
    ]),
  );
}

class _InfoRow extends StatelessWidget {
  final String label;
  final String value;
  final Color? valueColor;
  const _InfoRow(this.label, this.value, {this.valueColor});
  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 2),
    child: Row(children: [
      Text(label, style: Theme.of(context).textTheme.bodySmall),
      const Spacer(),
      Text(value,
          style: TextStyle(
            fontSize: 12,
            fontWeight: FontWeight.w500,
            color: valueColor,
          )),
    ]),
  );
}
