import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:rbot_companion/features/remote/remote_screen.dart';
import 'package:rbot_companion/features/remote/motor_controller.dart';
import 'package:rbot_companion/data/ble/ble_transport.dart';
import 'package:rbot_companion/data/models/protocol_models.dart';

void main() {
  group('Capability-Driven UI Safety & Motor Locking Tests', () {
    late MockBleTransport mockTransport;

    setUp(() {
      mockTransport = MockBleTransport();
    });

    testWidgets('Displays warning banner and disables motion when motor capability is false', (tester) async {
      // Ensure motor capability is false
      mockTransport.setMockCapabilities(const DeviceCapabilities(motor: false));

      await tester.pumpWidget(
        ProviderScope(
          overrides: [
            bleTransportProvider.overrideWithValue(mockTransport),
          ],
          child: const MaterialApp(
            home: RemoteScreen(),
          ),
        ),
      );
      await tester.pumpAndSettle();

      // Verify the warning banner is displayed
      expect(find.textContaining('Motor controls disabled'), findsOneWidget);

      // Attempt to tap/hold FORWARD button
      final forwardFinder = find.text('FORWARD');
      expect(forwardFinder, findsOneWidget);
      await tester.tap(forwardFinder);
      await tester.pumpAndSettle();

      // Verify that motor state remains stationary and not moving
      final container = ProviderContainer(
        overrides: [
          bleTransportProvider.overrideWithValue(mockTransport),
        ],
      );
      addTearDown(container.dispose);
      expect(container.read(motorControllerProvider).isMoving, isFalse);
    });

    testWidgets('Warning banner is absent when motor capability is enabled', (tester) async {
      mockTransport.setMockCapabilities(const DeviceCapabilities(motor: true));

      await tester.pumpWidget(
        ProviderScope(
          overrides: [
            bleTransportProvider.overrideWithValue(mockTransport),
          ],
          child: const MaterialApp(
            home: RemoteScreen(),
          ),
        ),
      );
      await tester.pumpAndSettle();

      // Verify no disabled banner is present
      expect(find.textContaining('Motor controls disabled'), findsNothing);
    });
  });
}

