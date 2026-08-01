import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../ble_service.dart';

class ScanScreen extends StatefulWidget {
  const ScanScreen({
    super.key,
    required this.bleService,
    required this.onConnected,
  });

  final BleService bleService;
  final VoidCallback onConnected;

  @override
  State<ScanScreen> createState() => _ScanScreenState();
}

class _ScanScreenState extends State<ScanScreen> {
  bool _scanning = false;
  bool _filterBeaconOnly = false;
  String? _connectingId;
  String? _error;

  Future<void> _scan() async {
    setState(() {
      _error = null;
      _scanning = true;
    });
    try {
      await widget.bleService.startScan();
    } catch (e) {
      setState(() => _error = e.toString());
    } finally {
      if (mounted) setState(() => _scanning = false);
    }
  }

  Future<void> _connect(ScanResult result) async {
    final id = result.device.remoteId.str;
    setState(() {
      _connectingId = id;
      _error = null;
    });
    try {
      await widget.bleService.connect(result.device);
      widget.onConnected();
    } catch (e) {
      setState(() => _error = e.toString());
    } finally {
      if (mounted) setState(() => _connectingId = null);
    }
  }

  @override
  Widget build(BuildContext context) {
    final connecting = _connectingId != null;

    return Scaffold(
      appBar: AppBar(
        title: const Text('BLE Scan'),
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 8),
            child: FilterChip(
              label: const Text('BCN only'),
              selected: _filterBeaconOnly,
              onSelected: (v) => setState(() => _filterBeaconOnly = v),
              visualDensity: VisualDensity.compact,
            ),
          ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            FilledButton.icon(
              onPressed: (_scanning || connecting) ? null : _scan,
              icon: _scanning
                  ? const SizedBox(
                      width: 18,
                      height: 18,
                      child: CircularProgressIndicator(
                        strokeWidth: 2,
                        color: Colors.white,
                      ),
                    )
                  : const Icon(Icons.bluetooth_searching),
              label: Padding(
                padding: const EdgeInsets.symmetric(vertical: 14),
                child: Text(_scanning ? 'Scanning...' : 'Scan'),
              ),
            ),
            const SizedBox(height: 8),
            if (connecting)
              const Padding(
                padding: EdgeInsets.only(bottom: 8),
                child: LinearProgressIndicator(),
              ),
            if (_error != null)
              Padding(
                padding: const EdgeInsets.only(bottom: 8),
                child: Text(
                  _error!,
                  style: const TextStyle(color: Colors.redAccent),
                ),
              ),
            Expanded(
              child: StreamBuilder<List<ScanResult>>(
                stream: widget.bleService.scanResults,
                initialData: const [],
                builder: (context, snapshot) {
                  var results = List<ScanResult>.from(snapshot.data ?? []);
                  if (_filterBeaconOnly) {
                    results =
                        results.where(widget.bleService.isLikelyBeacon).toList();
                  }
                  results.sort((a, b) => b.rssi.compareTo(a.rssi));

                  if (results.isEmpty) {
                    return Center(
                      child: Text(
                        _scanning ? 'Searching...' : 'Press Scan to find devices',
                        style: const TextStyle(color: Colors.grey),
                      ),
                    );
                  }

                  return ListView.separated(
                    itemCount: results.length,
                    separatorBuilder: (_, __) =>
                        const Divider(height: 1, indent: 56),
                    itemBuilder: (context, i) {
                      final r = results[i];
                      final isBeacon = widget.bleService.isLikelyBeacon(r);
                      final name = _nameOf(r);
                      final isThisConnecting =
                          _connectingId == r.device.remoteId.str;

                      return ListTile(
                        contentPadding: const EdgeInsets.symmetric(
                          horizontal: 4,
                          vertical: 6,
                        ),
                        leading: CircleAvatar(
                          backgroundColor: isBeacon
                              ? const Color(0xff00a39b).withOpacity(0.2)
                              : Colors.grey.withOpacity(0.15),
                          child: Icon(
                            Icons.bluetooth,
                            color: isBeacon
                                ? const Color(0xff00a39b)
                                : Colors.grey,
                            size: 20,
                          ),
                        ),
                        title: Row(
                          children: [
                            Expanded(
                              child: Text(
                                name,
                                style: TextStyle(
                                  fontWeight: isBeacon
                                      ? FontWeight.bold
                                      : FontWeight.normal,
                                ),
                              ),
                            ),
                            if (isBeacon)
                              Container(
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 6,
                                  vertical: 2,
                                ),
                                decoration: BoxDecoration(
                                  color: const Color(0xff00a39b).withOpacity(0.2),
                                  borderRadius: BorderRadius.circular(4),
                                ),
                                child: const Text(
                                  'BCN',
                                  style: TextStyle(
                                    fontSize: 10,
                                    color: Color(0xff00a39b),
                                    fontWeight: FontWeight.bold,
                                  ),
                                ),
                              ),
                          ],
                        ),
                        subtitle: Text(
                          r.device.remoteId.str,
                          style: const TextStyle(fontSize: 12),
                        ),
                        trailing: isThisConnecting
                            ? const SizedBox(
                                width: 22,
                                height: 22,
                                child: CircularProgressIndicator(strokeWidth: 2),
                              )
                            : Text(
                                '${r.rssi} dBm',
                                style: TextStyle(
                                  color: _rssiColor(r.rssi),
                                  fontSize: 13,
                                ),
                              ),
                        onTap: connecting ? null : () => _connect(r),
                      );
                    },
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }

  String _nameOf(ScanResult r) {
    if (r.device.platformName.isNotEmpty) return r.device.platformName;
    if (r.advertisementData.advName.isNotEmpty) return r.advertisementData.advName;
    return '(no name)';
  }

  Color _rssiColor(int rssi) {
    if (rssi >= -60) return Colors.green;
    if (rssi >= -75) return Colors.orange;
    return Colors.redAccent;
  }
}
