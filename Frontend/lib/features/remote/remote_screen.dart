import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/constants/ble_constants.dart';
import '../../core/theme/app_colors.dart';
import '../../data/ble/ble_transport.dart';
import '../../data/models/protocol_models.dart';
import 'motor_controller.dart';

class RemoteScreen extends ConsumerWidget {
  const RemoteScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final motorState = ref.watch(motorControllerProvider);
    final motorNotifier = ref.read(motorControllerProvider.notifier);
    final transport = ref.watch(bleTransportProvider);
    final capabilities = ref.watch(deviceCapabilitiesProvider).value ?? transport.currentCapabilities;
    final motorEnabled = capabilities.motor;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Remote Control 🎮'),
      ),
      body: SafeArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
          child: Column(
            children: [
              // Status Card
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(14),
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            motorState.isMoving
                                ? 'State: Moving (${motorState.activeDirection.wireName})'
                                : 'State: Stationary / Stop',
                            style: TextStyle(
                              fontWeight: FontWeight.bold,
                              color: motorState.isMoving ? AppColors.stopRed : AppColors.darkNavy,
                            ),
                          ),
                          const SizedBox(height: 4),
                          Text(
                            'Safety Lease: 500 ms active watchdog',
                            style: TextStyle(fontSize: 12, color: AppColors.subText),
                          ),
                        ],
                      ),
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
                        decoration: BoxDecoration(
                          color: motorState.isMoving ? AppColors.lightBlush : AppColors.warmMilkWhite,
                          borderRadius: BorderRadius.circular(10),
                        ),
                        child: Text(
                          '${motorState.speed}% PWM',
                          style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 13),
                        ),
                      ),
                    ],
                  ),
                ),
              ),

              // Capability Safety Warning Banner
              if (!motorEnabled)
                Container(
                  margin: const EdgeInsets.only(top: 10),
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: Colors.amber.shade100,
                    borderRadius: BorderRadius.circular(12),
                    border: Border.all(color: Colors.amber.shade400),
                  ),
                  child: const Row(
                    children: [
                      Icon(Icons.lock, color: Colors.orange, size: 22),
                      SizedBox(width: 10),
                      Expanded(
                        child: Text(
                          'Motor controls disabled: Firmware capability reports motor motion is inactive.',
                          style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.bold,
                            color: AppColors.darkNavy,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),

              if (motorState.lastError != null)
                Padding(
                  padding: const EdgeInsets.only(top: 8),
                  child: Text(
                    '⚠️ ${motorState.lastError}',
                    style: const TextStyle(color: AppColors.stopRed, fontSize: 13),
                  ),
                ),

              const SizedBox(height: 20),

              // Directional D-Pad Controls (Press-and-Hold Dead-man behavior)
              Column(
                children: [
                  // Forward
                  _buildControlBtn(
                    direction: MotorDirection.forward,
                    icon: Icons.keyboard_arrow_up,
                    label: 'FORWARD',
                    motorNotifier: motorNotifier,
                    enabled: motorEnabled,
                  ),
                  const SizedBox(height: 12),
                  // Left / Right / Pivot
                  Row(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      _buildControlBtn(
                        direction: MotorDirection.turnLeft,
                        icon: Icons.keyboard_arrow_left,
                        label: 'LEFT',
                        motorNotifier: motorNotifier,
                        enabled: motorEnabled,
                      ),
                      const SizedBox(width: 14),
                      // Emergency STOP Button in center of D-Pad
                      _buildStopBtn(motorNotifier, enabled: motorEnabled),
                      const SizedBox(width: 14),
                      _buildControlBtn(
                        direction: MotorDirection.turnRight,
                        icon: Icons.keyboard_arrow_right,
                        label: 'RIGHT',
                        motorNotifier: motorNotifier,
                        enabled: motorEnabled,
                      ),
                    ],
                  ),
                  const SizedBox(height: 12),
                  // Backward
                  _buildControlBtn(
                    direction: MotorDirection.backward,
                    icon: Icons.keyboard_arrow_down,
                    label: 'BACKWARD',
                    motorNotifier: motorNotifier,
                    enabled: motorEnabled,
                  ),
                ],
              ),

              const SizedBox(height: 16),

              // Pivot Row
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  _buildPivotBtn(
                    direction: MotorDirection.pivotLeft,
                    label: '⟲ Pivot L',
                    motorNotifier: motorNotifier,
                    enabled: motorEnabled,
                  ),
                  const SizedBox(width: 16),
                  _buildPivotBtn(
                    direction: MotorDirection.pivotRight,
                    label: 'Pivot R ⟳',
                    motorNotifier: motorNotifier,
                    enabled: motorEnabled,
                  ),
                ],
              ),

              const SizedBox(height: 20),

              // Speed Slider
              Card(
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('Speed Limit:', style: TextStyle(fontWeight: FontWeight.bold)),
                          Text('${motorState.speed}% (Safe Cap: ${BleConstants.maxSafeSpeed}%)'),
                        ],
                      ),
                      Slider(
                        value: motorState.speed.toDouble(),
                        min: BleConstants.minSpeed.toDouble(),
                        max: BleConstants.maxSafeSpeed.toDouble(),
                        divisions: 12,
                        activeColor: motorEnabled ? AppColors.pastelPink : Colors.grey,
                        inactiveColor: AppColors.cardBorder,
                        onChanged: motorEnabled ? (val) => motorNotifier.setSpeed(val.round()) : null,
                      ),
                    ],
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildControlBtn({
    required MotorDirection direction,
    required IconData icon,
    required String label,
    required MotorController motorNotifier,
    required bool enabled,
  }) {
    return Listener(
      onPointerDown: enabled ? (_) => motorNotifier.startMove(direction) : null,
      onPointerUp: enabled ? (_) => motorNotifier.stop() : null,
      onPointerCancel: enabled ? (_) => motorNotifier.stop() : null,
      child: Container(
        width: 76,
        height: 76,
        decoration: BoxDecoration(
          color: enabled ? AppColors.lavender : Colors.grey.shade200,
          borderRadius: BorderRadius.circular(22),
          border: Border.all(color: Colors.white, width: 2),
          boxShadow: enabled
              ? [
                  BoxShadow(
                    color: AppColors.lavender.withAlpha(120),
                    blurRadius: 8,
                    offset: const Offset(0, 4),
                  ),
                ]
              : null,
        ),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(icon, size: 36, color: enabled ? AppColors.darkNavy : Colors.grey.shade400),
            Text(
              label,
              style: TextStyle(
                fontSize: 9,
                fontWeight: FontWeight.bold,
                color: enabled ? AppColors.darkNavy : Colors.grey.shade400,
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildStopBtn(MotorController motorNotifier, {required bool enabled}) {
    return GestureDetector(
      onTap: enabled ? () => motorNotifier.stop() : null,
      child: Container(
        width: 76,
        height: 76,
        decoration: BoxDecoration(
          color: enabled ? AppColors.stopRed : Colors.grey.shade300,
          shape: BoxShape.circle,
          border: Border.all(color: Colors.white, width: 3),
          boxShadow: enabled
              ? [
                  BoxShadow(
                    color: AppColors.stopRed.withAlpha(150),
                    blurRadius: 10,
                    offset: const Offset(0, 4),
                  ),
                ]
              : null,
        ),
        child: Center(
          child: Text(
            'STOP',
            style: TextStyle(
              color: enabled ? Colors.white : Colors.grey.shade500,
              fontWeight: FontWeight.w900,
              fontSize: 16,
              letterSpacing: 1.0,
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildPivotBtn({
    required MotorDirection direction,
    required String label,
    required MotorController motorNotifier,
    required bool enabled,
  }) {
    return Listener(
      onPointerDown: enabled ? (_) => motorNotifier.startMove(direction) : null,
      onPointerUp: enabled ? (_) => motorNotifier.stop() : null,
      onPointerCancel: enabled ? (_) => motorNotifier.stop() : null,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
        decoration: BoxDecoration(
          color: enabled ? AppColors.softSkyBlue : Colors.grey.shade200,
          borderRadius: BorderRadius.circular(16),
          border: Border.all(color: Colors.white, width: 1.5),
        ),
        child: Text(
          label,
          style: TextStyle(
            fontWeight: FontWeight.bold,
            color: enabled ? AppColors.darkNavy : Colors.grey.shade400,
            fontSize: 13,
          ),
        ),
      ),
    );
  }
}

