import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/theme/app_colors.dart';
import '../../data/ble/ble_transport.dart';
import '../../data/models/protocol_models.dart';
import '../diagnostics/diagnostics_screen.dart';
import '../discovery/discovery_screen.dart';
import '../remote/remote_screen.dart';
import '../settings/settings_screen.dart';
import '../watchface/watch_face_studio_screen.dart';
import '../../shared/widgets/kawaii_cat_illustration.dart';

class HomeScreen extends ConsumerWidget {
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connState = ref.watch(bleConnectionStateProvider).value ?? BleConnectionState.disconnected;
    final status = ref.watch(deviceStatusProvider).value ?? DeviceStatus.disconnected();
    final isConnected = connState == BleConnectionState.connected;

    return Scaffold(
      appBar: AppBar(
        title: const Text('R-bot Companion 🐱'),
        actions: [
          IconButton(
            icon: const Icon(Icons.medical_services_outlined),
            tooltip: 'Diagnostics',
            onPressed: () {
              Navigator.push(
                context,
                MaterialPageRoute(builder: (_) => const DiagnosticsScreen()),
              );
            },
          ),
          IconButton(
            icon: const Icon(Icons.settings_outlined),
            tooltip: 'Settings',
            onPressed: () {
              Navigator.push(
                context,
                MaterialPageRoute(builder: (_) => const SettingsScreen()),
              );
            },
          ),
        ],
      ),
      body: SafeArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
          child: Column(
            children: [
              // Connection Status Header Bar
              _buildConnectionBar(context, connState, ref),
              const SizedBox(height: 20),

              // Kawaii Cat Avatar & Name
              KawaiiCatIllustration(
                size: 160,
                activeFace: status.activeFace,
              ),
              const SizedBox(height: 14),

              Text(
                status.robotName,
                style: const TextStyle(
                  fontSize: 24,
                  fontWeight: FontWeight.bold,
                  color: AppColors.darkNavy,
                  letterSpacing: 0.5,
                ),
              ),
              const SizedBox(height: 4),

              // Subtitle & Firmware badge
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Container(
                    padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                    decoration: BoxDecoration(
                      color: AppColors.lightBlush,
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: Text(
                      'ESP32-S3 • ${status.firmwareVersion}',
                      style: const TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.bold,
                        color: AppColors.darkNavy,
                      ),
                    ),
                  ),
                  if (status.hasRealBattery && status.batteryPercent != null) ...[
                    const SizedBox(width: 8),
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                      decoration: BoxDecoration(
                        color: AppColors.mintFresh,
                        borderRadius: BorderRadius.circular(12),
                      ),
                      child: Text(
                        '🔋 ${status.batteryPercent}%',
                        style: const TextStyle(
                          fontSize: 11,
                          fontWeight: FontWeight.bold,
                          color: AppColors.darkNavy,
                        ),
                      ),
                    ),
                  ],
                ],
              ),

              const SizedBox(height: 24),

              // Main Navigation Action Cards
              _buildFeatureCard(
                context: context,
                title: 'Remote Control',
                subtitle: 'Press-and-hold movement & emergency STOP',
                icon: Icons.sports_esports_outlined,
                color: AppColors.lavender,
                enabled: isConnected,
                onTap: () {
                  Navigator.push(
                    context,
                    MaterialPageRoute(builder: (_) => const RemoteScreen()),
                  );
                },
              ),

              const SizedBox(height: 12),

              _buildFeatureCard(
                context: context,
                title: 'Watch Face Studio',
                subtitle: 'Personalize ST7735 Kawaii Cat expressions & themes',
                icon: Icons.face_retouching_natural_outlined,
                color: AppColors.pastelPink,
                enabled: isConnected,
                onTap: () {
                  Navigator.push(
                    context,
                    MaterialPageRoute(builder: (_) => const WatchFaceStudioScreen()),
                  );
                },
              ),

              const SizedBox(height: 12),

              _buildFeatureCard(
                context: context,
                title: 'Firmware & Wi-Fi OTA',
                subtitle: status.otaSupported
                    ? 'Dual-App OTA available'
                    : 'Single-App partition (Direct flash only)',
                icon: Icons.system_update_alt_outlined,
                color: AppColors.softSkyBlue,
                enabled: isConnected,
                onTap: () {
                  _showOtaNotice(context, status);
                },
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildConnectionBar(BuildContext context, BleConnectionState state, WidgetRef ref) {
    Color bg;
    String text;
    IconData icon;

    switch (state) {
      case BleConnectionState.connected:
        bg = AppColors.mintFresh;
        text = 'Connected to R-bot';
        icon = Icons.bluetooth_connected;
        break;
      case BleConnectionState.connecting:
        bg = AppColors.lavender;
        text = 'Connecting...';
        icon = Icons.bluetooth_searching;
        break;
      case BleConnectionState.scanning:
        bg = AppColors.softSkyBlue;
        text = 'Scanning for R-bot...';
        icon = Icons.bluetooth_searching;
        break;
      case BleConnectionState.disconnected:
      case BleConnectionState.error:
      default:
        bg = AppColors.lightBlush;
        text = 'Disconnected • Tap to Pair';
        icon = Icons.bluetooth_disabled;
        break;
    }

    return GestureDetector(
      onTap: () {
        Navigator.push(
          context,
          MaterialPageRoute(builder: (_) => const DiscoveryScreen()),
        );
      },
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
        decoration: BoxDecoration(
          color: bg,
          borderRadius: BorderRadius.circular(16),
          border: Border.all(color: Colors.white, width: 2),
        ),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Row(
              children: [
                Icon(icon, size: 20, color: AppColors.darkNavy),
                const SizedBox(width: 10),
                Text(
                  text,
                  style: const TextStyle(
                    fontWeight: FontWeight.bold,
                    fontSize: 13,
                    color: AppColors.darkNavy,
                  ),
                ),
              ],
            ),
            const Icon(Icons.arrow_forward_ios, size: 14, color: AppColors.darkNavy),
          ],
        ),
      ),
    );
  }

  Widget _buildFeatureCard({
    required BuildContext context,
    required String title,
    required String subtitle,
    required IconData icon,
    required Color color,
    required bool enabled,
    required VoidCallback onTap,
  }) {
    return Card(
      child: InkWell(
        borderRadius: BorderRadius.circular(20),
        onTap: enabled ? onTap : null,
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              Container(
                width: 50,
                height: 50,
                decoration: BoxDecoration(
                  color: enabled ? color.withAlpha(80) : Colors.grey.withAlpha(40),
                  borderRadius: BorderRadius.circular(16),
                ),
                child: Icon(
                  icon,
                  color: enabled ? AppColors.darkNavy : Colors.grey,
                  size: 26,
                ),
              ),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      title,
                      style: TextStyle(
                        fontWeight: FontWeight.bold,
                        fontSize: 16,
                        color: enabled ? AppColors.darkNavy : Colors.grey,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      subtitle,
                      style: TextStyle(
                        fontSize: 12,
                        color: enabled ? AppColors.subText : Colors.grey,
                      ),
                    ),
                  ],
                ),
              ),
              Icon(
                Icons.arrow_forward_ios,
                size: 14,
                color: enabled ? AppColors.darkNavy : Colors.grey,
              ),
            ],
          ),
        ),
      ),
    );
  }

  void _showOtaNotice(BuildContext context, DeviceStatus status) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Firmware & OTA Status 🛡️'),
        content: Text(
          status.otaSupported
              ? 'Robot has dual application partitions and is ready for safe Wi-Fi OTA updates.'
              : 'Robot is currently running on a single factory partition layout (partitions_singleapp.csv). Standard dual-slot OTA is disabled until the partition table is updated to ota_0/ota_1 with otadata.\n\nAll current updates must be applied via USB cable (idf.py flash).',
        ),
        actions: [
          ElevatedButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Understood'),
          ),
        ],
      ),
    );
  }
}
