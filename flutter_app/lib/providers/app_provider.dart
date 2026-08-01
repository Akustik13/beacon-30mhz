import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../protocol/selftest.dart';

class AppProvider extends ChangeNotifier {
  int  _tabIndex = 0;
  bool _filterBeaconsOnly = false;
  List<String> selftestFailures = [];

  int  get tabIndex           => _tabIndex;
  bool get filterBeaconsOnly  => _filterBeaconsOnly;
  bool get selftestOk         => selftestFailures.isEmpty;

  Future<void> init() async {
    // Protocol selftest — runs once at startup
    selftestFailures = runSelftest();
    if (selftestFailures.isNotEmpty) {
      assert(false, 'Protocol selftest FAILED:\n${selftestFailures.join('\n')}');
    }

    final prefs = await SharedPreferences.getInstance();
    _filterBeaconsOnly = prefs.getBool('filter_beacons_only') ?? false;
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
}
