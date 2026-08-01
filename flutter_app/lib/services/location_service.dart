import 'package:geolocator/geolocator.dart';

class LocationData {
  final double lat;
  final double lon;
  final double accuracy;
  final String? address;
  final DateTime timestamp;

  const LocationData({
    required this.lat,
    required this.lon,
    required this.accuracy,
    this.address,
    required this.timestamp,
  });
}

class LocationService {
  static final LocationService _instance = LocationService._();
  factory LocationService() => _instance;
  LocationService._();

  LocationData? _last;
  LocationData? get last => _last;

  Future<bool> requestPermission() async {
    bool serviceEnabled = await Geolocator.isLocationServiceEnabled();
    if (!serviceEnabled) return false;

    LocationPermission perm = await Geolocator.checkPermission();
    if (perm == LocationPermission.denied) {
      perm = await Geolocator.requestPermission();
    }
    return perm == LocationPermission.whileInUse || perm == LocationPermission.always;
  }

  Future<LocationData?> getCurrentLocation() async {
    final hasPermission = await requestPermission();
    if (!hasPermission) return null;
    try {
      final pos = await Geolocator.getCurrentPosition(
        desiredAccuracy: LocationAccuracy.high,
      ).timeout(const Duration(seconds: 15));
      _last = LocationData(
        lat: pos.latitude,
        lon: pos.longitude,
        accuracy: pos.accuracy,
        timestamp: DateTime.now(),
      );
      return _last;
    } catch (_) {
      return null;
    }
  }

  Future<LocationData?> getLocationWithAddress() async {
    final loc = await getCurrentLocation();
    if (loc == null) return null;
    try {
      return loc;
    } catch (_) {
      return loc;
    }
  }
}
