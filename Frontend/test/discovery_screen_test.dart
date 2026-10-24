import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:rbot_companion/core/constants/ble_constants.dart';
import 'package:rbot_companion/data/ble/ble_transport.dart';
import 'package:rbot_companion/features/discovery/discovery_screen.dart';

void main() {
  group('DiscoveryScreen Widget Tests', () {
    late MockBleTransport transport;

    setUp(() {
      transport = MockBleTransport();
    });

    tearDown(() {
      transport.dispose();
    });

    testWidgets('Renders DiscoveryScreen with target service UUID and scan button', (tester) async {
      await tester.pumpWidget(
        ProviderScope(
          overrides: [
            bleTransportProvider.overrideWithValue(transport),
          ],
          child: const MaterialApp(
            home: DiscoveryScreen(),
          ),
        ),
      );
      await tester.pumpAndSettle();

      expect(find.text('Pair R-bot 🔍'), findsOneWidget);
      expect(find.text('Target Service UUID'), findsOneWidget);
      expect(find.text(BleConstants.serviceUuid), findsOneWidget);
      expect(find.text('Scan Again'), findsOneWidget);
    });

    testWidgets('Displays discovered device after starting scan', (tester) async {
      await tester.pumpWidget(
        ProviderScope(
          overrides: [
            bleTransportProvider.overrideWithValue(transport),
          ],
          child: const MaterialApp(
            home: DiscoveryScreen(),
          ),
        ),
      );
      await tester.pumpAndSettle();

      // Trigger scan via transport
      await transport.startScan();
      await tester.pumpAndSettle();

      expect(find.text('Rbot-Mochi (Simulated)'), findsOneWidget);
      expect(find.text('Connect 🔗'), findsOneWidget);
    });
  });
}

