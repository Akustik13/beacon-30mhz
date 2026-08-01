import 'dart:typed_data';

/// Parsed from 36-byte BoardInfo characteristic (0x0000BEA1)
class BoardInfo {
  final int hwVersion;        // 0x02 = eval board, 0x01 = final PCB
  final int fwMajor;
  final int fwMinor;
  final int fwPatch;
  final int sensorsPresent;   // bitmask: bit0=STTS22H, bit1=ISM330DHCX, bit2=int_temp, bit3=batt_adc
  final int uniqueId;
  final String animalTag;
  final int flashTotalBytes;
  final int flashUsedBytes;

  const BoardInfo({
    required this.hwVersion,
    required this.fwMajor,
    required this.fwMinor,
    required this.fwPatch,
    required this.sensorsPresent,
    required this.uniqueId,
    required this.animalTag,
    required this.flashTotalBytes,
    required this.flashUsedBytes,
  });

  bool get hasSTTS22H      => (sensorsPresent & 0x01) != 0;
  bool get hasISM330DHCX   => (sensorsPresent & 0x02) != 0;
  bool get hasInternalTemp => (sensorsPresent & 0x04) != 0;
  bool get hasBatteryADC   => (sensorsPresent & 0x08) != 0;

  int  get flashFreeBytes  => flashTotalBytes - flashUsedBytes;
  int  get flashUsedPct    => flashTotalBytes > 0
      ? ((flashUsedBytes * 100) / flashTotalBytes).round()
      : 0;
  int  get logEntriesCount => flashUsedBytes ~/ 20;
  int  get logEntriesMax   => flashTotalBytes ~/ 20;

  String get fwVersion     => '$fwMajor.$fwMinor.$fwPatch';
  String get uniqueIdHex   => uniqueId.toRadixString(16).padLeft(8, '0').toUpperCase();

  factory BoardInfo.fromBytes(List<int> raw) {
    if (raw.length < 36) raw = [...raw, ...List.filled(36 - raw.length, 0)];
    final bytes = Uint8List.fromList(raw);
    final bd = ByteData.sublistView(bytes);
    return BoardInfo(
      hwVersion:       bytes[0],
      fwMajor:         bytes[1],
      fwMinor:         bytes[2],
      fwPatch:         bytes[3],
      sensorsPresent:  bytes[4],
      uniqueId:        bd.getUint32(8,  Endian.little),
      animalTag:       String.fromCharCodes(
                         bytes.sublist(12, 28).takeWhile((b) => b != 0).toList()),
      flashTotalBytes: bd.getUint32(28, Endian.little),
      flashUsedBytes:  bd.getUint32(32, Endian.little),
    );
  }
}
