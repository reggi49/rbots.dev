import 'dart:convert';
import 'package:flutter_test/flutter_test.dart';
import 'package:rbot_companion/data/models/protocol_models.dart';

void main() {
  group('BLE MTU & UTF-8 Payload Measurement Tests', () {
    const int maxDefaultAttPayload = 20; // 23 - 3 ATT header
    const int maxNegotiatedAttPayload = 509; // 512 - 3 ATT header

    test('PING request and response fit within negotiated MTU (509B) but exceed default MTU (20B)', () {
      final reqStr = BleCommand(id: 1729665000000, type: 'ping', action: 'ping').encode();
      final reqBytes = utf8.encode(reqStr);

      expect(reqBytes.length, greaterThan(maxDefaultAttPayload));
      expect(reqBytes.length, lessThanOrEqualTo(maxNegotiatedAttPayload));
      expect(reqBytes.length, equals(56));

      const respStr = '{"v":1,"id":1729665000000,"ok":true,"pong":true}';
      final respBytes = utf8.encode(respStr);
      expect(respBytes.length, greaterThan(maxDefaultAttPayload));
      expect(respBytes.length, lessThanOrEqualTo(maxNegotiatedAttPayload));
      expect(respBytes.length, equals(respStr.length));
    });

    test('GET_INFO request and response fit safely within negotiated MTU (509B)', () {
      final reqStr = BleCommand.getInfo(id: 1729665000000).encode();
      final reqBytes = utf8.encode(reqStr);

      expect(reqBytes.length, greaterThan(maxDefaultAttPayload));
      expect(reqBytes.length, lessThanOrEqualTo(maxNegotiatedAttPayload));
      expect(reqBytes.length, equals(62));

      const respStr =
          '{"v":1,"id":1729665000000,"ok":true,"robot_name":"Rbot-DEV","firmware_version":"3e0559c-dirty","hardware_id":"ESP32-S3-N16R8","build_date":"Oct 23 2026 14:18:00"}';
      final respBytes = utf8.encode(respStr);
      expect(respBytes.length, greaterThan(maxDefaultAttPayload));
      expect(respBytes.length, lessThanOrEqualTo(maxNegotiatedAttPayload));
      expect(respBytes.length, equals(respStr.length));
    });

    test('GET_STATUS request and response fit safely within negotiated MTU (509B)', () {
      final reqStr = BleCommand.getStatus(id: 1729665000000).encode();
      final reqBytes = utf8.encode(reqStr);

      expect(reqBytes.length, greaterThan(maxDefaultAttPayload));
      expect(reqBytes.length, lessThanOrEqualTo(maxNegotiatedAttPayload));
      expect(reqBytes.length, equals(64));

      const respStr =
          '{"v":1,"id":1729665000000,"ok":true,"robot_name":"Rbot-DEV","firmware_version":"3e0559c-dirty","hardware_id":"ESP32-S3-N16R8","active_face":"unavailable","movement_state":"unavailable","has_real_battery":false,"wifi_connected":false,"server_connected":"unknown","ota_supported":false}';
      final respBytes = utf8.encode(respStr);
      expect(respBytes.length, greaterThan(maxDefaultAttPayload));
      expect(respBytes.length, lessThanOrEqualTo(maxNegotiatedAttPayload));
      expect(respBytes.length, equals(respStr.length));
      expect(respBytes.length, equals(284));
    });

    test('GET_CAPABILITIES request and response fit safely within negotiated MTU (509B)', () {
      final reqStr = BleCommand.getCapabilities(id: 1729665000000).encode();
      final reqBytes = utf8.encode(reqStr);

      expect(reqBytes.length, greaterThan(maxDefaultAttPayload));
      expect(reqBytes.length, lessThanOrEqualTo(maxNegotiatedAttPayload));
      expect(reqBytes.length, equals(70));

      const respStr =
          '{"v":1,"id":1729665000000,"ok":true,"capabilities":{"motor":false,"face":false,"voice":false,"ota":false,"battery_adc":false,"config_write":false,"watch_face_write":false,"wifi_provisioning":false,"wifi_ota":false,"rollback":false}}';
      final respBytes = utf8.encode(respStr);
      expect(respBytes.length, greaterThan(maxDefaultAttPayload));
      expect(respBytes.length, lessThanOrEqualTo(maxNegotiatedAttPayload));
      expect(respBytes.length, equals(respStr.length));
    });

    test('NOT_SUPPORTED and INVALID_ARGUMENT responses fit safely within negotiated MTU (509B)', () {
      const notSupported =
          '{"v":1,"id":1729665000000,"ok":false,"error_code":"NOT_SUPPORTED","error_message":"Command not supported in read-only slice"}';
      final notSuppBytes = utf8.encode(notSupported);
      expect(notSuppBytes.length, greaterThan(maxDefaultAttPayload));
      expect(notSuppBytes.length, lessThanOrEqualTo(maxNegotiatedAttPayload));
      expect(notSuppBytes.length, equals(notSupported.length));

      const malformed =
          '{"v":1,"id":0,"ok":false,"error_code":"INVALID_ARGUMENT","error_message":"Invalid JSON format"}';
      final malformedBytes = utf8.encode(malformed);
      expect(malformedBytes.length, greaterThan(maxDefaultAttPayload));
      expect(malformedBytes.length, lessThanOrEqualTo(maxNegotiatedAttPayload));
      expect(malformedBytes.length, equals(malformed.length));
    });
  });
}

