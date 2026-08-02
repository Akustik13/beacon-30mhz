import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import 'package:permission_handler/permission_handler.dart';
import 'app_theme.dart';
import 'providers/app_provider.dart';
import 'providers/ble_provider.dart';
import 'providers/beacon_provider.dart';
import 'providers/devices_provider.dart';
import 'screens/main_screen.dart';
import 'services/update_service.dart';
import 'screens/widgets/update_dialog.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
  SystemChrome.setSystemUIOverlayStyle(const SystemUiOverlayStyle(
    systemNavigationBarColor: Colors.transparent,
    systemNavigationBarDividerColor: Colors.transparent,
  ));
  runApp(
    MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (_) => AppProvider()),
        ChangeNotifierProvider(create: (_) => BleProvider()),
        ChangeNotifierProvider(create: (_) => BeaconProvider()),
        ChangeNotifierProvider(create: (_) => DevicesProvider()),
      ],
      child: const BeaconApp(),
    ),
  );
}

class BeaconApp extends StatelessWidget {
  const BeaconApp({super.key});

  @override
  Widget build(BuildContext context) {
    final themeMode = context.watch<AppProvider>().themeMode;
    return MaterialApp(
      title: 'Beacon Manager',
      theme:      buildLightTheme(),
      darkTheme:  buildAppTheme(),
      themeMode:  themeMode,
      debugShowCheckedModeBanner: false,
      home: const _InitWrapper(),
    );
  }
}

class _InitWrapper extends StatefulWidget {
  const _InitWrapper();
  @override
  State<_InitWrapper> createState() => _InitWrapperState();
}

class _InitWrapperState extends State<_InitWrapper> {
  bool   _ready  = false;
  String _status = 'Starting…';

  @override
  void initState() {
    super.initState();
    _init();
  }

  Future<void> _init() async {
    setState(() => _status = 'Requesting permissions…');
    await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.locationWhenInUse,
    ].request();

    setState(() => _status = 'Initializing…');
    await context.read<AppProvider>().init();
    await context.read<DevicesProvider>().load();

    if (mounted) {
      setState(() => _ready = true);
      _scheduleAutoUpdateCheck();
    }
  }

  Future<void> _scheduleAutoUpdateCheck() async {
    final svc = UpdateService();
    if (!await svc.shouldAutoCheck()) return;
    await Future.delayed(const Duration(seconds: 3));
    if (!mounted) return;
    final release = await svc.checkForUpdate();
    if (release == null || !mounted) return;
    final current = await svc.currentVersion();
    if (!mounted) return;
    await UpdateDialog.show(context,
        currentVersion: current, release: release, service: svc);
  }

  @override
  Widget build(BuildContext context) {
    if (_ready) return const MainScreen();

    final selftestFail = context.watch<AppProvider>().selftestFailures;

    return Scaffold(
      body: Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(Icons.radio, size: 72,
                  color: Theme.of(context).colorScheme.primary),
              const SizedBox(height: 20),
              const Text('Beacon Manager',
                  style: TextStyle(fontSize: 24, fontWeight: FontWeight.bold)),
              const SizedBox(height: 32),
              if (selftestFail.isNotEmpty)
                Container(
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: Colors.red.withValues(alpha: 0.1),
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: Colors.red.withValues(alpha: 0.5)),
                  ),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text('Protocol selftest FAILED',
                          style: TextStyle(color: Colors.red, fontWeight: FontWeight.bold)),
                      ...selftestFail.map((f) => Text(f,
                          style: const TextStyle(color: Colors.red, fontSize: 12))),
                    ],
                  ),
                )
              else
                const CircularProgressIndicator(),
              const SizedBox(height: 16),
              Text(_status, style: Theme.of(context).textTheme.bodySmall),
            ],
          ),
        ),
      ),
    );
  }
}
