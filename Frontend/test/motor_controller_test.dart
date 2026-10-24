import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:rbot_companion/features/remote/motor_controller.dart';
import 'package:rbot_companion/data/ble/ble_transport.dart';
import 'package:rbot_companion/data/models/protocol_models.dart';
import 'package:rbot_companion/core/constants/ble_constants.dart';

void main() {
  group('Motor Controller & Dead-man Safety Tests', () {
    late ProviderContainer container;
    late MockBleTransport transport;

    setUp(() {
      transport = MockBleTransport();
      container = ProviderContainer(
        overrides: [
          bleTransportProvider.overrideWithValue(transport),
        ],
      );
    });

    tearDown(() {
      container.dispose();
    });

    test('Initial motor state is stopped with default speed', () {
      final state = container.read(motorControllerProvider);
      expect(state.isMoving, isFalse);
      expect(state.activeDirection, equals(MotorDirection.stop));
      expect(state.speed, equals(BleConstants.defaultSpeed));
    });

    test('Clamps speed to safe bounds', () {
      final notifier = container.read(motorControllerProvider.notifier);
      notifier.setSpeed(120); // Above safe max (80)
      expect(container.read(motorControllerProvider).speed, equals(BleConstants.maxSafeSpeed));

      notifier.setSpeed(5); // Below min
      expect(container.read(motorControllerProvider).speed, equals(BleConstants.minSpeed));
    });

    test('Blocks movement when motor capability is false (default safety)', () async {
      final notifier = container.read(motorControllerProvider.notifier);
      // Ensure motor capability is false
      expect(transport.currentCapabilities.motor, isFalse);

      await notifier.startMove(MotorDirection.forward);

      final state = container.read(motorControllerProvider);
      expect(state.isMoving, isFalse);
      expect(state.lastError, contains('disabled'));
    });

    test('Allows movement and halts cleanly when motor capability is explicitly enabled', () async {
      transport.setMockCapabilities(const DeviceCapabilities(motor: true));
      expect(transport.currentCapabilities.motor, isTrue);

      final notifier = container.read(motorControllerProvider.notifier);
      await notifier.startMove(MotorDirection.forward);

      expect(container.read(motorControllerProvider).isMoving, isTrue);
      expect(container.read(motorControllerProvider).activeDirection, equals(MotorDirection.forward));

      await notifier.stop();
      expect(container.read(motorControllerProvider).isMoving, isFalse);
      expect(container.read(motorControllerProvider).activeDirection, equals(MotorDirection.stop));
    });
  });
}
