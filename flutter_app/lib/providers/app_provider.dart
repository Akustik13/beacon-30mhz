import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../protocol/selftest.dart';

class AppProvider extends ChangeNotifier {
  int       _tabIndex          = 0;
  bool      _filterBeaconsOnly = false;
  ThemeMode _themeMode         = ThemeMode.dark;
  int       _layoutMode        = 0; // 0=Focused, 1=Fleet
  bool      _autoConnect       = true;
  bool      _continuousScan    = false;
  bool      _autoDisconnect    = true;

  List<String> selftestFailures = [];

  int       get tabIndex          => _tabIndex;
  bool      get filterBeaconsOnly => _filterBeaconsOnly;
  ThemeMode get themeMode         => _themeMode;
  int       get layoutMode        => _layoutMode;
  bool      get autoConnect       => _autoConnect;
  bool      get continuousScan    => _continuousScan;
  bool      get autoDisconnect    => _autoDisconnect;
  bool      get selftestOk        => selftestFailures.isEmpty;

  Future<void> init() async {
    selftestFailures = runSelftest();
    if (selftestFailures.isNotEmpty) {
      assert(false, 'Protocol selftest FAILED:\n${selftestFailures.join('\n')}');
    }

    final prefs = await SharedPreferences.getInstance();
    _filterBeaconsOnly = prefs.getBool('filter_beacons_only') ?? false;
    _layoutMode        = prefs.getInt('layout_mode')          ?? 0;
    final darkMode     = prefs.getBool('dark_mode')           ?? true;
    _themeMode         = darkMode ? ThemeMode.dark : ThemeMode.light;
    _autoConnect       = prefs.getBool('auto_connect')        ?? true;
    _continuousScan    = prefs.getBool('continuous_scan')     ?? false;
    _autoDisconnect    = prefs.getBool('auto_disconnect')     ?? true;
    notifyListeners();
  }

  void setTabIndex(int i) {
    _tabIndex = i;
    notifyListeners();
  }

  Future<void> setFilterBeaconsOnly(bool v) async {
    _filterBeaconsOnly = v;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool('filter_beacons_only', v);
    notifyListeners();
  }

  Future<void> setThemeMode(ThemeMode mode) async {
    _themeMode = mode;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool('dark_mode', mode == ThemeMode.dark);
    notifyListeners();
  }

  Future<void> setLayoutMode(int mode) async {
    _layoutMode = mode;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setInt('layout_mode', mode);
    notifyListeners();
  }

  Future<void> setAutoConnect(bool v) async {
    _autoConnect = v;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool('auto_connect', v);
    notifyListeners();
  }

  Future<void> setContinuousScan(bool v) async {
    _continuousScan = v;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool('continuous_scan', v);
    notifyListeners();
  }

  Future<void> setAutoDisconnect(bool v) async {
    _autoDisconnect = v;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool('auto_disconnect', v);
    notifyListeners();
  }
}
