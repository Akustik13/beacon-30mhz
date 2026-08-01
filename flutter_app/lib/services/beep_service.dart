import 'dart:async';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';
import 'package:audioplayers/audioplayers.dart';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';
import '../models/beacon_config.dart';

class BeepService {
  static final _player = AudioPlayer();
  static Timer? _timer;
  static bool _playing = false;
  static VoidCallback? _onDone;

  // Play ON/OFF pattern matching txPulseOnMs / txPulseOffMs for txDurationMs total
  static Future<void> playPattern(BeaconConfig cfg, {VoidCallback? onDone}) async {
    stop();
    _onDone = onDone;
    _playing = true;

    final onMs = cfg.txPulseOnMs.clamp(50, 10000);
    final offMs = cfg.txPulseOffMs.clamp(50, 10000);
    final durationMs = cfg.txDurationMs.clamp(100, 10000);

    // Generate short WAV for the ON duration (880 Hz tone)
    final wavBytes = _buildWav(880, onMs / 1000.0);
    final tmpFile = await _writeTmp(wavBytes);

    final deadline = DateTime.now().add(Duration(milliseconds: durationMs * 3));
    _scheduleNext(tmpFile.path, onMs, offMs, deadline);
  }

  static void _scheduleNext(String path, int onMs, int offMs, DateTime deadline) {
    if (!_playing || DateTime.now().isAfter(deadline)) {
      stop();
      return;
    }
    _player.play(DeviceFileSource(path));
    _timer = Timer(Duration(milliseconds: onMs + offMs), () {
      _scheduleNext(path, onMs, offMs, deadline);
    });
  }

  static void stop() {
    _playing = false;
    _timer?.cancel();
    _timer = null;
    _player.stop();
    final cb = _onDone;
    _onDone = null;
    cb?.call();
  }

  static Future<File> _writeTmp(List<int> bytes) async {
    final dir = await getTemporaryDirectory();
    final f = File('${dir.path}/beep.wav');
    await f.writeAsBytes(bytes);
    return f;
  }

  // Build a valid WAV file: 44-byte header + PCM int16 mono at 44100 Hz
  static List<int> _buildWav(int freqHz, double durationSec) {
    const sampleRate = 44100;
    const channels = 1;
    const bitsPerSample = 16;
    final numSamples = (sampleRate * durationSec).round();
    final dataSize = numSamples * channels * (bitsPerSample ~/ 8);

    final buf = ByteData(44 + dataSize);
    int o = 0;

    // RIFF header
    _writeStr(buf, o, 'RIFF'); o += 4;
    buf.setUint32(o, 36 + dataSize, Endian.little); o += 4;
    _writeStr(buf, o, 'WAVE'); o += 4;
    _writeStr(buf, o, 'fmt '); o += 4;
    buf.setUint32(o, 16, Endian.little); o += 4;
    buf.setUint16(o, 1, Endian.little); o += 2;
    buf.setUint16(o, channels, Endian.little); o += 2;
    buf.setUint32(o, sampleRate, Endian.little); o += 4;
    buf.setUint32(o, sampleRate * channels * bitsPerSample ~/ 8, Endian.little); o += 4;
    buf.setUint16(o, channels * bitsPerSample ~/ 8, Endian.little); o += 2;
    buf.setUint16(o, bitsPerSample, Endian.little); o += 2;
    _writeStr(buf, o, 'data'); o += 4;
    buf.setUint32(o, dataSize, Endian.little); o += 4;

    // PCM samples — sine wave with fade-in/out to avoid clicks
    const amp = 28000.0;
    const fadeMs = 5;
    final fadeSamples = (sampleRate * fadeMs / 1000).round();
    for (int i = 0; i < numSamples; i++) {
      final t = i / sampleRate;
      double sample = amp * sin(2 * pi * freqHz * t);
      if (i < fadeSamples) sample *= i / fadeSamples;
      if (i > numSamples - fadeSamples) sample *= (numSamples - i) / fadeSamples;
      buf.setInt16(o, sample.round().clamp(-32768, 32767), Endian.little);
      o += 2;
    }

    return buf.buffer.asUint8List();
  }

  static void _writeStr(ByteData buf, int offset, String s) {
    for (int i = 0; i < s.length; i++) {
      buf.setUint8(offset + i, s.codeUnitAt(i));
    }
  }
}
