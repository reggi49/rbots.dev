import 'dart:async';
import '../../core/constants/ble_constants.dart';
import '../models/protocol_models.dart';
import 'ble_transport.dart';

/// Test-only Mock BLE Transport for unit and widget testing.
/// Strict constraint: Must only be instantiated in tests or explicit provider overrides.
class MockBleTransport implements BleTransport {
  final _connectionController = StreamController<BleConnectionState>.broadcast();
  final _adapterController = StreamController<BleAdapterState>.broadcast();
  final _discoveredController = StreamController<List<DiscoveredRobot>>.broadcast();
  final _statusController = StreamController<DeviceStatus>.broadcast();
  final _eventController = StreamController<String>.broadcast();
  final _capabilitiesController = StreamController<DeviceCapabilities>.broadcast();

  BleConnectionState _state = BleConnectionState.disconnected;
  final BleAdapterState _adapterState = BleAdapterState.ready;
  List<DiscoveredRobot> _discoveredRobots = [];
  DeviceCapabilities _capabilities = DeviceCapabilities.none();

  final DeviceStatus _deviceStatus = const DeviceStatus(
    robotName: 'Mochi (Mock)',
    firmwareVersion: 'v1.0.0-mock',
    hardwareId: 'ESP32-S3-N16R8',
    activeFace: 'unavailable',
    movementState: 'unavailable',
    batteryPercent: null,
    batteryVoltage: null,
    hasRealBattery: false,
    wifiConnected: true,
    serverConnected: 'unknown',
    otaSupported: false,
  );

  MockBleTransport() {
    _connectionController.add(_state);
    _adapterController.add(_adapterState);
    _discoveredController.add(_discoveredRobots);
    _statusController.add(_deviceStatus);
    _capabilitiesController.add(_capabilities);
  }

  void setMockCapabilities(DeviceCapabilities caps) {
    _capabilities = caps;
    _capabilitiesController.add(_capabilities);
  }

  @override
  Stream<DeviceCapabilities> get capabilitiesStream => _capabilitiesController.stream;

  @override
  DeviceCapabilities get currentCapabilities => _capabilities;

  @override
  Stream<BleConnectionState> get connectionStateStream => _connectionController.stream;

  @override
  BleConnectionState get currentConnectionState => _state;

  @override
  Stream<BleAdapterState> get adapterStateStream => _adapterController.stream;

  @override
  BleAdapterState get currentAdapterState => _adapterState;

  @override
  Stream<List<DiscoveredRobot>> get discoveredRobotsStream => _discoveredController.stream;

  @override
  List<DiscoveredRobot> get discoveredRobots => List.unmodifiable(_discoveredRobots);

  @override
  Stream<DeviceStatus> get statusStream => _statusController.stream;

  @override
  Stream<String> get rawEventStream => _eventController.stream;

  @override
  Future<void> startScan({Duration timeout = const Duration(seconds: 5)}) async {
    _state = BleConnectionState.scanning;
    _connectionController.add(_state);

    // Simulate finding a valid R-bot Mochi peripheral
    _discoveredRobots = [
      const DiscoveredRobot(
        id: 'rbot_esp32s3_mock',
        name: 'Rbot-Mochi (Simulated)',
        rssi: -58,
        serviceUuids: [BleConstants.serviceUuid],
      ),
    ];
    _discoveredController.add(_discoveredRobots);
  }

  @override
  Future<void> stopScan() async {
    if (_state == BleConnectionState.scanning) {
      _state = BleConnectionState.disconnected;
      _connectionController.add(_state);
    }
  }

  @override
  Future<void> connect(String deviceId, {Duration timeout = const Duration(seconds: 10)}) async {
    _state = BleConnectionState.connecting;
    _connectionController.add(_state);

    await Future.delayed(const Duration(milliseconds: 50));
    _state = BleConnectionState.connected;
    _connectionController.add(_state);
    _statusController.add(_deviceStatus);
  }

  @override
  Future<void> disconnect() async {
    _state = BleConnectionState.disconnected;
    _connectionController.add(_state);
    _statusController.add(DeviceStatus.disconnected());
  }

  @override
  Future<BleResponse> sendCommand(BleCommand command, {Duration timeout = const Duration(seconds: 5)}) async {
    if (_state != BleConnectionState.connected) {
      return BleResponse(
        v: BleConstants.protocolVersion,
        id: command.id,
        ok: false,
        errorCode: 'NOT_CONNECTED',
        errorMessage: 'Device is not connected',
      );
    }

    // Protocol v1 version check
    if (command.v != BleConstants.protocolVersion) {
      return BleResponse(
        v: BleConstants.protocolVersion,
        id: command.id,
        ok: false,
        errorCode: 'UNSUPPORTED_VERSION',
        errorMessage: 'Unsupported protocol version',
      );
    }

    // PING
    if (command.type == 'ping' || command.action == 'ping') {
      return BleResponse(
        v: BleConstants.protocolVersion,
        id: command.id,
        ok: true,
        data: {'pong': true},
      );
    }

    // GET_INFO
    if (command.action == 'get_info') {
      return BleResponse(
        v: BleConstants.protocolVersion,
        id: command.id,
        ok: true,
        data: {
          'robot_name': 'Rbot-Mochi',
          'firmware_version': 'v1.0.0',
          'hardware_id': 'ESP32-S3-N16R8',
          'build_date': '2026-10-23',
        },
      );
    }

    // GET_STATUS
    if (command.action == 'get_status') {
      return BleResponse(
        v: BleConstants.protocolVersion,
        id: command.id,
        ok: true,
        data: {
          'robot_name': 'Rbot-Mochi',
          'firmware_version': 'v1.0.0',
          'hardware_id': 'ESP32-S3-N16R8',
          'active_face': 'unavailable',
          'movement_state': 'unavailable',
          'has_real_battery': false,
          'wifi_connected': true,
          'server_connected': 'unknown',
          'ota_supported': false,
        },
      );
    }

    // GET_CAPABILITIES
    if (command.action == 'get_capabilities') {
      return BleResponse(
        v: BleConstants.protocolVersion,
        id: command.id,
        ok: true,
        data: {
          'capabilities': {
            'motor': false,
            'face': false,
            'voice': false,
            'ota': false,
            'battery_adc': false,
            'config_write': false,
            'watch_face_write': false,
            'wifi_provisioning': false,
            'wifi_ota': false,
            'rollback': false,
          },
        },
      );
    }

    // All mutation and unsupported commands (motor, config, face, ota, reboot) return NOT_SUPPORTED
    return BleResponse(
      v: BleConstants.protocolVersion,
      id: command.id,
      ok: false,
      errorCode: 'NOT_SUPPORTED',
      errorMessage: 'Command not supported in read-only slice',
    );
  }

  void dispose() {
    _connectionController.close();
    _adapterController.close();
    _discoveredController.close();
    _statusController.close();
    _eventController.close();
    _capabilitiesController.close();
  }
}
