import 'dart:convert';

/// Direction enumeration matching DRV8833 firmware motion modes
enum MotorDirection {
  forward,
  backward,
  turnLeft,
  turnRight,
  pivotLeft,
  pivotRight,
  stop,
}

extension MotorDirectionExt on MotorDirection {
  String get wireName {
    switch (this) {
      case MotorDirection.forward:
        return 'forward';
      case MotorDirection.backward:
        return 'backward';
      case MotorDirection.turnLeft:
        return 'turn_left';
      case MotorDirection.turnRight:
        return 'turn_right';
      case MotorDirection.pivotLeft:
        return 'pivot_left';
      case MotorDirection.pivotRight:
        return 'pivot_right';
      case MotorDirection.stop:
        return 'stop';
    }
  }

  static MotorDirection fromWireName(String name) {
    switch (name) {
      case 'forward':
        return MotorDirection.forward;
      case 'backward':
        return MotorDirection.backward;
      case 'turn_left':
        return MotorDirection.turnLeft;
      case 'turn_right':
        return MotorDirection.turnRight;
      case 'pivot_left':
        return MotorDirection.pivotLeft;
      case 'pivot_right':
        return MotorDirection.pivotRight;
      case 'stop':
      default:
        return MotorDirection.stop;
    }
  }
}

/// Outgoing Command Payload (Protocol v1)
class BleCommand {
  final int v;
  final int id;
  final String type;
  final String action;
  final Map<String, dynamic> payload;

  BleCommand({
    this.v = 1,
    required this.id,
    required this.type,
    required this.action,
    this.payload = const {},
  });

  Map<String, dynamic> toJson() {
    return {
      'v': v,
      'id': id,
      'type': type,
      'action': action,
      ...payload,
    };
  }

  String encode() => jsonEncode(toJson());

  static BleCommand motorMove({
    required int id,
    required MotorDirection direction,
    required int speed,
    int ttlMs = 500,
  }) {
    return BleCommand(
      id: id,
      type: 'motor',
      action: direction == MotorDirection.stop ? 'stop' : 'move',
      payload: {
        'direction': direction.wireName,
        'speed': speed,
        'ttl_ms': ttlMs,
      },
    );
  }

  static BleCommand setConfig({
    required int id,
    String? deviceName,
    String? watchFace,
    int? maxMotorSpeed,
  }) {
    final map = <String, dynamic>{};
    if (deviceName != null) map['device_name'] = deviceName;
    if (watchFace != null) map['watch_face'] = watchFace;
    if (maxMotorSpeed != null) map['max_motor_speed'] = maxMotorSpeed;

    return BleCommand(
      id: id,
      type: 'config',
      action: 'set',
      payload: map,
    );
  }

  static BleCommand getInfo({required int id}) {
    return BleCommand(id: id, type: 'system', action: 'get_info');
  }

  static BleCommand getStatus({required int id}) {
    return BleCommand(id: id, type: 'system', action: 'get_status');
  }

  static BleCommand getCapabilities({required int id}) {
    return BleCommand(id: id, type: 'system', action: 'get_capabilities');
  }

  static BleCommand setWatchFace({required int id, required String themeId}) {
    return BleCommand(
      id: id,
      type: 'face',
      action: 'set_theme',
      payload: {'theme': themeId},
    );
  }

  static BleCommand startOta({
    required int id,
    required String url,
    required String sha256,
    required int size,
  }) {
    return BleCommand(
      id: id,
      type: 'ota',
      action: 'start',
      payload: {
        'url': url,
        'sha256': sha256,
        'size': size,
      },
    );
  }
}

/// Incoming Response Packet (Protocol v1)
class BleResponse {
  final int v;
  final int id;
  final bool ok;
  final String? state;
  final String? errorCode;
  final String? errorMessage;
  final Map<String, dynamic> data;

  BleResponse({
    required this.v,
    required this.id,
    required this.ok,
    this.state,
    this.errorCode,
    this.errorMessage,
    this.data = const {},
  });

  factory BleResponse.fromJson(Map<String, dynamic> json) {
    final v = json['v'] as int? ?? 1;
    final id = json['id'] as int? ?? 0;
    final ok = json['ok'] as bool? ?? false;
    final state = json['state'] as String?;
    final errCode = json['error_code'] as String?;
    final errMsg = json['error_message'] as String?;

    final data = Map<String, dynamic>.from(json);
    data.remove('v');
    data.remove('id');
    data.remove('ok');
    data.remove('state');
    data.remove('error_code');
    data.remove('error_message');

    return BleResponse(
      v: v,
      id: id,
      ok: ok,
      state: state,
      errorCode: errCode,
      errorMessage: errMsg,
      data: data,
    );
  }

  static BleResponse decode(String rawJson) {
    final decoded = jsonDecode(rawJson) as Map<String, dynamic>;
    return BleResponse.fromJson(decoded);
  }
}

/// Truthful Hardware & Firmware Capabilities
class DeviceCapabilities {
  final bool motor;
  final bool face;
  final bool voice;
  final bool ota;
  final bool batteryAdc;
  final bool configWrite;
  final bool watchFaceWrite;
  final bool wifiProvisioning;
  final bool wifiOta;
  final bool rollback;

  const DeviceCapabilities({
    this.motor = false,
    this.face = false,
    this.voice = false,
    this.ota = false,
    this.batteryAdc = false,
    this.configWrite = false,
    this.watchFaceWrite = false,
    this.wifiProvisioning = false,
    this.wifiOta = false,
    this.rollback = false,
  });

  factory DeviceCapabilities.none() => const DeviceCapabilities();

  factory DeviceCapabilities.fromJson(Map<String, dynamic> json) {
    return DeviceCapabilities(
      motor: json['motor'] as bool? ?? false,
      face: json['face'] as bool? ?? false,
      voice: json['voice'] as bool? ?? false,
      ota: json['ota'] as bool? ?? false,
      batteryAdc: json['battery_adc'] as bool? ?? false,
      configWrite: json['config_write'] as bool? ?? false,
      watchFaceWrite: json['watch_face_write'] as bool? ?? false,
      wifiProvisioning: json['wifi_provisioning'] as bool? ?? false,
      wifiOta: json['wifi_ota'] as bool? ?? false,
      rollback: json['rollback'] as bool? ?? false,
    );
  }
}

/// Real Telemetry & Status Packet
class DeviceStatus {
  final String robotName;
  final String firmwareVersion;
  final String hardwareId;
  final String activeFace;
  final String movementState;
  final int? batteryPercent;
  final double? batteryVoltage;
  final bool hasRealBattery;
  final bool wifiConnected;
  final String serverConnected;
  final bool otaSupported;

  const DeviceStatus({
    required this.robotName,
    required this.firmwareVersion,
    required this.hardwareId,
    required this.activeFace,
    required this.movementState,
    this.batteryPercent,
    this.batteryVoltage,
    required this.hasRealBattery,
    required this.wifiConnected,
    required this.serverConnected,
    required this.otaSupported,
  });

  factory DeviceStatus.disconnected() {
    return const DeviceStatus(
      robotName: 'R-bot (Offline)',
      firmwareVersion: 'Unknown',
      hardwareId: 'Unknown',
      activeFace: 'unavailable',
      movementState: 'unavailable',
      batteryPercent: null,
      batteryVoltage: null,
      hasRealBattery: false,
      wifiConnected: false,
      serverConnected: 'unknown',
      otaSupported: false,
    );
  }

  factory DeviceStatus.fromJson(Map<String, dynamic> json) {
    // Master Prompt: "Display battery information only when a real measurement or reliable estimate exists"
    final hasRealBat = json['has_real_battery'] as bool? ?? false;
    final batPct = hasRealBat ? (json['battery_percent'] as int?) : null;
    final batVolt = hasRealBat ? (json['battery_voltage'] as num?)?.toDouble() : null;

    final serverRaw = json['server_connected'];
    final String serverStatus;
    if (serverRaw is bool) {
      serverStatus = serverRaw ? 'connected' : 'unreachable';
    } else if (serverRaw is String) {
      serverStatus = serverRaw;
    } else {
      serverStatus = 'unknown';
    }

    return DeviceStatus(
      robotName: json['robot_name'] as String? ?? 'R-bot',
      firmwareVersion: json['firmware_version'] as String? ?? 'v1.0.0',
      hardwareId: json['hardware_id'] as String? ?? 'ESP32-S3-N16R8',
      activeFace: json['active_face'] as String? ?? 'unavailable',
      movementState: json['movement_state'] as String? ?? 'unavailable',
      batteryPercent: batPct,
      batteryVoltage: batVolt,
      hasRealBattery: hasRealBat,
      wifiConnected: json['wifi_connected'] as bool? ?? false,
      serverConnected: serverStatus,
      otaSupported: json['ota_supported'] as bool? ?? false,
    );
  }
}
