import 'dart:typed_data';

/// CRC32 — zlib-compatible, poly 0xEDB88320.
/// Identical to Python binascii.crc32() & 0xFFFFFFFF.
class Crc32 {
  static const int _poly = 0xEDB88320;

  static final List<int> _table = List.generate(256, (i) {
    var c = i & 0xFFFFFFFF;
    for (var j = 0; j < 8; j++) {
      if ((c & 1) != 0) {
        c = (_poly ^ (c >>> 1)) & 0xFFFFFFFF;
      } else {
        c = (c >>> 1) & 0xFFFFFFFF;
      }
    }
    return c;
  });

  static int compute(Uint8List data) {
    var crc = 0xFFFFFFFF;
    for (var i = 0; i < data.length; i++) {
      final idx = (crc ^ data[i]) & 0xFF;
      crc = (_table[idx] ^ (crc >>> 8)) & 0xFFFFFFFF;
    }
    return (crc ^ 0xFFFFFFFF) & 0xFFFFFFFF;
  }
}
