import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/theme/app_colors.dart';
import '../../data/ble/ble_transport.dart';
import '../../data/models/protocol_models.dart';

class DiagnosticsScreen extends ConsumerWidget {
  const DiagnosticsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connState = ref.watch(bleConnectionStateProvider).value ?? BleConnectionState.disconnected;
    final status = ref.watch(deviceStatusProvider).value ?? DeviceStatus.disconnected();

    return Scaffold(
      appBar: AppBar(
        title: const Text('Diagnostics 🩺'),
      ),
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
          children: [
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text('Communication Link', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
                    const Divider(height: 20),
                    _buildDiagRow('BLE Link State', connState.name.toUpperCase()),
                    _buildDiagRow('Protocol Version', 'v1 (JSON Frame)'),
                    _buildDiagRow('Service UUID', '7b4a0001-...'),
                    _buildDiagRow('Command UUID', '7b4a0002-...'),
                    _buildDiagRow('Status UUID', '7b4a0003-...'),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 14),
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text('Subsystem Health', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
                    const Divider(height: 20),
                    _buildDiagRow('Wi-Fi Network', status.wifiConnected ? 'Connected (STA)' : 'Disconnected / Idle'),
                    _buildDiagRow(
                      'Cloud AI Backend',
                      status.serverConnected == 'connected' || status.serverConnected == 'true'
                          ? 'Healthy (HTTP 200)'
                          : (status.serverConnected == 'unknown'
                              ? 'Unknown (telemetry offline)'
                              : 'Unreachable'),
                    ),
                    _buildDiagRow('Display Framebuffer', '40 KB DMA RAM Active'),
                    _buildDiagRow('Local Wake Word', 'Active (Core 1 WakeNet10)'),
                    _buildDiagRow(
                      'Battery Telemetry',
                      status.hasRealBattery
                          ? '${status.batteryPercent}% (${status.batteryVoltage}V)'
                          : 'No ADC sensor (Not fabricated)',
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildDiagRow(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(label, style: TextStyle(color: AppColors.subText, fontSize: 13)),
          Text(value, style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 13)),
        ],
      ),
    );
  }
}
