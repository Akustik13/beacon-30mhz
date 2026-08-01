import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/app_provider.dart';
import '../providers/ble_provider.dart';
import '../providers/beacon_provider.dart';
import 'home_tab.dart';
import 'beacon_tab.dart';
import 'logging_tab.dart';
import 'data_tab.dart';
import 'devices_tab.dart';

class MainScreen extends StatelessWidget {
  const MainScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final app = context.watch<AppProvider>();
    final ble = context.watch<BleProvider>();
    final beacon = context.read<BeaconProvider>();

    // Wire BLE transport to BeaconProvider when connection state changes
    final t = ble.transport;
    if (ble.isConnected && t != null && beacon.status == null && !beacon.isRefreshing) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        beacon.attach(t);
        beacon.refreshAll();
      });
    } else if (!ble.isConnected && (beacon.status != null || beacon.config != null)) {
      WidgetsBinding.instance.addPostFrameCallback((_) => beacon.detach());
    }

    const bodies = [HomeTab(), BeaconTab(), LoggingTab(), DataTab(), DevicesTab()];

    return Scaffold(
      body: IndexedStack(index: app.tabIndex, children: bodies),
      bottomNavigationBar: NavigationBar(
        selectedIndex: app.tabIndex,
        onDestinationSelected: app.setTabIndex,
        labelBehavior: NavigationDestinationLabelBehavior.onlyShowSelected,
        destinations: const [
          NavigationDestination(
            icon: Icon(Icons.home_outlined), selectedIcon: Icon(Icons.home), label: 'Home'),
          NavigationDestination(
            icon: Icon(Icons.radio_outlined), selectedIcon: Icon(Icons.radio), label: 'Beacon'),
          NavigationDestination(
            icon: Icon(Icons.storage_outlined), selectedIcon: Icon(Icons.storage), label: 'Logging'),
          NavigationDestination(
            icon: Icon(Icons.show_chart_outlined), selectedIcon: Icon(Icons.show_chart), label: 'Data'),
          NavigationDestination(
            icon: Icon(Icons.bluetooth_outlined), selectedIcon: Icon(Icons.bluetooth_connected), label: 'Devices'),
        ],
      ),
    );
  }
}
