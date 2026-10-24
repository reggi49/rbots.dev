import 'dart:async';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/constants/ble_constants.dart';
import '../../data/ble/ble_transport.dart';
import '../../data/models/protocol_models.dart';

class MotorState {
  final MotorDirection activeDirection;
  final int speed;
  final bool isMoving;
  final String? lastError;

  const MotorState({
    this.activeDirection = MotorDirection.stop,
    this.speed = BleConstants.defaultSpeed,
    this.isMoving = false,
    this.lastError,
  });

  MotorState copyWith({
    MotorDirection? activeDirection,
    int? speed,
    bool? isMoving,
    String? lastError,
  }) {
    return MotorState(
      activeDirection: activeDirection ?? this.activeDirection,
      speed: speed ?? this.speed,
      isMoving: isMoving ?? this.isMoving,
      lastError: lastError,
    );
  }
}

class MotorController extends Notifier<MotorState> {
  final BleTransport? _customTransport;
  Timer? _heartbeatTimer;
  int _commandSeq = 1;

  MotorController([this._customTransport]);

  BleTransport get _transport => _customTransport ?? ref.read(bleTransportProvider);

  @override
  MotorState build() {
    ref.onDispose(() {
      _heartbeatTimer?.cancel();
    });
    return const MotorState();
  }

  void setSpeed(int newSpeed) {
    final clamped = newSpeed.clamp(BleConstants.minSpeed, BleConstants.maxSafeSpeed);
    state = state.copyWith(speed: clamped);
  }

  /// Triggered on PointerDown (Press-and-hold dead-man behavior)
  Future<void> startMove(MotorDirection direction) async {
    if (direction == MotorDirection.stop) {
      await stop();
      return;
    }

    // Safety check: block if motor capability is not supported by firmware
    if (!_transport.currentCapabilities.motor) {
      state = state.copyWith(
        lastError: 'Motor motion is disabled: firmware reports motor capability is inactive',
        isMoving: false,
      );
      return;
    }

    _heartbeatTimer?.cancel();
    state = state.copyWith(
      activeDirection: direction,
      isMoving: true,
      lastError: null,
    );

    await _sendMovePacket(direction, state.speed);

    // Heartbeat renewal loop every 250ms (well within the 500ms firmware lease)
    _heartbeatTimer = Timer.periodic(
      const Duration(milliseconds: BleConstants.heartbeatIntervalMs),
      (_) => _sendMovePacket(direction, state.speed),
    );
  }

  /// Triggered on PointerUp / PointerCancel / Focus Loss
  Future<void> stop() async {
    _heartbeatTimer?.cancel();
    _heartbeatTimer = null;

    state = state.copyWith(
      activeDirection: MotorDirection.stop,
      isMoving: false,
    );

    if (!_transport.currentCapabilities.motor) {
      return;
    }

    // Immediate STOP command has highest priority
    final cmd = BleCommand.motorMove(
      id: _commandSeq++,
      direction: MotorDirection.stop,
      speed: 0,
      ttlMs: 0,
    );
    try {
      await _transport.sendCommand(cmd);
    } catch (e) {
      state = state.copyWith(lastError: e.toString());
    }
  }

  Future<void> _sendMovePacket(MotorDirection direction, int speed) async {
    if (!_transport.currentCapabilities.motor) {
      return;
    }

    final cmd = BleCommand.motorMove(
      id: _commandSeq++,
      direction: direction,
      speed: speed,
      ttlMs: BleConstants.movementTtlMs,
    );
    try {
      final res = await _transport.sendCommand(cmd);
      if (!res.ok) {
        state = state.copyWith(lastError: res.errorMessage ?? 'Command rejected');
      }
    } catch (e) {
      state = state.copyWith(lastError: e.toString());
    }
  }

  void dispose() {
    _heartbeatTimer?.cancel();
  }
}

final motorControllerProvider = NotifierProvider<MotorController, MotorState>(() {
  return MotorController();
});
