import 'dart:async';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../models/protocol_models.dart';
import 'ble_models.dart';
import 'reactive_ble_transport.dart';

export 'ble_models.dart';
export 'mock_ble_transport.dart';
export 'reactive_ble_transport.dart';

/// Abstract BLE Transport interface for R-bot Companion.
/// Package-independent boundary: Presentation, repositories, and controllers
/// depend solely on this abstraction and package-independent models.
abstract class BleTransport {
  Stream<BleConnectionState> get connectionStateStream;
  BleConnectionState get currentConnectionState;

  Stream<BleAdapterState> get adapterStateStream;
  BleAdapterState get currentAdapterState;

  Stream<List<DiscoveredRobot>> get discoveredRobotsStream;
  List<DiscoveredRobot> get discoveredRobots;

  Stream<DeviceStatus> get statusStream;
  Stream<String> get rawEventStream;

  Stream<DeviceCapabilities> get capabilitiesStream;
  DeviceCapabilities get currentCapabilities;

  Future<void> startScan({Duration timeout = const Duration(seconds: 8)});
  Future<void> stopScan();
  Future<void> connect(String deviceId, {Duration timeout = const Duration(seconds: 10)});
  Future<void> disconnect();
  Future<BleResponse> sendCommand(BleCommand command, {Duration timeout = const Duration(seconds: 5)});
}

/// Master Production BleTransport Provider.
/// Strict Rule: In production, always resolves to ReactiveBleTransport.
/// Never silently catches errors to fall back to MockBleTransport.
/// MockBleTransport is only injected via explicit ProviderScope override in tests.
final bleTransportProvider = Provider<BleTransport>((ref) {
  final transport = ReactiveBleTransport();
  ref.onDispose(() {
    transport.dispose();
  });
  return transport;
});

final bleConnectionStateProvider = StreamProvider<BleConnectionState>((ref) {
  final transport = ref.watch(bleTransportProvider);
  return transport.connectionStateStream;
});

final bleAdapterStateProvider = StreamProvider<BleAdapterState>((ref) {
  final transport = ref.watch(bleTransportProvider);
  return transport.adapterStateStream;
});

final discoveredRobotsProvider = StreamProvider<List<DiscoveredRobot>>((ref) {
  final transport = ref.watch(bleTransportProvider);
  return transport.discoveredRobotsStream;
});

final deviceStatusProvider = StreamProvider<DeviceStatus>((ref) {
  final transport = ref.watch(bleTransportProvider);
  return transport.statusStream;
});

final bleRawEventProvider = StreamProvider<String>((ref) {
  final transport = ref.watch(bleTransportProvider);
  return transport.rawEventStream;
});

final deviceCapabilitiesProvider = StreamProvider<DeviceCapabilities>((ref) {
  final transport = ref.watch(bleTransportProvider);
  return transport.capabilitiesStream;
});
