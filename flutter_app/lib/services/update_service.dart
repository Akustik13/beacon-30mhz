import 'dart:io';
import 'package:http/http.dart' as http;
import 'dart:convert';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:path_provider/path_provider.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:open_filex/open_filex.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/app_release.dart';

class UpdateService {
  static const _repoApi =
      'https://api.github.com/repos/Akustik13/beacon-30mhz/releases/latest';

  static bool isNewer(String current, String remote) {
    List<int> _parse(String v) =>
        v.replaceFirst('v', '').split('.').map((p) => int.tryParse(p) ?? 0).toList();
    final c = _parse(current);
    final r = _parse(remote);
    for (var i = 0; i < 3; i++) {
      final ci = i < c.length ? c[i] : 0;
      final ri = i < r.length ? r[i] : 0;
      if (ri > ci) return true;
      if (ri < ci) return false;
    }
    return false;
  }

  Future<AppRelease> fetchLatestRelease() async {
    final resp = await http.get(
      Uri.parse(_repoApi),
      headers: {'Accept': 'application/vnd.github.v3+json'},
    ).timeout(const Duration(seconds: 15));
    if (resp.statusCode != 200) {
      throw Exception('GitHub API error: ${resp.statusCode}');
    }
    return AppRelease.fromJson(jsonDecode(resp.body) as Map<String, dynamic>);
  }

  Future<String> currentVersion() async {
    final info = await PackageInfo.fromPlatform();
    return info.version;
  }

  Future<AppRelease?> checkForUpdate({bool ignoreSkipped = false}) async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final autoCheck = prefs.getBool('update_auto_check') ?? true;
      if (!autoCheck && !ignoreSkipped) return null;

      final release = await fetchLatestRelease();
      final current = await currentVersion();
      if (!isNewer(current, release.version)) return null;

      if (!ignoreSkipped) {
        final skipped = prefs.getString('update_skip_version');
        if (skipped == release.version) return null;
      }

      await prefs.setString('update_last_check', DateTime.now().toIso8601String());
      return release;
    } catch (_) {
      return null;
    }
  }

  Future<bool> shouldAutoCheck() async {
    final prefs = await SharedPreferences.getInstance();
    if (!(prefs.getBool('update_auto_check') ?? true)) return false;
    final lastStr = prefs.getString('update_last_check');
    if (lastStr == null) return true;
    final last = DateTime.tryParse(lastStr);
    if (last == null) return true;
    return DateTime.now().difference(last).inHours >= 24;
  }

  Future<File> downloadApk(
    AppRelease release,
    void Function(double progress) onProgress,
  ) async {
    final dir = await getExternalStorageDirectory();
    final apkDir = Directory('${dir!.path}/apk_updates');
    if (!await apkDir.exists()) await apkDir.create(recursive: true);

    // Delete any stale APKs from previous attempts
    await for (final f in apkDir.list()) {
      if (f is File && f.path.endsWith('.apk')) await f.delete();
    }

    final file = File('${apkDir.path}/${release.apkFileName}');
    final client = http.Client();
    try {
      final request = http.Request('GET', Uri.parse(release.apkUrl));
      final response = await client.send(request);
      final total = response.contentLength ?? release.apkSizeBytes;
      var received = 0;
      final sink = file.openWrite();
      await response.stream.listen((chunk) {
        sink.add(chunk);
        received += chunk.length;
        if (total > 0) onProgress(received / total);
      }).asFuture();
      await sink.close();
      return file;
    } catch (e) {
      client.close();
      if (await file.exists()) await file.delete();
      rethrow;
    } finally {
      client.close();
    }
  }

  Future<String?> installApk(File apkFile) async {
    final status = await Permission.requestInstallPackages.status;
    if (!status.isGranted) {
      final result = await Permission.requestInstallPackages.request();
      if (!result.isGranted) {
        return 'Install permission denied. Go to Settings > Apps > '
            'Beacon Manager > Install unknown apps and enable it.';
      }
    }
    await OpenFilex.open(apkFile.path);
    return null;
  }

  Future<void> setAutoCheckEnabled(bool enabled) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool('update_auto_check', enabled);
  }

  Future<bool> getAutoCheckEnabled() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getBool('update_auto_check') ?? true;
  }

  Future<void> skipVersion(String version) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('update_skip_version', version);
  }

  Future<DateTime?> lastCheckTime() async {
    final prefs = await SharedPreferences.getInstance();
    final s = prefs.getString('update_last_check');
    return s != null ? DateTime.tryParse(s) : null;
  }
}
