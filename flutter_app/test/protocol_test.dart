import 'package:flutter_test/flutter_test.dart';
import 'package:beacon_manager/protocol/selftest.dart';

void main() {
  test('Protocol selftest passes', () {
    final failures = runSelftest();
    if (failures.isNotEmpty) {
      fail('Protocol selftest FAILED:\n${failures.join('\n')}');
    }
  });
}
