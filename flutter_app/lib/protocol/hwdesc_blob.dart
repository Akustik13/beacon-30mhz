import 'dart:convert';
import 'dart:typed_data';
import 'opcodes.dart';

// HW type constants
const int hwTempNone=0, hwTempCrystal=1, hwTempNtc=2, hwTempStts22h=3, hwTempLis2dw12=4;
const int hwLightNone=0, hwLightPresent=1;
const int hwBattNone=0, hwBattAdc=1, hwBattFuelgauge=2;
const int hwAccelNone=0, hwAccelIsm330=1, hwAccelLis2dw12=2, hwAccelOther=3;
const int hwLedNone=0, hwLedSingle=1, hwLedRgb=2;

/// HwDescBlob — 128 bytes compact hardware descriptor.
class HwDescBlob {
  int    hwVersion;
  int    tempType;
  int    lightType;
  int    battType;
  int    accelType;
  int    ledType;
  int    txChannels;
  int    txPwrLevels;
  int    txFreqHz;
  int    battFullMv;
  int    battEmptyMv;
  String lightModel;
  String accelModel;
  String txType;
  String ledModel;
  String comment;

  HwDescBlob({
    this.hwVersion   = 1,
    this.tempType    = hwTempCrystal,
    this.lightType   = hwLightNone,
    this.battType    = hwBattAdc,
    this.accelType   = hwAccelLis2dw12,
    this.ledType     = hwLedSingle,
    this.txChannels  = 4,
    this.txPwrLevels = 4,
    this.txFreqHz    = 30000000,
    this.battFullMv  = 4200,
    this.battEmptyMv = 3000,
    this.lightModel  = '',
    this.accelModel  = '',
    this.txType      = 'colpitts',
    this.ledModel    = '',
    this.comment     = '',
  });

  Uint8List toBytes() {
    final b = Uint8List(hwdescBlobSize);
    b[0] = hwVersion   & 0xFF;
    b[1] = tempType    & 0xFF;
    b[2] = lightType   & 0xFF;
    b[3] = battType    & 0xFF;
    b[4] = accelType   & 0xFF;
    b[5] = ledType     & 0xFF;
    b[6] = txChannels  & 0xFF;
    b[7] = txPwrLevels & 0xFF;
    final bd = ByteData.sublistView(b);
    bd.setUint32(8,  txFreqHz    & 0xFFFFFFFF, Endian.little);
    bd.setUint16(12, battFullMv  & 0xFFFF,     Endian.little);
    bd.setUint16(14, battEmptyMv & 0xFFFF,     Endian.little);
    _writeStr(b, 16, lightModel, 16);
    _writeStr(b, 32, accelModel, 16);
    _writeStr(b, 48, txType,     16);
    _writeStr(b, 64, ledModel,   16);
    _writeStr(b, 80, comment,    48);
    return b;
  }

  static void _writeStr(Uint8List b, int off, String s, int maxLen) {
    final enc = ascii.encode(s.length > maxLen - 1 ? s.substring(0, maxLen - 1) : s);
    for (int i = 0; i < enc.length; i++) { b[off + i] = enc[i]; }
  }

  static String _readStr(Uint8List b, int off, int len) {
    final sub = b.sublist(off, off + len);
    final end = sub.indexOf(0);
    return ascii.decode(end >= 0 ? sub.sublist(0, end) : sub, allowInvalid: true);
  }

  static HwDescBlob fromBytes(Uint8List data) {
    if (data.length < hwdescBlobSize) return HwDescBlob();
    final bd = ByteData.sublistView(data, 0, hwdescBlobSize);
    return HwDescBlob(
      hwVersion:   data[0],
      tempType:    data[1],
      lightType:   data[2],
      battType:    data[3],
      accelType:   data[4],
      ledType:     data[5],
      txChannels:  data[6],
      txPwrLevels: data[7],
      txFreqHz:    bd.getUint32(8,  Endian.little),
      battFullMv:  bd.getUint16(12, Endian.little),
      battEmptyMv: bd.getUint16(14, Endian.little),
      lightModel:  _readStr(data, 16, 16),
      accelModel:  _readStr(data, 32, 16),
      txType:      _readStr(data, 48, 16),
      ledModel:    _readStr(data, 64, 16),
      comment:     _readStr(data, 80, 48),
    );
  }
}
