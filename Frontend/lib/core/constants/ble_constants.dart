/// BLE GATT Specification & Protocol Constants
/// Master Prompt Section 6: UUID & Schema Contract
class BleConstants {
  BleConstants._();

  // Primary Service UUID
  static const String serviceUuid = '7b4a0001-9d3f-4c2e-8a61-2f0c5b7e1000';

  // Characteristics
  static const String commandUuid = '7b4a0002-9d3f-4c2e-8a61-2f0c5b7e1000';
  static const String statusUuid = '7b4a0003-9d3f-4c2e-8a61-2f0c5b7e1000';
  static const String configUuid = '7b4a0004-9d3f-4c2e-8a61-2f0c5b7e1000';
  static const String eventUuid = '7b4a0005-9d3f-4c2e-8a61-2f0c5b7e1000';

  // Device Identification
  static const String devicePrefix = 'Rbot-';
  static const String defaultDeviceName = 'Mochi';

  // Protocol Version
  static const int protocolVersion = 1;

  // Motor Safety Constraints
  static const int defaultSpeed = 45;
  static const int maxSafeSpeed = 80;
  static const int minSpeed = 20;
  static const int movementTtlMs = 500;
  static const int heartbeatIntervalMs = 250;
}
