class AppRelease {
  final String version;
  final String notes;
  final String apkUrl;
  final String apkFileName;
  final int apkSizeBytes;
  final DateTime publishedAt;

  const AppRelease({
    required this.version,
    required this.notes,
    required this.apkUrl,
    required this.apkFileName,
    required this.apkSizeBytes,
    required this.publishedAt,
  });

  factory AppRelease.fromJson(Map<String, dynamic> json) {
    final assets = json['assets'] as List;
    final asset = assets.firstWhere(
      (a) => (a['name'] as String).endsWith('.apk'),
      orElse: () => throw Exception('No APK asset in release'),
    );
    return AppRelease(
      version: (json['tag_name'] as String).replaceFirst('v', ''),
      notes: (json['body'] as String?) ?? '',
      apkUrl: asset['browser_download_url'] as String,
      apkFileName: asset['name'] as String,
      apkSizeBytes: asset['size'] as int,
      publishedAt: DateTime.parse(json['published_at'] as String),
    );
  }
}
