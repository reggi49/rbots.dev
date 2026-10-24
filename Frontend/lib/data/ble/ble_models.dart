/// Package-independent BLE models for R-bot Companion.
/// Master Architecture Rule: No third-party BLE plugin types leak outside the transport layer.
library;

/// Operating status of the mobile host's Bluetooth adapter.
enum BleAdapterState {
  unknown,
  unsupported,
  unauthorized,
  poweredOff,
  locationServicesDisabled,
  ready,
}

/// Connection lifecycle states of the R-bot BLE peripheral.
enum BleConnectionState {
  disconnected,
  scanning,
  connecting,
  connected,
  disconnecting,
  error,
}

/// Discovered R-bot BLE candidate.
class DiscoveredRobot {
  final String id;
  final String name;
  final int rssi;
  final List<String> serviceUuids;

  const DiscoveredRobot({
    required this.id,
    required this.name,
    required this.rssi,
    this.serviceUuids = const [],
  });

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is DiscoveredRobot &&
          runtimeType == other.runtimeType &&
          id == other.id;

  @override
  int get hashCode => id.hashCode;

  @override
  String toString() => 'DiscoveredRobot(id: $id, name: $name, rssi: $rssi dBm)';
}

/// Standardized error codes for BLE operations.
enum BleTransportErrorCode {
  bluetoothUnavailable,
  bluetoothDisabled,
  permissionDenied,
  permissionPermanentlyDenied,
  scanTimeout,
  noCompatibleRobotFound,
  connectionTimeout,
  connectionFailed,
  serviceNotFound,
  characteristicNotFound,
  writeFailed,
  readFailed,
  notificationFailed,
  disconnected,
  protocolTimeout,
  malformedResponse,
  unsupportedProtocolVersion,
  requestIdMismatch,
  deviceIdentityMismatch,
  payloadTooLarge,
  unknownTransportError,
}

/// Package-independent error representation for diagnostics and UI feedback.
class BleTransportError implements Exception {
  final BleTransportErrorCode code;
  final String message;
  final String? details;

  const BleTransportError({
    required this.code,
    required this.message,
    this.details,
  });

  @override
  String toString() =>
      'BleTransportError(${code.name}): $message${details != null ? ' ($details)' : ''}';
}
