import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:rbot_companion/main.dart';
import 'package:rbot_companion/features/home/home_screen.dart';
import 'package:rbot_companion/data/ble/ble_transport.dart';

void main() {
  testWidgets('RbotApp boots and renders HomeScreen with Kawaii companion cards', (WidgetTester tester) async {
    await tester.pumpWidget(
      ProviderScope(
        overrides: [
          bleTransportProvider.overrideWithValue(MockBleTransport()),
        ],
        child: const RbotApp(),
      ),
    );
    await tester.pumpAndSettle();

    expect(find.byType(HomeScreen), findsOneWidget);
    expect(find.text('R-bot Companion 🐱'), findsOneWidget);
    expect(find.text('Remote Control'), findsOneWidget);
    expect(find.text('Watch Face Studio'), findsOneWidget);
    expect(find.text('Firmware & Wi-Fi OTA'), findsOneWidget);
  });
}
