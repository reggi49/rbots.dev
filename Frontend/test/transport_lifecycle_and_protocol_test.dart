import 'package:flutter_test/flutter_test.dart';
import 'package:rbot_companion/data/ble/ble_transport.dart';
import 'package:rbot_companion/data/models/protocol_models.dart';

void main() {
  group('Transport Lifecycle & Read-Only Protocol Verification', () {
    late MockBleTransport transport;

    setUp(() {
      transport = MockBleTransport();
    });

    tearDown(() {
      transport.dispose();
    });

    test('Initial connection state is disconnected with ready adapter', () {
      expect(transport.currentConnectionState, equals(BleConnectionState.disconnected));
      expect(transport.currentAdapterState, equals(BleAdapterState.ready));
      expect(transport.discoveredRobots, isEmpty);
    });

    test('Scanning updates discoveredRobots list and state', () async {
      final states = <BleConnectionState>[];
      final sub = transport.connectionStateStream.listen(states.add);
      addTearDown(sub.cancel);

      await transport.startScan();
      expect(transport.currentConnectionState, equals(BleConnectionState.scanning));
      expect(transport.discoveredRobots.isNotEmpty, isTrue);
      expect(transport.discoveredRobots.first.id, equals('rbot_esp32s3_mock'));

      await transport.stopScan();
      expect(transport.currentConnectionState, equals(BleConnectionState.disconnected));
    });

    test('Connect and disconnect update connection state and status', () async {
      await transport.connect('rbot_esp32s3_mock');
      expect(transport.currentConnectionState, equals(BleConnectionState.connected));

      await transport.disconnect();
      expect(transport.currentConnectionState, equals(BleConnectionState.disconnected));
    });

    test('Sending command while disconnected returns NOT_CONNECTED error', () async {
      final cmd = BleCommand.getInfo(id: 1);
      final resp = await transport.sendCommand(cmd);

      expect(resp.ok, isFalse);
      expect(resp.errorCode, equals('NOT_CONNECTED'));
      expect(resp.id, equals(1));
    });

    test('PING command succeeds and returns pong: true with matching id', () async {
      await transport.connect('rbot_esp32s3_mock');

      final cmd = BleCommand(id: 42, type: 'ping', action: 'ping');
      final resp = await transport.sendCommand(cmd);

      expect(resp.ok, isTrue);
      expect(resp.id, equals(42));
      expect(resp.data['pong'], isTrue);
    });

    test('GET_INFO returns valid firmware and hardware metadata', () async {
      await transport.connect('rbot_esp32s3_mock');

      final cmd = BleCommand.getInfo(id: 101);
      final resp = await transport.sendCommand(cmd);

      expect(resp.ok, isTrue);
      expect(resp.id, equals(101));
      expect(resp.data['hardware_id'], equals('ESP32-S3-N16R8'));
      expect(resp.data['firmware_version'], isNotNull);
    });

    test('GET_STATUS returns live status with honest battery availability (has_real_battery: false)', () async {
      await transport.connect('rbot_esp32s3_mock');

      final cmd = BleCommand.getStatus(id: 102);
      final resp = await transport.sendCommand(cmd);

      expect(resp.ok, isTrue);
      expect(resp.id, equals(102));
      expect(resp.data['has_real_battery'], isFalse);
      expect(resp.data['active_face'], equals('unavailable'));
      expect(resp.data['movement_state'], equals('unavailable'));
      expect(resp.data['server_connected'], equals('unknown'));
      expect(resp.data['battery_percent'], isNull);
      expect(resp.data['battery_voltage'], isNull);
    });

    test('GET_CAPABILITIES returns truthful feature flags (mutation flags false)', () async {
      await transport.connect('rbot_esp32s3_mock');

      final cmd = BleCommand(id: 103, type: 'system', action: 'get_capabilities');
      final resp = await transport.sendCommand(cmd);

      expect(resp.ok, isTrue);
      expect(resp.id, equals(103));

      final caps = resp.data['capabilities'] as Map<String, dynamic>;
      expect(caps['motor'], isFalse);
      expect(caps['config_write'], isFalse);
      expect(caps['watch_face_write'], isFalse);
      expect(caps['wifi_provisioning'], isFalse);
      expect(caps['wifi_ota'], isFalse);
      expect(caps['rollback'], isFalse);
      expect(caps['battery_adc'], isFalse);
    });

    test('Motor commands are safely rejected with NOT_SUPPORTED', () async {
      await transport.connect('rbot_esp32s3_mock');

      final cmd = BleCommand.motorMove(
        id: 201,
        direction: MotorDirection.forward,
        speed: 50,
      );
      final resp = await transport.sendCommand(cmd);

      expect(resp.ok, isFalse);
      expect(resp.id, equals(201));
      expect(resp.errorCode, equals('NOT_SUPPORTED'));
    });

    test('Configuration mutation commands are safely rejected with NOT_SUPPORTED', () async {
      await transport.connect('rbot_esp32s3_mock');

      final cmd = BleCommand.setConfig(
        id: 202,
        deviceName: 'NewName',
      );
      final resp = await transport.sendCommand(cmd);

      expect(resp.ok, isFalse);
      expect(resp.id, equals(202));
      expect(resp.errorCode, equals('NOT_SUPPORTED'));
    });

    test('Watch face mutation commands are safely rejected with NOT_SUPPORTED', () async {
      await transport.connect('rbot_esp32s3_mock');

      final cmd = BleCommand.setWatchFace(
        id: 203,
        themeId: 'happy_cat',
      );
      final resp = await transport.sendCommand(cmd);

      expect(resp.ok, isFalse);
      expect(resp.id, equals(203));
      expect(resp.errorCode, equals('NOT_SUPPORTED'));
    });

    test('OTA commands are safely rejected with NOT_SUPPORTED', () async {
      await transport.connect('rbot_esp32s3_mock');

      final cmd = BleCommand.startOta(
        id: 204,
        url: 'http://example.com/fw.bin',
        sha256: 'abc',
        size: 1000,
      );
      final resp = await transport.sendCommand(cmd);

      expect(resp.ok, isFalse);
      expect(resp.id, equals(204));
      expect(resp.errorCode, equals('NOT_SUPPORTED'));
    });

    test('Unsupported protocol version returns UNSUPPORTED_VERSION error', () async {
      await transport.connect('rbot_esp32s3_mock');

      final cmd = BleCommand(v: 99, id: 301, type: 'ping', action: 'ping');
      final resp = await transport.sendCommand(cmd);

      expect(resp.ok, isFalse);
      expect(resp.id, equals(301));
      expect(resp.errorCode, equals('UNSUPPORTED_VERSION'));
    });
  });
}
