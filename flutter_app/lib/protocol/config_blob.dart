import 'dart:typed_data';
import 'crc32.dart';
import 'opcodes.dart';

/// ConfigBlob — 64 bytes, little-endian.
/// Layout mirrors _CFG_FMT in tx_beacon_gui_v4.py / proto_structs.h.
class ConfigBlob {
  int protoVer;       // u8  @0
  int cfgSize;        // u8  @1  = 64
  int rfMode;         // u8  @2  0=off 1=pulse 2=cont 3=eco
  int rfChannel;      // u8  @3  0..3
  int rfPower;        // u8  @4  1..4
  int ledMode;        // u8  @5
  int rfPulseMs;      // u16 @6
  int rfPeriodMs;     // u32 @8
  int tempIvS;        // u16 @12  0=off
  int lightIvS;       // u16 @14
  int batIvS;         // u16 @16
  int tempOffset01c;  // i16 @18
  int batScaleX100;   // u16 @20
  int logMask;        // u8  @22
  int logMode;        // u8  @23  0=always
  int logOverflow;    // u8  @24  0=stop 1=circular
  int logCkpN;        // u8  @25
  int logDt01c;       // u8  @26
  int logDlPct;       // u8  @27
  int logDbPct;       // u8  @28
  int logTsSource;    // u8  @29  0=boot 1=RTC
  int schedEn;        // u8  @30
  int schedScope;     // u8  @31
  int schedHours;     // u32 @32
  int schedDays;      // u8  @36
  int reserved3;      // u8  @37
  int schedMonths;    // u16 @38
  int uptimeSaveMin;  // u16 @40
  // reserved4[18]         @42..59
  int crc32;          // u32 @60  (read from wire; computed fresh by toBytes)

  ConfigBlob({
    this.protoVer      = protoVersion,
    this.cfgSize       = 64,
    this.rfMode        = 1,
    this.rfChannel     = 0,
    this.rfPower       = 4,
    this.ledMode       = 3,
    this.rfPulseMs     = 23,
    this.rfPeriodMs    = 2000,
    this.tempIvS       = 60,
    this.lightIvS      = 0,
    this.batIvS        = 3600,
    this.tempOffset01c = 0,
    this.batScaleX100  = 200,
    this.logMask       = 0x0D,
    this.logMode       = 0,
    this.logOverflow   = 1,
    this.logCkpN       = 16,
    this.logDt01c      = 5,
    this.logDlPct      = 10,
    this.logDbPct      = 2,
    this.logTsSource   = 0,
    this.schedEn       = 0,
    this.schedScope    = 0,
    this.schedHours    = 0,
    this.schedDays     = 0,
    this.reserved3     = 0,
    this.schedMonths   = 0,
    this.uptimeSaveMin = 0,
    this.crc32         = 0,
  });

  Uint8List toBytes() {
    final bd = ByteData(64);
    bd.setUint8(0,  protoVer & 0xFF);
    bd.setUint8(1,  cfgSize  & 0xFF);
    bd.setUint8(2,  rfMode   & 0xFF);
    bd.setUint8(3,  rfChannel & 0xFF);
    bd.setUint8(4,  rfPower  & 0xFF);
    bd.setUint8(5,  ledMode  & 0xFF);
    bd.setUint16(6,  rfPulseMs & 0xFFFF,    Endian.little);
    bd.setUint32(8,  rfPeriodMs & 0xFFFFFFFF, Endian.little);
    bd.setUint16(12, tempIvS & 0xFFFF,      Endian.little);
    bd.setUint16(14, lightIvS & 0xFFFF,     Endian.little);
    bd.setUint16(16, batIvS & 0xFFFF,       Endian.little);
    bd.setInt16( 18, tempOffset01c,         Endian.little);
    bd.setUint16(20, batScaleX100 & 0xFFFF, Endian.little);
    bd.setUint8(22, logMask     & 0xFF);
    bd.setUint8(23, logMode     & 0xFF);
    bd.setUint8(24, logOverflow & 0xFF);
    bd.setUint8(25, logCkpN    & 0xFF);
    bd.setUint8(26, logDt01c   & 0xFF);
    bd.setUint8(27, logDlPct   & 0xFF);
    bd.setUint8(28, logDbPct   & 0xFF);
    bd.setUint8(29, logTsSource & 0xFF);
    bd.setUint8(30, schedEn    & 0xFF);
    bd.setUint8(31, schedScope & 0xFF);
    bd.setUint32(32, schedHours & 0xFFFFFFFF,  Endian.little);
    bd.setUint8(36, schedDays  & 0xFF);
    bd.setUint8(37, reserved3  & 0xFF);
    bd.setUint16(38, schedMonths & 0xFFFF,  Endian.little);
    bd.setUint16(40, uptimeSaveMin & 0xFFFF, Endian.little);
    // bytes 42..59 are reserved zeros (ByteData is zero-initialized)

    final raw = Uint8List.sublistView(bd.buffer.asUint8List(), 0, 64);
    final computedCrc = Crc32.compute(Uint8List.sublistView(raw, 0, 60));
    bd.setUint32(60, computedCrc, Endian.little);
    return bd.buffer.asUint8List();
  }

  static ConfigBlob fromBytes(Uint8List data) {
    if (data.length < 64) throw ArgumentError('ConfigBlob needs 64 bytes');
    final bd = ByteData.sublistView(data, 0, 64);
    return ConfigBlob(
      protoVer:      bd.getUint8(0),
      cfgSize:       bd.getUint8(1),
      rfMode:        bd.getUint8(2),
      rfChannel:     bd.getUint8(3),
      rfPower:       bd.getUint8(4),
      ledMode:       bd.getUint8(5),
      rfPulseMs:     bd.getUint16(6,  Endian.little),
      rfPeriodMs:    bd.getUint32(8,  Endian.little),
      tempIvS:       bd.getUint16(12, Endian.little),
      lightIvS:      bd.getUint16(14, Endian.little),
      batIvS:        bd.getUint16(16, Endian.little),
      tempOffset01c: bd.getInt16( 18, Endian.little),
      batScaleX100:  bd.getUint16(20, Endian.little),
      logMask:       bd.getUint8(22),
      logMode:       bd.getUint8(23),
      logOverflow:   bd.getUint8(24),
      logCkpN:       bd.getUint8(25),
      logDt01c:      bd.getUint8(26),
      logDlPct:      bd.getUint8(27),
      logDbPct:      bd.getUint8(28),
      logTsSource:   bd.getUint8(29),
      schedEn:       bd.getUint8(30),
      schedScope:    bd.getUint8(31),
      schedHours:    bd.getUint32(32, Endian.little),
      schedDays:     bd.getUint8(36),
      reserved3:     bd.getUint8(37),
      schedMonths:   bd.getUint16(38, Endian.little),
      uptimeSaveMin: bd.getUint16(40, Endian.little),
      crc32:         bd.getUint32(60, Endian.little),
    );
  }

  bool verifyCrc() {
    final fresh = toBytes();
    final freshCrc = ByteData.sublistView(fresh).getUint32(60, Endian.little);
    return freshCrc == crc32;
  }

  ConfigBlob copy() => fromBytes(toBytes())..crc32 = crc32;
}
