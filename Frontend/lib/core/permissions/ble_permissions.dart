import 'package:flutter/services.dart';

enum BlePermissionStatus {
  granted,
  denied,
  permanentlyDenied,
  unknown,
}

class BlePermissionResult {
  final BlePermissionStatus status;
  final List<String> missingPermissions;

  const BlePermissionResult({
    required this.status,
    this.missingPermissions = const [],
  });

  bool get isGranted => status == BlePermissionStatus.granted;
  bool get isPermanentlyDenied => status == BlePermissionStatus.permanentlyDenied;
}

class BlePermissionService {
  static const MethodChannel _channel =
      MethodChannel('com.rbot.rbot_companion/ble_permissions');

  static BlePermissionStatus _parseStatus(String? statusStr) {
    switch (statusStr) {
      case 'granted':
        return BlePermissionStatus.granted;
      case 'permanently_denied':
        return BlePermissionStatus.permanentlyDenied;
      case 'denied':
        return BlePermissionStatus.denied;
      default:
        return BlePermissionStatus.unknown;
    }
  }

  static Future<BlePermissionResult> checkPermissions() async {
    try {
      final res = await _channel.invokeMethod<Map<dynamic, dynamic>>('checkPermissions');
      if (res == null) {
        return const BlePermissionResult(status: BlePermissionStatus.unknown);
      }
      final statusStr = res['status'] as String?;
      final missingList = (res['missing'] as List<dynamic>?)?.cast<String>() ?? [];
      return BlePermissionResult(
        status: _parseStatus(statusStr),
        missingPermissions: missingList,
      );
    } on MissingPluginException {
      // Non-Android platforms or testing mock environments
      return const BlePermissionResult(status: BlePermissionStatus.granted);
    } catch (_) {
      return const BlePermissionResult(status: BlePermissionStatus.unknown);
    }
  }

  static Future<BlePermissionResult> requestPermissions() async {
    try {
      final res = await _channel.invokeMethod<Map<dynamic, dynamic>>('requestPermissions');
      if (res == null) {
        return const BlePermissionResult(status: BlePermissionStatus.unknown);
      }
      final statusStr = res['status'] as String?;
      final missingList = (res['missing'] as List<dynamic>?)?.cast<String>() ?? [];
      return BlePermissionResult(
        status: _parseStatus(statusStr),
        missingPermissions: missingList,
      );
    } on MissingPluginException {
      // Non-Android platforms or testing mock environments
      return const BlePermissionResult(status: BlePermissionStatus.granted);
    } catch (_) {
      return const BlePermissionResult(status: BlePermissionStatus.unknown);
    }
  }

  static Future<bool> openAppSettings() async {
    try {
      final res = await _channel.invokeMethod<bool>('openAppSettings');
      return res ?? false;
    } catch (_) {
      return false;
    }
  }
}

