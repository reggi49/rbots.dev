import 'dart:async';
import 'dart:convert';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';
import '../../core/constants/ble_constants.dart';
import '../models/protocol_models.dart';
import 'ble_transport.dart';

/// Production BLE Transport implementation utilizing flutter_reactive_ble.
/// Strict Architecture Rule: Only this concrete class imports flutter_reactive_ble.
class ReactiveBleTransport implements BleTransport {
  final FlutterReactiveBle _ble;

  final _connectionController = StreamController<BleConnectionState>.broadcast();
  final _adapterController = StreamController<BleAdapterState>.broadcast();
  final _discoveredController = StreamController<List<DiscoveredRobot>>.broadcast();
  final _statusController = StreamController<DeviceStatus>.broadcast();
  final _eventController = StreamController<String>.broadcast();
  final _capabilitiesController = StreamController<DeviceCapabilities>.broadcast();

  BleConnectionState _connectionState = BleConnectionState.disconnected;
  BleAdapterState _adapterState = BleAdapterState.unknown;
  final List<DiscoveredRobot> _discoveredRobots = [];
  DeviceCapabilities _capabilities = DeviceCapabilities.none();

  StreamSubscription<BleStatus>? _adapterSub;
  StreamSubscription<DiscoveredDevice>? _scanSub;
  Timer? _scanTimer;
  StreamSubscription<ConnectionStateUpdate>? _connSub;
  StreamSubscription<List<int>>? _statusNotifySub;
  StreamSubscription<List<int>>? _eventNotifySub;

  String? _connectedDeviceId;
  DeviceStatus _deviceStatus = DeviceStatus.disconnected();

  // Bounded Request Coordinator State (Single In-Flight Request)
  Completer<BleResponse>? _pendingRequestCompleter;
  int? _pendingRequestId;
  Timer? _requestTimeoutTimer;

  // Maximum single-packet payload without fragmentation (MTU 512 - 3 ATT header bytes = 509 bytes)
  static const int maxNegotiatedPayloadBytes = 509;

  ReactiveBleTransport({FlutterReactiveBle? ble}) : _ble = ble ?? FlutterReactiveBle() {
    _connectionController.add(_connectionState);
    _statusController.add(_deviceStatus);
    _discoveredController.add(_discoveredRobots);
    _capabilitiesController.add(_capabilities);
    _initAdapterObserver();
  }

  void _initAdapterObserver() {
    _adapterState = _mapBleStatus(_ble.status);
    _adapterController.add(_adapterState);

    _adapterSub = _ble.statusStream.listen((status) {
      _adapterState = _mapBleStatus(status);
      _adapterController.add(_adapterState);

      // If Bluetooth was turned off during connection, clean up state
      if (_adapterState != BleAdapterState.ready &&
          _connectionState != BleConnectionState.disconnected) {
        _handleDisconnectCleanup('Bluetooth adapter became unavailable');
      }
    });
  }

  BleAdapterState _mapBleStatus(BleStatus status) {
    switch (status) {
      case BleStatus.unknown:
        return BleAdapterState.unknown;
      case BleStatus.unsupported:
        return BleAdapterState.unsupported;
      case BleStatus.unauthorized:
        return BleAdapterState.unauthorized;
      case BleStatus.poweredOff:
        return BleAdapterState.poweredOff;
      case BleStatus.locationServicesDisabled:
        return BleAdapterState.locationServicesDisabled;
      case BleStatus.ready:
        return BleAdapterState.ready;
    }
  }

  @override
  Stream<BleConnectionState> get connectionStateStream => _connectionController.stream;

  @override
  BleConnectionState get currentConnectionState => _connectionState;

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
  Stream<DeviceCapabilities> get capabilitiesStream => _capabilitiesController.stream;

  @override
  DeviceCapabilities get currentCapabilities => _capabilities;

  @override
  Future<void> startScan({Duration timeout = const Duration(seconds: 8)}) async {
    if (_connectionState == BleConnectionState.connecting ||
        _connectionState == BleConnectionState.connected) {
      return;
    }

    if (_adapterState != BleAdapterState.ready) {
      throw BleTransportError(
        code: _adapterState == BleAdapterState.poweredOff
            ? BleTransportErrorCode.bluetoothDisabled
            : BleTransportErrorCode.bluetoothUnavailable,
        message: 'Cannot scan: Bluetooth adapter is not ready (${_adapterState.name})',
      );
    }

    await stopScan();

    _connectionState = BleConnectionState.scanning;
    _connectionController.add(_connectionState);
    _discoveredRobots.clear();
    _discoveredController.add(List.unmodifiable(_discoveredRobots));

    final serviceUuid = Uuid.parse(BleConstants.serviceUuid);

    try {
      _scanSub = _ble.scanForDevices(
        withServices: [serviceUuid],
        scanMode: ScanMode.lowLatency,
      ).listen(
        (device) {
          final existingIdx = _discoveredRobots.indexWhere((r) => r.id == device.id);
          final devName = device.name.isNotEmpty
              ? device.name
              : '${BleConstants.devicePrefix}${BleConstants.defaultDeviceName}';

          final candidate = DiscoveredRobot(
            id: device.id,
            name: devName,
            rssi: device.rssi,
            serviceUuids: device.serviceUuids.map((u) => u.toString()).toList(),
          );

          if (existingIdx >= 0) {
            _discoveredRobots[existingIdx] = candidate;
          } else {
            _discoveredRobots.add(candidate);
          }
          _discoveredController.add(List.unmodifiable(_discoveredRobots));
        },
        onError: (err) {
          _handleScanError('Scan stream error: $err');
        },
      );

      _scanTimer = Timer(timeout, () async {
        await stopScan();
      });
    } catch (e) {
      _handleScanError('Failed to start scan: $e');
      throw BleTransportError(
        code: BleTransportErrorCode.unknownTransportError,
        message: 'Could not start BLE scan: $e',
      );
    }
  }

  void _handleScanError(String message) {
    stopScan();
  }

  @override
  Future<void> stopScan() async {
    _scanTimer?.cancel();
    _scanTimer = null;
    await _scanSub?.cancel();
    _scanSub = null;

    if (_connectionState == BleConnectionState.scanning) {
      _connectionState = BleConnectionState.disconnected;
      _connectionController.add(_connectionState);
    }
  }

  @override
  Future<void> connect(String deviceId, {Duration timeout = const Duration(seconds: 10)}) async {
    if (_connectionState == BleConnectionState.connected && _connectedDeviceId == deviceId) {
      return;
    }

    await stopScan();
    await disconnect();

    _connectionState = BleConnectionState.connecting;
    _connectionController.add(_connectionState);

    final completer = Completer<void>();
    final timeoutTimer = Timer(timeout, () {
      if (!completer.isCompleted) {
        _handleDisconnectCleanup('Connection timed out after ${timeout.inSeconds}s');
        completer.completeError(const BleTransportError(
          code: BleTransportErrorCode.connectionTimeout,
          message: 'Connection attempt timed out',
        ));
      }
    });

    try {
      _connSub = _ble.connectToDevice(
        id: deviceId,
        connectionTimeout: timeout,
      ).listen(
        (update) async {
          switch (update.connectionState) {
            case DeviceConnectionState.connecting:
              _connectionState = BleConnectionState.connecting;
              _connectionController.add(_connectionState);
              break;

            case DeviceConnectionState.connected:
              _connectedDeviceId = deviceId;
              try {
                await _validateGattAndSubscribe(deviceId);
                _connectionState = BleConnectionState.connected;
                _connectionController.add(_connectionState);
                if (!completer.isCompleted) {
                  timeoutTimer.cancel();
                  completer.complete();
                }

                // Query capabilities immediately upon connection
                _fetchInitialCapabilities();
              } catch (err) {
                if (!completer.isCompleted) {
                  timeoutTimer.cancel();
                  completer.completeError(err);
                }
                _handleDisconnectCleanup('GATT validation failed: $err');
              }
              break;

            case DeviceConnectionState.disconnecting:
              _connectionState = BleConnectionState.disconnecting;
              _connectionController.add(_connectionState);
              break;

            case DeviceConnectionState.disconnected:
              final wasConnected = _connectionState == BleConnectionState.connected;
              _handleDisconnectCleanup(
                wasConnected ? 'Remote device disconnected' : 'Connection failed',
              );
              if (!completer.isCompleted) {
                timeoutTimer.cancel();
                completer.completeError(BleTransportError(
                  code: BleTransportErrorCode.connectionFailed,
                  message: update.failure?.toString() ?? 'BLE connection disconnected',
                ));
              }
              break;
          }
        },
        onError: (err) {
          if (!completer.isCompleted) {
            timeoutTimer.cancel();
            completer.completeError(BleTransportError(
              code: BleTransportErrorCode.connectionFailed,
              message: 'Connection stream error: $err',
            ));
          }
          _handleDisconnectCleanup('Connection stream error: $err');
        },
      );

      await completer.future;
    } catch (e) {
      timeoutTimer.cancel();
      _handleDisconnectCleanup('Connection failed: $e');
      if (e is BleTransportError) rethrow;
      throw BleTransportError(
        code: BleTransportErrorCode.connectionFailed,
        message: 'Could not connect to device: $e',
      );
    }
  }

  Future<void> _validateGattAndSubscribe(String deviceId) async {
    // 1. Mandatory ATT MTU Negotiation (Request 512 to allow up to 509B unfragmented payload)
    try {
      await _ble.requestMtu(deviceId: deviceId, mtu: 512);
    } catch (_) {
      // Best-effort MTU exchange; ignore if platform or peripheral rejects
    }

    // 2. Discover GATT services
    await _ble.discoverAllServices(deviceId);
    final services = await _ble.getDiscoveredServices(deviceId);

    final targetSvcUuid = Uuid.parse(BleConstants.serviceUuid);
    final cmdUuid = Uuid.parse(BleConstants.commandUuid);
    final statusUuid = Uuid.parse(BleConstants.statusUuid);
    final eventUuid = Uuid.parse(BleConstants.eventUuid);

    final service = services.cast<DiscoveredService?>().firstWhere(
      (s) => s != null && s.serviceId == targetSvcUuid,
      orElse: () => null,
    );

    if (service == null) {
      throw const BleTransportError(
        code: BleTransportErrorCode.serviceNotFound,
        message: 'Required R-bot Primary Service (7b4a0001-...) not found on device',
      );
    }

    final hasCmd = service.characteristics.any((c) => c.characteristicId == cmdUuid);
    final hasStatus = service.characteristics.any((c) => c.characteristicId == statusUuid);

    if (!hasCmd || !hasStatus) {
      throw const BleTransportError(
        code: BleTransportErrorCode.characteristicNotFound,
        message: 'Required Command or Status characteristic missing from GATT service',
      );
    }

    // 3. Subscribe to Status notifications with response routing
    final statusChar = QualifiedCharacteristic(
      characteristicId: statusUuid,
      serviceId: targetSvcUuid,
      deviceId: deviceId,
    );

    _statusNotifySub = _ble.subscribeToCharacteristic(statusChar).listen(
      (bytes) {
        try {
          final jsonStr = utf8.decode(bytes);
          final json = jsonDecode(jsonStr) as Map<String, dynamic>;
          final id = json['id'] as int?;

          // Route to pending request completer if ID matches
          if (id != null && _pendingRequestId != null && id == _pendingRequestId) {
            _requestTimeoutTimer?.cancel();
            _requestTimeoutTimer = null;
            final completer = _pendingRequestCompleter;
            _pendingRequestCompleter = null;
            _pendingRequestId = null;
            if (completer != null && !completer.isCompleted) {
              completer.complete(BleResponse.fromJson(json));
            }
          }

          // Route capabilities if present
          if (json.containsKey('capabilities') && json['capabilities'] is Map<String, dynamic>) {
            _capabilities = DeviceCapabilities.fromJson(json['capabilities'] as Map<String, dynamic>);
            _capabilitiesController.add(_capabilities);
          }

          // Route general status if present
          if (json.containsKey('robot_name') || json.containsKey('active_face')) {
            _deviceStatus = DeviceStatus.fromJson(json);
            _statusController.add(_deviceStatus);
          }
        } catch (_) {}
      },
      onError: (_) {},
    );

    // 4. Subscribe to Event notifications if present
    final hasEvent = service.characteristics.any((c) => c.characteristicId == eventUuid);
    if (hasEvent) {
      final eventChar = QualifiedCharacteristic(
        characteristicId: eventUuid,
        serviceId: targetSvcUuid,
        deviceId: deviceId,
      );

      _eventNotifySub = _ble.subscribeToCharacteristic(eventChar).listen(
        (bytes) {
          try {
            final jsonStr = utf8.decode(bytes);
            _eventController.add(jsonStr);
          } catch (_) {}
        },
        onError: (_) {},
      );
    }

    // 5. Initial status read
    try {
      final initialBytes = await _ble.readCharacteristic(statusChar);
      final jsonStr = utf8.decode(initialBytes);
      final json = jsonDecode(jsonStr) as Map<String, dynamic>;
      _deviceStatus = DeviceStatus.fromJson(json);
      _statusController.add(_deviceStatus);
    } catch (_) {}
  }

  Future<void> _fetchInitialCapabilities() async {
    try {
      final cmd = BleCommand.getCapabilities(
        id: DateTime.now().millisecondsSinceEpoch % 1000000,
      );
      final res = await sendCommand(cmd, timeout: const Duration(seconds: 3));
      if (res.ok && res.data.containsKey('capabilities')) {
        _capabilities = DeviceCapabilities.fromJson(
          res.data['capabilities'] as Map<String, dynamic>,
        );
        _capabilitiesController.add(_capabilities);
      }
    } catch (_) {}
  }

  void _handleDisconnectCleanup(String reason) {
    _statusNotifySub?.cancel();
    _statusNotifySub = null;
    _eventNotifySub?.cancel();
    _eventNotifySub = null;
    _connSub?.cancel();
    _connSub = null;

    // Fail any in-flight pending request
    _requestTimeoutTimer?.cancel();
    _requestTimeoutTimer = null;
    if (_pendingRequestCompleter != null && !_pendingRequestCompleter!.isCompleted) {
      _pendingRequestCompleter!.complete(BleResponse(
        v: BleConstants.protocolVersion,
        id: _pendingRequestId ?? 0,
        ok: false,
        errorCode: 'DISCONNECTED',
        errorMessage: 'Device disconnected while request was pending',
      ));
    }
    _pendingRequestCompleter = null;
    _pendingRequestId = null;

    _connectedDeviceId = null;
    _connectionState = BleConnectionState.disconnected;
    _deviceStatus = DeviceStatus.disconnected();
    _capabilities = DeviceCapabilities.none();

    _connectionController.add(_connectionState);
    _statusController.add(_deviceStatus);
    _capabilitiesController.add(_capabilities);
  }

  @override
  Future<void> disconnect() async {
    _handleDisconnectCleanup('User requested disconnect');
  }

  @override
  Future<BleResponse> sendCommand(
    BleCommand command, {
    Duration timeout = const Duration(seconds: 5),
  }) async {
    if (_connectionState != BleConnectionState.connected || _connectedDeviceId == null) {
      return BleResponse(
        v: BleConstants.protocolVersion,
        id: command.id,
        ok: false,
        errorCode: 'NOT_CONNECTED',
        errorMessage: 'Device is not connected',
      );
    }

    // 1-in-flight queue enforcement
    if (_pendingRequestCompleter != null && !_pendingRequestCompleter!.isCompleted) {
      return BleResponse(
        v: BleConstants.protocolVersion,
        id: command.id,
        ok: false,
        errorCode: 'BUSY',
        errorMessage: 'Another request is currently in flight',
      );
    }

    final payloadStr = command.encode();
    final payloadBytes = utf8.encode(payloadStr);

    if (payloadBytes.length > maxNegotiatedPayloadBytes) {
      return BleResponse(
        v: BleConstants.protocolVersion,
        id: command.id,
        ok: false,
        errorCode: 'PAYLOAD_TOO_LARGE',
        errorMessage: 'Payload of ${payloadBytes.length} bytes exceeds unfragmented limit ($maxNegotiatedPayloadBytes bytes)',
      );
    }

    final targetSvcUuid = Uuid.parse(BleConstants.serviceUuid);
    final cmdUuid = Uuid.parse(BleConstants.commandUuid);

    final cmdChar = QualifiedCharacteristic(
      characteristicId: cmdUuid,
      serviceId: targetSvcUuid,
      deviceId: _connectedDeviceId!,
    );

    final completer = Completer<BleResponse>();
    _pendingRequestCompleter = completer;
    _pendingRequestId = command.id;

    _requestTimeoutTimer = Timer(timeout, () {
      if (_pendingRequestId == command.id) {
        _pendingRequestCompleter = null;
        _pendingRequestId = null;
        _requestTimeoutTimer = null;
        if (!completer.isCompleted) {
          completer.complete(BleResponse(
            v: BleConstants.protocolVersion,
            id: command.id,
            ok: false,
            errorCode: 'TIMEOUT',
            errorMessage: 'Protocol request timed out after ${timeout.inSeconds}s',
          ));
        }
      }
    });

    try {
      // Write-with-response to Command characteristic (acknowledges ATT layer delivery)
      await _ble.writeCharacteristicWithResponse(cmdChar, value: payloadBytes);
      // Wait for application-level response routed from Status characteristic notification
      return await completer.future;
    } catch (e) {
      _requestTimeoutTimer?.cancel();
      _requestTimeoutTimer = null;
      if (_pendingRequestId == command.id) {
        _pendingRequestCompleter = null;
        _pendingRequestId = null;
      }
      return BleResponse(
        v: BleConstants.protocolVersion,
        id: command.id,
        ok: false,
        errorCode: 'TRANSPORT_ERROR',
        errorMessage: e.toString(),
      );
    }
  }

  void dispose() {
    stopScan();
    disconnect();
    _adapterSub?.cancel();
    _connectionController.close();
    _adapterController.close();
    _discoveredController.close();
    _statusController.close();
    _eventController.close();
    _capabilitiesController.close();
  }
}
