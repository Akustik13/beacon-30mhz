import 'dart:io';
import 'package:flutter/material.dart';
import '../../models/app_release.dart';
import '../../services/update_service.dart';

class UpdateDialog extends StatefulWidget {
  final String currentVersion;
  final AppRelease release;
  final UpdateService service;

  const UpdateDialog({
    super.key,
    required this.currentVersion,
    required this.release,
    required this.service,
  });

  static Future<void> show(
    BuildContext context, {
    required String currentVersion,
    required AppRelease release,
    required UpdateService service,
  }) {
    return showDialog<void>(
      context: context,
      barrierDismissible: false,
      builder: (_) => UpdateDialog(
        currentVersion: currentVersion,
        release: release,
        service: service,
      ),
    );
  }

  @override
  State<UpdateDialog> createState() => _UpdateDialogState();
}

class _UpdateDialogState extends State<UpdateDialog> {
  _Phase _phase = _Phase.idle;
  double _progress = 0.0;
  String? _error;

  Future<void> _startUpdate() async {
    setState(() { _phase = _Phase.downloading; _error = null; });
    File? apkFile;
    try {
      apkFile = await widget.service.downloadApk(
        widget.release,
        (p) { if (mounted) setState(() => _progress = p); },
      );
      if (!mounted) return;
      setState(() => _phase = _Phase.installing);
      final err = await widget.service.installApk(apkFile);
      if (!mounted) return;
      if (err != null) {
        setState(() { _phase = _Phase.error; _error = err; });
      } else {
        Navigator.of(context).pop();
      }
    } catch (e) {
      if (mounted) {
        setState(() {
          _phase = _Phase.error;
          _error = e.toString();
        });
      }
    }
  }

  Future<void> _skipVersion() async {
    await widget.service.skipVersion(widget.release.version);
    if (mounted) Navigator.of(context).pop();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return AlertDialog(
      title: const Text('Update available'),
      content: SizedBox(
        width: double.maxFinite,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'v${widget.currentVersion}  →  v${widget.release.version}',
              style: theme.textTheme.titleMedium?.copyWith(
                color: theme.colorScheme.primary,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 12),
            if (widget.release.notes.isNotEmpty) ...[
              const Text("What's new:",
                  style: TextStyle(fontWeight: FontWeight.w600)),
              const SizedBox(height: 6),
              Container(
                constraints: const BoxConstraints(maxHeight: 200),
                decoration: BoxDecoration(
                  color: theme.colorScheme.surfaceContainerHighest,
                  borderRadius: BorderRadius.circular(6),
                ),
                padding: const EdgeInsets.all(10),
                child: SingleChildScrollView(
                  child: Text(
                    widget.release.notes,
                    style: theme.textTheme.bodySmall,
                  ),
                ),
              ),
              const SizedBox(height: 12),
            ],
            if (_phase == _Phase.downloading || _phase == _Phase.installing)
              _ProgressSection(phase: _phase, progress: _progress),
            if (_phase == _Phase.error && _error != null)
              _ErrorSection(
                error: _error!,
                onRetry: () => setState(() {
                  _phase = _Phase.idle;
                  _progress = 0.0;
                  _error = null;
                }),
              ),
          ],
        ),
      ),
      actions: _phase == _Phase.idle || _phase == _Phase.error
          ? [
              TextButton(
                onPressed: _phase == _Phase.error
                    ? () => Navigator.of(context).pop()
                    : _skipVersion,
                child: Text(_phase == _Phase.error ? 'Close' : 'Skip this version'),
              ),
              FilledButton.icon(
                onPressed: _startUpdate,
                icon: const Icon(Icons.download, size: 18),
                label: Text(_phase == _Phase.error ? 'Retry' : 'Update'),
              ),
            ]
          : null,
    );
  }
}

class _ProgressSection extends StatelessWidget {
  final _Phase phase;
  final double progress;
  const _ProgressSection({required this.phase, required this.progress});

  @override
  Widget build(BuildContext context) {
    final label = phase == _Phase.installing
        ? 'Launching installer…'
        : '${(progress * 100).toStringAsFixed(0)}%';
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        LinearProgressIndicator(
          value: phase == _Phase.installing ? null : progress,
          minHeight: 6,
          borderRadius: BorderRadius.circular(3),
        ),
        const SizedBox(height: 6),
        Text(label, style: Theme.of(context).textTheme.bodySmall),
      ],
    );
  }
}

class _ErrorSection extends StatelessWidget {
  final String error;
  final VoidCallback onRetry;
  const _ErrorSection({required this.error, required this.onRetry});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: Colors.red.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: Colors.red.withValues(alpha: 0.4)),
      ),
      child: Text(error,
          style: const TextStyle(color: Colors.red, fontSize: 12)),
    );
  }
}

enum _Phase { idle, downloading, installing, error }
