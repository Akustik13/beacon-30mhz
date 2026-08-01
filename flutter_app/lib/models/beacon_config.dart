import 'dart:typed_data';

class BeaconConfig {
  int channel;          // 1/2/3
  int powerLevel;       // 0=Low 1=Mid 2=High
  bool loggingEnabled;
  int logOverflowMode;  // 0=overwrite 1=stop
  int txPeriodMs;
  int txDurationMs;
  int txPulseOnMs;
  int txPulseOffMs;
  int activeHours;      // bitmask 24 bits
  int activeDays;       // bitmask 7 bits
  int activeMonths;     // bitmask 12 bits
  int bleIntervalMin;
  int bleDurationSec;
  int bleTxPowerIdx;    // index into blePowerDbm table
  String animalTag;
  int batteryCapacityMah;
  bool tempSensorEnabled;
  bool accelSensorEnabled;
  bool transmitterEnabled;  // show/enable 30 MHz TX section
  bool continuousTx;        // continuous TX (no timing/schedule)
  int tempThresholdC;       // firmware overheat threshold (°C)
  int uniqueId;             // read-only from device; set by firmware
  int tempReadPeriodSec;    // 0=read on TX only; >0=periodic read interval
  int accelMode;            // 0=off, 1=interrupt, 2=periodic, 3=both
  int accelPeriodSec;       // periodic accel read interval (when mode 2 or 3)

  static const blePowerDbm = [-40, -20, -15, -10, -5, 0, 4];
  static String blePowerLabel(int idx) =>
      '${blePowerDbm[idx.clamp(0, blePowerDbm.length - 1)]} dBm';

  BeaconConfig({
    this.channel = 1,
    this.powerLevel = 1,
    this.loggingEnabled = true,
    this.logOverflowMode = 0,
    this.txPeriodMs = 5000,
    this.txDurationMs = 500,
    this.txPulseOnMs = 100,
    this.txPulseOffMs = 100,
    this.activeHours = 0x00FFFF00,
    this.activeDays = 0x1F,
    this.activeMonths = 0x07E0,
    this.bleIntervalMin = 30,
    this.bleDurationSec = 30,
    this.bleTxPowerIdx = 5,
    this.animalTag = '',
    this.batteryCapacityMah = 300,
    this.tempSensorEnabled = true,
    this.accelSensorEnabled = true,
    this.transmitterEnabled = false,
    this.continuousTx = false,
    this.tempThresholdC = 40,
    this.uniqueId = 0,
    this.tempReadPeriodSec = 0,
    this.accelMode = 1,
    this.accelPeriodSec = 60,
  });

  BeaconConfig copyWith({
    int? channel, int? powerLevel, bool? loggingEnabled, int? logOverflowMode,
    int? txPeriodMs, int? txDurationMs,
    int? txPulseOnMs, int? txPulseOffMs, int? activeHours, int? activeDays,
    int? activeMonths, int? bleIntervalMin, int? bleDurationSec, int? bleTxPowerIdx,
    String? animalTag, int? batteryCapacityMah, int? uniqueId,
    bool? tempSensorEnabled, bool? accelSensorEnabled,
    bool? transmitterEnabled, bool? continuousTx, int? tempThresholdC,
    int? tempReadPeriodSec, int? accelMode, int? accelPeriodSec,
  }) => BeaconConfig(
    channel: channel ?? this.channel,
    powerLevel: powerLevel ?? this.powerLevel,
    loggingEnabled: loggingEnabled ?? this.loggingEnabled,
    logOverflowMode: logOverflowMode ?? this.logOverflowMode,
    txPeriodMs: txPeriodMs ?? this.txPeriodMs,
    txDurationMs: txDurationMs ?? this.txDurationMs,
    txPulseOnMs: txPulseOnMs ?? this.txPulseOnMs,
    txPulseOffMs: txPulseOffMs ?? this.txPulseOffMs,
    activeHours: activeHours ?? this.activeHours,
    activeDays: activeDays ?? this.activeDays,
    activeMonths: activeMonths ?? this.activeMonths,
    bleIntervalMin: bleIntervalMin ?? this.bleIntervalMin,
    bleDurationSec: bleDurationSec ?? this.bleDurationSec,
    bleTxPowerIdx: bleTxPowerIdx ?? this.bleTxPowerIdx,
    animalTag: animalTag ?? this.animalTag,
    batteryCapacityMah: batteryCapacityMah ?? this.batteryCapacityMah,
    uniqueId: uniqueId ?? this.uniqueId,
    tempSensorEnabled: tempSensorEnabled ?? this.tempSensorEnabled,
    accelSensorEnabled: accelSensorEnabled ?? this.accelSensorEnabled,
    transmitterEnabled: transmitterEnabled ?? this.transmitterEnabled,
    continuousTx: continuousTx ?? this.continuousTx,
    tempThresholdC: tempThresholdC ?? this.tempThresholdC,
    tempReadPeriodSec: tempReadPeriodSec ?? this.tempReadPeriodSec,
    accelMode: accelMode ?? this.accelMode,
    accelPeriodSec: accelPeriodSec ?? this.accelPeriodSec,
  );

  Map<String, dynamic> toJson() => {
    'channel': channel, 'powerLevel': powerLevel,
    'txPeriodMs': txPeriodMs, 'txDurationMs': txDurationMs,
    'txPulseOnMs': txPulseOnMs, 'txPulseOffMs': txPulseOffMs,
    'activeHours': activeHours, 'activeDays': activeDays,
    'activeMonths': activeMonths, 'bleIntervalMin': bleIntervalMin,
    'bleDurationSec': bleDurationSec, 'bleTxPowerIdx': bleTxPowerIdx,
    'animalTag': animalTag, 'batteryCapacityMah': batteryCapacityMah,
    'tempSensorEnabled': tempSensorEnabled, 'accelSensorEnabled': accelSensorEnabled,
    'transmitterEnabled': transmitterEnabled, 'continuousTx': continuousTx,
    'tempThresholdC': tempThresholdC,
    'tempReadPeriodSec': tempReadPeriodSec, 'accelMode': accelMode,
    'accelPeriodSec': accelPeriodSec,
  };

  factory BeaconConfig.fromJson(Map<String, dynamic> j) => BeaconConfig(
    channel: j['channel'] ?? 1,
    powerLevel: j['powerLevel'] ?? 1,
    txPeriodMs: j['txPeriodMs'] ?? 5000,
    txDurationMs: j['txDurationMs'] ?? 500,
    txPulseOnMs: j['txPulseOnMs'] ?? 100,
    txPulseOffMs: j['txPulseOffMs'] ?? 100,
    activeHours: j['activeHours'] ?? 0x00FFFF00,
    activeDays: j['activeDays'] ?? 0x1F,
    activeMonths: j['activeMonths'] ?? 0x07E0,
    bleIntervalMin: j['bleIntervalMin'] ?? 30,
    bleDurationSec: j['bleDurationSec'] ?? 30,
    bleTxPowerIdx: j['bleTxPowerIdx'] ?? 5,
    animalTag: j['animalTag'] ?? '',
    batteryCapacityMah: j['batteryCapacityMah'] ?? 300,
    tempSensorEnabled: j['tempSensorEnabled'] ?? true,
    accelSensorEnabled: j['accelSensorEnabled'] ?? true,
    transmitterEnabled: j['transmitterEnabled'] ?? false,
    continuousTx: j['continuousTx'] ?? false,
    tempThresholdC: j['tempThresholdC'] ?? 40,
    tempReadPeriodSec: j['tempReadPeriodSec'] ?? 0,
    accelMode: j['accelMode'] ?? 1,
    accelPeriodSec: j['accelPeriodSec'] ?? 60,
  );

  // ---- V2 BLE binary protocol (64-byte packed struct) ----

  factory BeaconConfig.fromBytes(List<int> raw) {
    if (raw.length < 64) raw = [...raw, ...List.filled(64 - raw.length, 0)];
    final bytes = Uint8List.fromList(raw);
    final bd = ByteData.sublistView(bytes);
    return BeaconConfig(
      channel:          bytes[0],
      powerLevel:       bytes[1],
      loggingEnabled:   bytes[2] != 0,
      logOverflowMode:  bytes[3],
      txPeriodMs:       bd.getUint32(4,  Endian.little),
      txDurationMs:     bd.getUint32(8,  Endian.little),
      txPulseOnMs:      bd.getUint32(12, Endian.little),
      txPulseOffMs:     bd.getUint32(16, Endian.little),
      activeHours:      bd.getUint32(20, Endian.little),
      activeDays:       bytes[24],
      tempThresholdC:   bytes[25],
      activeMonths:     bd.getUint16(26, Endian.little),
      bleIntervalMin:   bd.getUint16(28, Endian.little),
      bleDurationSec:   bd.getUint16(30, Endian.little),
      animalTag:        String.fromCharCodes(
                          bytes.sublist(32, 48).takeWhile((b) => b != 0).toList()),
      uniqueId:         bd.getUint32(48, Endian.little),
    );
  }

  Uint8List toBytes() {
    final bytes = Uint8List(64);
    final bd = ByteData.sublistView(bytes);
    bytes[0] = channel & 0xFF;
    bytes[1] = powerLevel & 0xFF;
    bytes[2] = loggingEnabled ? 1 : 0;
    bytes[3] = logOverflowMode & 0xFF;
    bd.setUint32(4,  txPeriodMs,    Endian.little);
    bd.setUint32(8,  txDurationMs,  Endian.little);
    bd.setUint32(12, txPulseOnMs,   Endian.little);
    bd.setUint32(16, txPulseOffMs,  Endian.little);
    bd.setUint32(20, activeHours,   Endian.little);
    bytes[24] = activeDays & 0xFF;
    bytes[25] = tempThresholdC & 0xFF;
    bd.setUint16(26, activeMonths,  Endian.little);
    bd.setUint16(28, bleIntervalMin, Endian.little);
    bd.setUint16(30, bleDurationSec, Endian.little);
    final tagBytes = animalTag.codeUnits.take(15).toList();
    for (int i = 0; i < tagBytes.length; i++) bytes[32 + i] = tagBytes[i];
    bd.setUint32(48, uniqueId,      Endian.little);
    bd.setUint32(52, 0x00000002,    Endian.little);  // config_version
    bd.setUint32(56, _crc32(bytes.sublist(0, 56)), Endian.little);
    // bytes 60-63 remain zero (padding)
    return bytes;
  }

  static int _crc32(List<int> data) {
    int crc = 0xFFFFFFFF;
    for (final b in data) {
      crc ^= b & 0xFF;
      for (int i = 0; i < 8; i++) {
        crc = (crc & 1) != 0 ? ((crc >> 1) ^ 0xEDB88320) : (crc >> 1);
        crc &= 0xFFFFFFFF;
      }
    }
    return (crc ^ 0xFFFFFFFF) & 0xFFFFFFFF;
  }

  static String powerLabel(int level) =>
      ['Low ~2 mA', 'Mid ~5 mA', 'High ~7 mA'][level.clamp(0, 2)];
  static double powerMa(int level) => [2.0, 5.0, 7.0][level.clamp(0, 2)];
  static String channelLabel(int ch) =>
      ['CH1 ~30.000 MHz', 'CH2 ~30.005 MHz', 'CH3 ~30.010 MHz'][(ch - 1).clamp(0, 2)];

  static String fmtMs(int ms) {
    if (ms < 1000) return '$ms ms';
    if (ms < 60000) return '${(ms / 1000.0).toStringAsFixed(ms % 1000 == 0 ? 0 : 1)} s';
    return '${(ms / 60000.0).toStringAsFixed(ms % 60000 == 0 ? 0 : 1)} min';
  }
}
