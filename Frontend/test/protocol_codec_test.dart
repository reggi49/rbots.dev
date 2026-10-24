import 'package:flutter_test/flutter_test.dart';
import 'package:rbot_companion/data/models/protocol_models.dart';

void main() {
  group('Protocol v1 Codec Tests', () {
    test('Encodes motor forward command correctly', () {
      final cmd = BleCommand.motorMove(
        id: 101,
        direction: MotorDirection.forward,
        speed: 50,
        ttlMs: 500,
      );

      final json = cmd.toJson();
      expect(json['v'], equals(1));
      expect(json['id'], equals(101));
      expect(json['type'], equals('motor'));
      expect(json['action'], equals('move'));
      expect(json['direction'], equals('forward'));
      expect(json['speed'], equals(50));
      expect(json['ttl_ms'], equals(500));
    });

    test('Encodes motor stop command with action stop and speed 0', () {
      final cmd = BleCommand.motorMove(
        id: 102,
        direction: MotorDirection.stop,
        speed: 0,
        ttlMs: 0,
      );

      final json = cmd.toJson();
      expect(json['action'], equals('stop'));
      expect(json['direction'], equals('stop'));
    });

    test('Decodes successful response correctly', () {
      const raw = '{"v":1,"id":101,"ok":true,"state":"moving"}';
      final res = BleResponse.decode(raw);

      expect(res.v, equals(1));
      expect(res.id, equals(101));
      expect(res.ok, isTrue);
      expect(res.state, equals('moving'));
      expect(res.errorCode, isNull);
    });

    test('Decodes error response with error code and message', () {
      const raw = '{"v":1,"id":103,"ok":false,"error_code":"INVALID_ARGUMENT","error_message":"Speed out of range"}';
      final res = BleResponse.decode(raw);

      expect(res.ok, isFalse);
      expect(res.errorCode, equals('INVALID_ARGUMENT'));
      expect(res.errorMessage, equals('Speed out of range'));
    });

    test('DeviceStatus handles absent battery telemetry safely (no fake values)', () {
      final json = {
        'robot_name': 'Mochi',
        'firmware_version': 'v1.0.0',
        'hardware_id': 'ESP32-S3-N16R8',
        'active_face': 'kawaii_classic',
        'movement_state': 'idle',
        'has_real_battery': false,
        'wifi_connected': true,
        'server_connected': true,
        'ota_supported': false,
      };

      final status = DeviceStatus.fromJson(json);
      expect(status.hasRealBattery, isFalse);
      expect(status.batteryPercent, isNull);
      expect(status.batteryVoltage, isNull);
    });
  });
}
