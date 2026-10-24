import 'dart:io';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:rbot_companion/data/ble/ble_transport.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUpAll(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(
      const MethodChannel('flutter_reactive_ble_method'),
      (MethodCall call) async {
        return null;
      },
    );
  });

  group('Transport Architecture & Separation Tests', () {
    test('Production bleTransportProvider resolves to ReactiveBleTransport', () {
      final container = ProviderContainer();
      addTearDown(container.dispose);

      final transport = container.read(bleTransportProvider);
      expect(transport, isA<ReactiveBleTransport>());
    });

    test('Production bleTransportProvider NEVER resolves to MockBleTransport', () {
      final container = ProviderContainer();
      addTearDown(container.dispose);

      final transport = container.read(bleTransportProvider);
      expect(transport is MockBleTransport, isFalse);
    });

    test('Tests can explicitly override bleTransportProvider with MockBleTransport', () {
      final mock = MockBleTransport();
      addTearDown(mock.dispose);

      final container = ProviderContainer(
        overrides: [
          bleTransportProvider.overrideWithValue(mock),
        ],
      );
      addTearDown(container.dispose);

      final transport = container.read(bleTransportProvider);
      expect(transport, isA<MockBleTransport>());
      expect(identical(transport, mock), isTrue);
    });

    test('Zero UI or controller files directly import flutter_reactive_ble', () {
      final libDir = Directory('lib');
      final dartFiles = libDir
          .listSync(recursive: true)
          .whereType<File>()
          .where((f) => f.path.endsWith('.dart'));

      final offendingFiles = <String>[];
      for (final file in dartFiles) {
        // Only reactive_ble_transport.dart is permitted to import flutter_reactive_ble
        if (file.path.endsWith('reactive_ble_transport.dart')) {
          continue;
        }
        final content = file.readAsStringSync();
        if (content.contains('package:flutter_reactive_ble') ||
            content.contains('flutter_reactive_ble/')) {
          offendingFiles.add(file.path);
        }
      }

      expect(
        offendingFiles,
        isEmpty,
        reason: 'Third-party BLE plugin types leaked into: $offendingFiles',
      );
    });

    test('Zero files in lib or test import flutter_blue_plus', () {
      final projectDir = Directory('.');
      final dartFiles = projectDir
          .listSync(recursive: true)
          .whereType<File>()
          .where((f) => f.path.endsWith('.dart') && (f.path.startsWith('./lib') || f.path.startsWith('./test') || f.path.startsWith('lib') || f.path.startsWith('test')));

      final offendingFiles = <String>[];
      for (final file in dartFiles) {
        if (file.path.endsWith('transport_architecture_test.dart')) {
          continue;
        }
        final content = file.readAsStringSync();
        if (content.contains('flutter_blue_plus')) {
          offendingFiles.add(file.path);
        }
      }

      expect(
        offendingFiles,
        isEmpty,
        reason: 'Legacy flutter_blue_plus remains in: $offendingFiles',
      );
    });
  });
}
