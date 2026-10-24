import 'package:flutter_test/flutter_test.dart';
import 'package:rbot_companion/core/constants/ble_constants.dart';

void main() {
  group('BLE UUID Contract & Consistency Audit', () {
    const expectedServiceUuid = '7b4a0001-9d3f-4c2e-8a61-2f0c5b7e1000';
    const expectedCommandUuid = '7b4a0002-9d3f-4c2e-8a61-2f0c5b7e1000';
    const expectedStatusUuid  = '7b4a0003-9d3f-4c2e-8a61-2f0c5b7e1000';
    const expectedConfigUuid  = '7b4a0004-9d3f-4c2e-8a61-2f0c5b7e1000';
    const expectedEventUuid   = '7b4a0005-9d3f-4c2e-8a61-2f0c5b7e1000';

    final uuidRegex = RegExp(r'^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$');

    test('Primary service UUID matches contract exactly', () {
      expect(BleConstants.serviceUuid, equals(expectedServiceUuid));
      expect(uuidRegex.hasMatch(BleConstants.serviceUuid), isTrue);
    });

    test('Command characteristic UUID matches contract exactly', () {
      expect(BleConstants.commandUuid, equals(expectedCommandUuid));
      expect(uuidRegex.hasMatch(BleConstants.commandUuid), isTrue);
    });

    test('Status characteristic UUID matches contract exactly', () {
      expect(BleConstants.statusUuid, equals(expectedStatusUuid));
      expect(uuidRegex.hasMatch(BleConstants.statusUuid), isTrue);
    });

    test('Configuration characteristic UUID matches contract exactly', () {
      expect(BleConstants.configUuid, equals(expectedConfigUuid));
      expect(uuidRegex.hasMatch(BleConstants.configUuid), isTrue);
    });

    test('Event characteristic UUID matches contract exactly', () {
      expect(BleConstants.eventUuid, equals(expectedEventUuid));
      expect(uuidRegex.hasMatch(BleConstants.eventUuid), isTrue);
    });

    test('All 5 GATT UUIDs are distinct and strictly unique', () {
      final allUuids = [
        BleConstants.serviceUuid,
        BleConstants.commandUuid,
        BleConstants.statusUuid,
        BleConstants.configUuid,
        BleConstants.eventUuid,
      ];
      final uniqueSet = allUuids.toSet();
      expect(uniqueSet.length, equals(5));
    });
  });
}

