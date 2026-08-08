import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../providers/app_provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import '../providers/devices_provider.dart';
import 'home_tab.dart';
import 'beacon_tab.dart';
import 'logging_tab.dart';
import 'data_tab.dart';
import 'devices_tab.dart';
import 'settings_screen.dart';
import 'fleet_home_tab.dart';
import 'events_tab.dart';

class MainScreen extends StatefulWidget {
  const MainScreen({super.key});
  @override
  State<MainScreen> createState() => _MainScreenState();
}

class _MainScreenState extends State<MainScreen> {
  bool   _continuousScanRunning  = false;
  bool   _autoConnectInProgress  = false;
  bool   _disconnectDialogShown  = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _initialScan());
  }

  @override
  void dispose() {
    _continuousScanRunning = false;
    super.dispose();
  }

  Future<void> _initialScan() async {
    if (!mounted) return;
    final ble  = context.read<BleProvider>();
    final app  = context.read<AppProvider>();

    // Register callback so every scan refreshes known-device MACs for auto-connect
    ble.onBeforeScan = () {
      if (!mounted) return;
      final d = context.read<DevicesProvider>();
      if (d.devices.isNotEmpty) {
        ble.setAutoConnectMacs(d.devices.map((dev) => dev.mac).toSet());
      }
    };

    if (ble.isConnected || ble.isScanning) {
      _setupContinuousScan();
      return;
    }
    await ble.startScan(timeoutSec: 8); // onBeforeScan sets MACs internally
    if (!mounted) return;
    await _tryAutoConnect(); // fallback
    _setupContinuousScan();
  }

  void _setupContinuousScan() {
    if (!mounted) return;
    final app = context.read<AppProvider>();
    if (!app.continuousScan) return;
    if (_continuousScanRunning) return;
    _continuousScanRunning = true;
    _runContinuousScanLoop();
  }

  // Tight scan loop — runs until continuousScan is off or widget is gone.
  // Zero pause between scans: if nothing found → immediately rescan.
  // If found → auto-connect fires inside startScan; loop waits 2 s then checks.
  Future<void> _runContinuousScanLoop() async {
    while (_continuousScanRunning && mounted) {
      final app = context.read<AppProvider>();
      final ble = context.read<BleProvider>();
      if (!app.continuousScan) break;
      if (ble.isConnected || ble.isConnecting) {
        await Future.delayed(const Duration(seconds: 2));
        continue;
      }
      if (!ble.isScanning) {
        await ble.startScan(timeoutSec: 5); // onBeforeScan sets MACs
      }
      // tiny yield so UI stays responsive between scans
      await Future.delayed(const Duration(milliseconds: 100));
    }
    _continuousScanRunning = false;
  }

  Future<void> _tryAutoConnect() async {
    if (!mounted) return;
    final app  = context.read<AppProvider>();
    final ble  = context.read<BleProvider>();
    final devs = context.read<DevicesProvider>();
    if (!app.autoConnect || ble.isConnected || _autoConnectInProgress) return;

    // Find the most recently seen saved device in current scan results
    final savedMacs = {for (final d in devs.devices) d.mac: d};
    // Sort by last-seen descending so we pick the most recently used
    final candidates = ble.scanResults
        .where((r) => savedMacs.containsKey(r.mac))
        .toList()
      ..sort((a, b) {
        final da = savedMacs[a.mac]?.lastSeen;
        final db = savedMacs[b.mac]?.lastSeen;
        if (da == null && db == null) return 0;
        if (da == null) return 1;
        if (db == null) return -1;
        return db.compareTo(da);
      });

    if (candidates.isEmpty) return;

    _autoConnectInProgress = true;
    final best = candidates.first;
    final ok = await ble.connect(best.device, best.name);
    _autoConnectInProgress = false;
    if (ok) {
      await devs.touchDevice(best.mac, best.name, rssi: best.rssi);
    }
  }

  @override
  Widget build(BuildContext context) {
    final app    = context.watch<AppProvider>();
    final ble    = context.watch<BleProvider>();
    final beacon = context.read<BeaconProvider>();
    final devs   = context.read<DevicesProvider>();

    // Wire BLE transport to BeaconProvider on connect/disconnect
    final t = ble.transport;
    if (ble.isConnected && t != null && beacon.status == null && !beacon.isRefreshing) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        beacon.attach(t);
        beacon.refreshAll();
        devs.touchDevice(
            ble.connectedMac, ble.connectedName ?? '', rssi: ble.rssi);
      });
    } else if (!ble.isConnected && (beacon.status != null || beacon.config != null)) {
      WidgetsBinding.instance.addPostFrameCallback((_) => beacon.detach());
    }

    // Stop continuous scan loop when setting is disabled
    if (!app.continuousScan && _continuousScanRunning) {
      _continuousScanRunning = false;
    }

    // Auto-disconnect warning dialog
    if (ble.disconnectWarning && !_disconnectDialogShown) {
      _disconnectDialogShown = true;
      WidgetsBinding.instance.addPostFrameCallback((_) async {
        if (!mounted) return;
        await showDialog(
          context: context,
          barrierDismissible: false,
          builder: (_) => const _DisconnectWarningDialog(),
        );
        if (mounted) _disconnectDialogShown = false;
      });
    }
    if (!ble.disconnectWarning) _disconnectDialogShown = false;

    // Fleet dashboard mode: 3-tab bottom nav (Fleet · Devices · Settings)
    // Full 7-item IndexedStack preserved so detail screens work unchanged.
    if (app.layoutMode == 1) {
      final fleetNavIdx = app.tabIndex == 5 ? 1 : app.tabIndex == 6 ? 2 : 0;
      return Scaffold(
        extendBody: true,
        body: IndexedStack(
          index: app.tabIndex.clamp(0, 6),
          children: const [
            FleetHomeTab(), BeaconTab(), LoggingTab(), DataTab(),
            EventsTab(), DevicesTab(), SettingsScreen(),
          ],
        ),
        bottomNavigationBar: NavigationBar(
          selectedIndex: fleetNavIdx,
          onDestinationSelected: (i) {
            HapticFeedback.lightImpact();
            app.setTabIndex(i == 1 ? 5 : i == 2 ? 6 : 0);
          },
          labelBehavior: NavigationDestinationLabelBehavior.onlyShowSelected,
          destinations: const [
            NavigationDestination(
              icon: Icon(Icons.grid_view_outlined),
              selectedIcon: Icon(Icons.grid_view), label: 'Fleet'),
            NavigationDestination(
              icon: Icon(Icons.bluetooth_outlined),
              selectedIcon: Icon(Icons.bluetooth_connected), label: 'Devices'),
            NavigationDestination(
              icon: Icon(Icons.settings_outlined),
              selectedIcon: Icon(Icons.settings), label: 'Settings'),
          ],
        ),
      );
    }

    // Focused mode (default)
    const bodies = [
      HomeTab(), BeaconTab(), LoggingTab(), DataTab(),
      EventsTab(), DevicesTab(), SettingsScreen(),
    ];

    return Scaffold(
      extendBody: true,
      body: IndexedStack(index: app.tabIndex.clamp(0, 6), children: bodies),
      bottomNavigationBar: NavigationBar(
        selectedIndex: app.tabIndex.clamp(0, 6),
        onDestinationSelected: app.setTabIndex,
        labelBehavior: NavigationDestinationLabelBehavior.onlyShowSelected,
        destinations: const [
          NavigationDestination(
            icon: Icon(Icons.home_outlined),
            selectedIcon: Icon(Icons.home), label: 'Home'),
          NavigationDestination(
            icon: Icon(Icons.radio_outlined),
            selectedIcon: Icon(Icons.radio), label: 'Beacon'),
          NavigationDestination(
            icon: Icon(Icons.storage_outlined),
            selectedIcon: Icon(Icons.storage), label: 'Logging'),
          NavigationDestination(
            icon: Icon(Icons.show_chart_outlined),
            selectedIcon: Icon(Icons.show_chart), label: 'Data'),
          NavigationDestination(
            icon: Icon(Icons.bolt_outlined),
            selectedIcon: Icon(Icons.bolt), label: 'Events'),
          NavigationDestination(
            icon: Icon(Icons.bluetooth_outlined),
            selectedIcon: Icon(Icons.bluetooth_connected), label: 'Devices'),
          NavigationDestination(
            icon: Icon(Icons.settings_outlined),
            selectedIcon: Icon(Icons.settings), label: 'Settings'),
        ],
      ),
    );
  }
}

// ── Auto-disconnect warning dialog ────────────────────────────────────────────

class _DisconnectWarningDialog extends StatefulWidget {
  const _DisconnectWarningDialog();
  @override
  State<_DisconnectWarningDialog> createState() => _DisconnectWarningDialogState();
}

class _DisconnectWarningDialogState extends State<_DisconnectWarningDialog> {
  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleProvider>();

    // Auto-close when warning clears or device disconnects
    if (!ble.isConnected || !ble.disconnectWarning) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted && Navigator.canPop(context)) Navigator.pop(context);
      });
    }

    final countdown = ble.inactivityCountdown;
    final urgent    = countdown <= 5;

    return AlertDialog(
      icon: Icon(Icons.timer_off_rounded,
          size: 40, color: urgent ? Colors.red : Colors.orange),
      title: const Text('Auto-disconnect', textAlign: TextAlign.center),
      content: Column(mainAxisSize: MainAxisSize.min, children: [
        const Text(
          'No BLE activity detected.\nDisconnecting in:',
          textAlign: TextAlign.center,
          style: TextStyle(fontSize: 14),
        ),
        const SizedBox(height: 16),
        AnimatedSwitcher(
          duration: const Duration(milliseconds: 200),
          child: Text(
            '$countdown',
            key: ValueKey(countdown),
            textAlign: TextAlign.center,
            style: TextStyle(
              fontSize: 64,
              fontWeight: FontWeight.bold,
              color: urgent ? Colors.red : Colors.orange,
            ),
          ),
        ),
        Text(
          countdown == 1 ? 'second' : 'seconds',
          textAlign: TextAlign.center,
          style: TextStyle(fontSize: 13, color: Colors.grey[500]),
        ),
      ]),
      actionsAlignment: MainAxisAlignment.spaceEvenly,
      actions: [
        OutlinedButton.icon(
          onPressed: () {
            ble.resetInactivity();
            Navigator.pop(context);
          },
          icon: const Icon(Icons.link),
          label: const Text('Stay connected'),
        ),
        FilledButton.icon(
          style: FilledButton.styleFrom(backgroundColor: Colors.red),
          onPressed: () {
            Navigator.pop(context);
            ble.disconnect();
          },
          icon: const Icon(Icons.link_off),
          label: const Text('Disconnect'),
        ),
      ],
    );
  }
}
