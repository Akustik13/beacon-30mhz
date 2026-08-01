import 'dart:convert';
import 'dart:typed_data';

/// InfoBlob — 48 bytes, little-endian.
/// Format: <BBBBBBHIII12sIIII>
/// Offsets: proto(0) hw(1) fwM(2) fwm(3) fwp(4) sens(5) lsz(6) uid(8)
///          ltot(12) fpsz(16) tag(20..31) act_h(32) stop1(36) shut(40) erase(44)
class InfoBlob {
  final int    protoVer;
  final int    hwRev;
  final int    fwMajor;
  final int    fwMinor;
  final int    fwPatch;
  final int    sensorsPresent;
  final int    logEntrySize;
  final int    uid;
  final int    logTotalEntries;
  final int    flashPageSize;
  final String tag;
  final int    totalActiveH;
  final int    totalStop1H;
  final int    totalShutdownH;
  final int    flashEraseCount;

  const InfoBlob({
    this.protoVer        = 1,
    this.hwRev           = 1,
    this.fwMajor         = 1,
    this.fwMinor         = 0,
    this.fwPatch         = 0,
    this.sensorsPresent  = 0x07,
    this.logEntrySize    = 16,
    this.uid             = 0,
    this.logTotalEntries = 768,
    this.flashPageSize   = 2048,
    this.tag             = '',
    this.totalActiveH    = 0,
    this.totalStop1H     = 0,
    this.totalShutdownH  = 0,
    this.flashEraseCount = 0,
  });

  static InfoBlob fromBytes(Uint8List data) {
    if (data.length < 48) return const InfoBlob();
    final bd = ByteData.sublistView(data, 0, 48);
    // tag at offset 20, 12 bytes
    final tagBytes = data.sublist(20, 32);
    final tagEnd   = tagBytes.indexOf(0);
    final tagStr   = latin1.decode(tagEnd >= 0 ? tagBytes.sublist(0, tagEnd) : tagBytes);
    return InfoBlob(
      protoVer:        bd.getUint8(0),
      hwRev:           bd.getUint8(1),
      fwMajor:         bd.getUint8(2),
      fwMinor:         bd.getUint8(3),
      fwPatch:         bd.getUint8(4),
      sensorsPresent:  bd.getUint8(5),
      logEntrySize:    bd.getUint16(6,  Endian.little),
      uid:             bd.getUint32(8,  Endian.little),
      logTotalEntries: bd.getUint32(12, Endian.little),
      flashPageSize:   bd.getUint32(16, Endian.little),
      tag:             tagStr,
      totalActiveH:    bd.getUint32(32, Endian.little),
      totalStop1H:     bd.getUint32(36, Endian.little),
      totalShutdownH:  bd.getUint32(40, Endian.little),
      flashEraseCount: bd.getUint32(44, Endian.little),
    );
  }

  String get fwVersion => 'v$fwMajor.$fwMinor.$fwPatch';
  String get uidHex    => '0x${uid.toRadixString(16).toUpperCase().padLeft(8, '0')}';
}
