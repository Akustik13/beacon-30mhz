import 'dart:convert';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/beacon_device.dart';

class StorageService {
  static final StorageService _instance = StorageService._();
  factory StorageService() => _instance;
  StorageService._();

  static const _keyDevices = 'beacon_devices';
  static const _keyDemoMode = 'demo_mode';
  static const _keyFilterBeacons = 'filter_beacons_only';
  static const _keyLastMac = 'last_connected_mac';
  static const _keyBattMinMv = 'batt_min_mv';
  static const _keyBattMaxMv = 'batt_max_mv';

  Future<List<BeaconDevice>> loadDevices() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_keyDevices);
    if (raw == null) return [];
    try {
      final list = jsonDecode(raw) as List;
      return list.map((e) => BeaconDevice.fromJson(e as Map<String, dynamic>)).toList();
    } catch (_) { return []; }
  }

  Future<void> saveDevices(List<BeaconDevice> devices) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_keyDevices, jsonEncode(devices.map((d) => d.toJson()).toList()));
  }

  Future<bool> getDemoMode() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getBool(_keyDemoMode) ?? false;
  }

  Future<void> setDemoMode(bool value) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool(_keyDemoMode, value);
  }

  Future<bool> getFilterBeaconsOnly() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getBool(_keyFilterBeacons) ?? false;
  }

  Future<void> setFilterBeaconsOnly(bool value) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool(_keyFilterBeacons, value);
  }

  Future<String?> getLastMac() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getString(_keyLastMac);
  }

  Future<void> saveLastMac(String mac) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_keyLastMac, mac);
  }

  Future<int> getBattMinMv() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getInt(_keyBattMinMv) ?? 1000;
  }

  Future<int> getBattMaxMv() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getInt(_keyBattMaxMv) ?? 3300;
  }

  Future<void> setBattMinMv(int value) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setInt(_keyBattMinMv, value);
  }

  Future<void> setBattMaxMv(int value) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setInt(_keyBattMaxMv, value);
  }
}
