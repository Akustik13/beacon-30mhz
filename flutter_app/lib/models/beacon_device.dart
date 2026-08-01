import 'beacon_config.dart';

class BeaconDevice {
  final String id;       // unique_id hex or MAC
  String mac;
  String name;           // e.g. "Rat M001"
  String nickname;       // e.g. "Mykola"
  String icon;           // emoji
  String notes;
  String animalTag;
  DateTime? implantDate;
  String species;
  String sex;
  double weightG;
  BeaconConfig config;
  DateTime? lastSeen;
  double? lastLat;
  double? lastLon;
  int? lastRssi;
  int? lastBatteryMv;
  double? lastTempC;
  int sessionCount;
  String fwVersion;

  BeaconDevice({
    required this.id,
    required this.mac,
    this.name = '',
    this.nickname = '',
    this.icon = '🐭',
    this.notes = '',
    this.animalTag = '',
    this.implantDate,
    this.species = '',
    this.sex = '',
    this.weightG = 0,
    BeaconConfig? config,
    this.lastSeen,
    this.lastLat,
    this.lastLon,
    this.lastRssi,
    this.lastBatteryMv,
    this.lastTempC,
    this.sessionCount = 0,
    this.fwVersion = '',
  }) : config = config ?? BeaconConfig();

  String get displayName => name.isNotEmpty ? name : (mac.length > 8 ? mac.substring(mac.length - 8) : mac);
  String get fullLabel => nickname.isNotEmpty ? '$displayName "$nickname"' : displayName;

  int get batteryPercent {
    final mv = lastBatteryMv ?? 0;
    if (mv == 0) return 0;
    return ((mv - 3000) / (4200 - 3000) * 100).clamp(0, 100).round();
  }

  Map<String, dynamic> toJson() => {
    'id': id, 'mac': mac, 'name': name, 'nickname': nickname,
    'icon': icon, 'notes': notes, 'animalTag': animalTag,
    'implantDate': implantDate?.toIso8601String(),
    'species': species, 'sex': sex, 'weightG': weightG,
    'config': config.toJson(), 'lastSeen': lastSeen?.toIso8601String(),
    'lastLat': lastLat, 'lastLon': lastLon, 'lastRssi': lastRssi,
    'lastBatteryMv': lastBatteryMv, 'lastTempC': lastTempC,
    'sessionCount': sessionCount, 'fwVersion': fwVersion,
  };

  factory BeaconDevice.fromJson(Map<String, dynamic> j) => BeaconDevice(
    id: j['id'] ?? '',
    mac: j['mac'] ?? '',
    name: j['name'] ?? '',
    nickname: j['nickname'] ?? '',
    icon: j['icon'] ?? '🐭',
    notes: j['notes'] ?? '',
    animalTag: j['animalTag'] ?? '',
    implantDate: j['implantDate'] != null ? DateTime.tryParse(j['implantDate']) : null,
    species: j['species'] ?? '',
    sex: j['sex'] ?? '',
    weightG: (j['weightG'] ?? 0).toDouble(),
    config: j['config'] != null ? BeaconConfig.fromJson(j['config']) : null,
    lastSeen: j['lastSeen'] != null ? DateTime.tryParse(j['lastSeen']) : null,
    lastLat: j['lastLat']?.toDouble(),
    lastLon: j['lastLon']?.toDouble(),
    lastRssi: j['lastRssi'],
    lastBatteryMv: j['lastBatteryMv'],
    lastTempC: j['lastTempC']?.toDouble(),
    sessionCount: j['sessionCount'] ?? 0,
    fwVersion: j['fwVersion'] ?? '',
  );
}
