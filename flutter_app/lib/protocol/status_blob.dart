import 'dart:typed_data';

/// StatusBlob — 24 bytes, little-endian.
/// Format: <IhHHBBHBBHHI>
class StatusBlob {
  final int uptimeS;     // u32 @0
  final int temp01c;     // i16 @4   × 0.1 °C
  final int vddaMv;      // u16 @6
  final int batMv;       // u16 @8
  final int batPct;      // u8  @10
  final int txActive;    // u8  @11
  final int lightRaw;    // u16 @12
  final int schedActive; // u8  @14
  final int flags;       // u8  @15
  final int logUsed;     // u16 @16
  final int logTotal;    // u16 @18
  final int rtcUnix;     // u32 @20

  const StatusBlob({
    this.uptimeS     = 0,
    this.temp01c     = 365,
    this.vddaMv      = 3300,
    this.batMv       = 3720,
    this.batPct      = 82,
    this.txActive    = 0,
    this.lightRaw    = 0,
    this.schedActive = 1,
    this.flags       = 0,
    this.logUsed     = 0,
    this.logTotal    = 768,
    this.rtcUnix     = 0,
  });

  static StatusBlob fromBytes(Uint8List data) {
    if (data.length < 24) return const StatusBlob();
    final bd = ByteData.sublistView(data, 0, 24);
    return StatusBlob(
      uptimeS:     bd.getUint32(0,  Endian.little),
      temp01c:     bd.getInt16( 4,  Endian.little),
      vddaMv:      bd.getUint16(6,  Endian.little),
      batMv:       bd.getUint16(8,  Endian.little),
      batPct:      bd.getUint8(10),
      txActive:    bd.getUint8(11),
      lightRaw:    bd.getUint16(12, Endian.little),
      schedActive: bd.getUint8(14),
      flags:       bd.getUint8(15),
      logUsed:     bd.getUint16(16, Endian.little),
      logTotal:    bd.getUint16(18, Endian.little),
      rtcUnix:     bd.getUint32(20, Endian.little),
    );
  }

  double get tempC => temp01c / 10.0;
  double get batV  => batMv   / 1000.0;
}
