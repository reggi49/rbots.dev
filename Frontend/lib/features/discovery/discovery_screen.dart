import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/constants/ble_constants.dart';
import '../../core/permissions/ble_permissions.dart';
import '../../core/theme/app_colors.dart';
import '../../data/ble/ble_transport.dart';

class DiscoveryScreen extends ConsumerStatefulWidget {
  const DiscoveryScreen({super.key});

  @override
  ConsumerState<DiscoveryScreen> createState() => _DiscoveryScreenState();
}

class _DiscoveryScreenState extends ConsumerState<DiscoveryScreen> {
  BlePermissionStatus _permissionStatus = BlePermissionStatus.granted;

  @override
  void initState() {
    super.initState();
    _checkPermissions();
  }

  Future<void> _checkPermissions() async {
    final result = await BlePermissionService.checkPermissions();
    if (mounted) {
      setState(() {
        _permissionStatus = result.status;
      });
    }
  }

  Future<void> _handleScanRequest(BleTransport transport) async {
    if (_permissionStatus == BlePermissionStatus.permanentlyDenied) {
      await BlePermissionService.openAppSettings();
      return;
    }

    if (_permissionStatus != BlePermissionStatus.granted) {
      final req = await BlePermissionService.requestPermissions();
      if (mounted) {
        setState(() {
          _permissionStatus = req.status;
        });
      }
      if (!req.isGranted) return;
    }

    try {
      await transport.startScan();
    } catch (_) {}
  }

  @override
  Widget build(BuildContext context) {
    final connState = ref.watch(bleConnectionStateProvider).value ?? BleConnectionState.disconnected;
    final adapterState = ref.watch(bleAdapterStateProvider).value ?? BleAdapterState.unknown;
    final discoveredRobots = ref.watch(discoveredRobotsProvider).value ?? [];
    final transport = ref.read(bleTransportProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Pair R-bot 🔍'),
      ),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // Permission Warning Banner
              if (_permissionStatus == BlePermissionStatus.denied)
                Container(
                  margin: const EdgeInsets.only(bottom: 14),
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: Colors.amber.shade100,
                    borderRadius: BorderRadius.circular(12),
                    border: Border.all(color: Colors.amber.shade400),
                  ),
                  child: Row(
                    children: [
                      const Icon(Icons.security, color: Colors.amber),
                      const SizedBox(width: 10),
                      const Expanded(
                        child: Text(
                          'Bluetooth permissions are required to scan for R-bot.',
                          style: TextStyle(fontSize: 12, color: AppColors.darkNavy),
                        ),
                      ),
                      TextButton(
                        onPressed: () => _handleScanRequest(transport),
                        child: const Text('Grant'),
                      ),
                    ],
                  ),
                )
              else if (_permissionStatus == BlePermissionStatus.permanentlyDenied)
                Container(
                  margin: const EdgeInsets.only(bottom: 14),
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: Colors.red.shade100,
                    borderRadius: BorderRadius.circular(12),
                    border: Border.all(color: Colors.red.shade300),
                  ),
                  child: Row(
                    children: [
                      const Icon(Icons.settings, color: Colors.red),
                      const SizedBox(width: 10),
                      const Expanded(
                        child: Text(
                          'Bluetooth permission was permanently denied. Please grant it in App Settings.',
                          style: TextStyle(fontSize: 12, color: AppColors.darkNavy),
                        ),
                      ),
                      TextButton(
                        onPressed: () => BlePermissionService.openAppSettings(),
                        child: const Text('Settings'),
                      ),
                    ],
                  ),
                ),

              // Bluetooth Adapter Warning Banner if not ready
              if (adapterState == BleAdapterState.poweredOff)
                Container(
                  margin: const EdgeInsets.only(bottom: 16),
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: Colors.amber.shade100,
                    borderRadius: BorderRadius.circular(12),
                    border: Border.all(color: Colors.amber.shade400),
                  ),
                  child: const Row(
                    children: [
                      Icon(Icons.bluetooth_disabled, color: Colors.amber),
                      SizedBox(width: 10),
                      Expanded(
                        child: Text(
                          'Bluetooth is turned off. Please enable Bluetooth on your phone.',
                          style: TextStyle(fontSize: 13, color: AppColors.darkNavy),
                        ),
                      ),
                    ],
                  ),
                ),

              // Target Service Information Card
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Row(
                    children: [
                      Container(
                        padding: const EdgeInsets.all(12),
                        decoration: BoxDecoration(
                          color: AppColors.lavender.withAlpha(80),
                          shape: BoxShape.circle,
                        ),
                        child: const Icon(Icons.bluetooth, color: AppColors.darkNavy, size: 28),
                      ),
                      const SizedBox(width: 14),
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            const Text(
                              'Target Service UUID',
                              style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16),
                            ),
                            Text(
                              BleConstants.serviceUuid,
                              style: const TextStyle(fontSize: 11, color: AppColors.subText),
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                ),
              ),

              const SizedBox(height: 20),

              const Text(
                'Discovered Companions',
                style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16, color: AppColors.darkNavy),
              ),
              const SizedBox(height: 10),

              // Dynamic List of Discovered Devices
              Expanded(
                child: discoveredRobots.isEmpty
                    ? Center(
                        child: Padding(
                          padding: const EdgeInsets.all(24),
                          child: Column(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Icon(
                                connState == BleConnectionState.scanning
                                    ? Icons.radar
                                    : Icons.bluetooth_searching,
                                size: 48,
                                color: AppColors.subText.withAlpha(120),
                              ),
                              const SizedBox(height: 12),
                              Text(
                                connState == BleConnectionState.scanning
                                    ? 'Searching for nearby R-bot...'
                                    : 'No R-bot found nearby.\nMake sure your robot is powered on and within range.',
                                textAlign: TextAlign.center,
                                style: const TextStyle(color: AppColors.subText, fontSize: 14),
                              ),
                            ],
                          ),
                        ),
                      )
                    : ListView.separated(
                        itemCount: discoveredRobots.length,
                        separatorBuilder: (_, _) => const SizedBox(height: 12),
                        itemBuilder: (context, index) {
                          final robot = discoveredRobots[index];
                          return Container(
                            padding: const EdgeInsets.all(16),
                            decoration: BoxDecoration(
                              color: Colors.white,
                              borderRadius: BorderRadius.circular(20),
                              border: Border.all(color: AppColors.cardBorder, width: 1.5),
                            ),
                            child: Row(
                              children: [
                                Container(
                                  width: 44,
                                  height: 44,
                                  decoration: const BoxDecoration(
                                    color: AppColors.lightBlush,
                                    shape: BoxShape.circle,
                                  ),
                                  child: const Icon(Icons.pets, color: AppColors.darkNavy),
                                ),
                                const SizedBox(width: 14),
                                Expanded(
                                  child: Column(
                                    crossAxisAlignment: CrossAxisAlignment.start,
                                    children: [
                                      Text(
                                        robot.name,
                                        style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 15),
                                      ),
                                      const SizedBox(height: 2),
                                      Text(
                                        'RSSI: ${robot.rssi} dBm • ${robot.id}',
                                        style: const TextStyle(fontSize: 12, color: AppColors.subText),
                                      ),
                                    ],
                                  ),
                                ),
                                if (connState == BleConnectionState.connected)
                                  ElevatedButton(
                                    style: ElevatedButton.styleFrom(
                                      backgroundColor: AppColors.lightBlush,
                                      foregroundColor: AppColors.darkNavy,
                                    ),
                                    onPressed: () => transport.disconnect(),
                                    child: const Text('Disconnect'),
                                  )
                                else if (connState == BleConnectionState.connecting)
                                  const SizedBox(
                                    width: 24,
                                    height: 24,
                                    child: CircularProgressIndicator(strokeWidth: 2.5),
                                  )
                                else
                                  ElevatedButton(
                                    onPressed: () => transport.connect(robot.id),
                                    child: const Text('Connect 🔗'),
                                  ),
                              ],
                            ),
                          );
                        },
                      ),
              ),

              const SizedBox(height: 16),

              SizedBox(
                width: double.infinity,
                height: 50,
                child: ElevatedButton.icon(
                  icon: const Icon(Icons.refresh),
                  label: Text(connState == BleConnectionState.scanning ? 'Scanning...' : 'Scan Again'),
                  onPressed: connState == BleConnectionState.scanning
                      ? null
                      : () => _handleScanRequest(transport),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
