import 'dart:async';
import 'package:flutter_test/flutter_test.dart';
import 'package:rbot_companion/data/models/protocol_models.dart';

/// Standalone test verifying the response coordination logic and contract
class RequestCoordinator {
  Completer<BleResponse>? _pendingCompleter;
  int? _pendingId;
  Timer? _timer;

  bool get hasInFlight => _pendingCompleter != null && !_pendingCompleter!.isCompleted;

  Future<BleResponse> dispatch(
    BleCommand command, {
    Duration timeout = const Duration(seconds: 2),
    required Future<void> Function(BleCommand cmd) onWrite,
  }) async {
    if (hasInFlight) {
      return BleResponse(
        v: command.v,
        id: command.id,
        ok: false,
        errorCode: 'BUSY',
        errorMessage: 'Another request is currently in flight',
      );
    }

    final completer = Completer<BleResponse>();
    _pendingCompleter = completer;
    _pendingId = command.id;

    _timer = Timer(timeout, () {
      if (_pendingId == command.id) {
        _pendingCompleter = null;
        _pendingId = null;
        _timer = null;
        if (!completer.isCompleted) {
          completer.complete(BleResponse(
            v: command.v,
            id: command.id,
            ok: false,
            errorCode: 'TIMEOUT',
            errorMessage: 'Timed out',
          ));
        }
      }
    });

    try {
      await onWrite(command);
      return await completer.future;
    } catch (e) {
      _timer?.cancel();
      _timer = null;
      _pendingCompleter = null;
      _pendingId = null;
      return BleResponse(
        v: command.v,
        id: command.id,
        ok: false,
        errorCode: 'TRANSPORT_ERROR',
        errorMessage: e.toString(),
      );
    }
  }

  void onNotification(Map<String, dynamic> json) {
    final id = json['id'] as int?;
    if (id != null && _pendingId != null && id == _pendingId) {
      _timer?.cancel();
      _timer = null;
      final comp = _pendingCompleter;
      _pendingCompleter = null;
      _pendingId = null;
      if (comp != null && !comp.isCompleted) {
        comp.complete(BleResponse.fromJson(json));
      }
    }
    // Mismatched or unknown IDs are safely dropped without affecting _pendingCompleter
  }

  void onDisconnect() {
    _timer?.cancel();
    _timer = null;
    if (_pendingCompleter != null && !_pendingCompleter!.isCompleted) {
      _pendingCompleter!.complete(BleResponse(
        v: 1,
        id: _pendingId ?? 0,
        ok: false,
        errorCode: 'DISCONNECTED',
        errorMessage: 'Device disconnected',
      ));
    }
    _pendingCompleter = null;
    _pendingId = null;
  }
}

void main() {
  group('Response Routing & Request Coordinator Unit Tests', () {
    late RequestCoordinator coordinator;

    setUp(() {
      coordinator = RequestCoordinator();
    });

    test('Dispatches command and resolves when notification arrives with matching ID', () async {
      final cmd = BleCommand.getInfo(id: 42);
      bool writeCalled = false;

      final future = coordinator.dispatch(
        cmd,
        onWrite: (c) async {
          writeCalled = true;
          // Simulate peripheral responding asynchronously after 20ms
          Future.delayed(const Duration(milliseconds: 20), () {
            coordinator.onNotification({
              'v': 1,
              'id': 42,
              'ok': true,
              'robot_name': 'Rbot-DEV',
            });
          });
        },
      );

      final resp = await future;
      expect(writeCalled, isTrue);
      expect(resp.ok, isTrue);
      expect(resp.id, equals(42));
      expect(resp.data['robot_name'], equals('Rbot-DEV'));
      expect(coordinator.hasInFlight, isFalse);
    });

    test('Rejects concurrent command with BUSY error when one is already in flight', () async {
      final cmd1 = BleCommand.getStatus(id: 100);
      final cmd2 = BleCommand.getInfo(id: 101);

      // Start cmd1 (does not complete immediately)
      final future1 = coordinator.dispatch(cmd1, onWrite: (_) async {});

      // Attempt cmd2 immediately
      final resp2 = await coordinator.dispatch(cmd2, onWrite: (_) async {});

      expect(resp2.ok, isFalse);
      expect(resp2.errorCode, equals('BUSY'));
      expect(resp2.id, equals(101));

      // Now complete cmd1
      coordinator.onNotification({'v': 1, 'id': 100, 'ok': true});
      final resp1 = await future1;
      expect(resp1.ok, isTrue);
      expect(resp1.id, equals(100));
    });

    test('Ignores unsolicited or mismatched notifications without corrupting pending request', () async {
      final cmd = BleCommand.getStatus(id: 200);

      final future = coordinator.dispatch(
        cmd,
        onWrite: (_) async {
          // Send mismatched notifications first
          coordinator.onNotification({'v': 1, 'id': 999, 'ok': true});
          coordinator.onNotification({'v': 1, 'id': 555, 'ok': false});

          // Then send correct matching notification
          coordinator.onNotification({'v': 1, 'id': 200, 'ok': true, 'active_face': 'unavailable'});
        },
      );

      final resp = await future;
      expect(resp.ok, isTrue);
      expect(resp.id, equals(200));
      expect(resp.data['active_face'], equals('unavailable'));
    });

    test('Times out cleanly and resets coordinator state', () async {
      final cmd = BleCommand.getStatus(id: 300);

      final resp = await coordinator.dispatch(
        cmd,
        timeout: const Duration(milliseconds: 50),
        onWrite: (_) async {
          // No notification arrives
        },
      );

      expect(resp.ok, isFalse);
      expect(resp.errorCode, equals('TIMEOUT'));
      expect(resp.id, equals(300));
      expect(coordinator.hasInFlight, isFalse);
    });

    test('Fails pending request cleanly with DISCONNECTED when device disconnects', () async {
      final cmd = BleCommand.getStatus(id: 400);

      final future = coordinator.dispatch(
        cmd,
        onWrite: (_) async {
          Future.delayed(const Duration(milliseconds: 20), () {
            coordinator.onDisconnect();
          });
        },
      );

      final resp = await future;
      expect(resp.ok, isFalse);
      expect(resp.errorCode, equals('DISCONNECTED'));
      expect(resp.id, equals(400));
      expect(coordinator.hasInFlight, isFalse);
    });
  });
}

