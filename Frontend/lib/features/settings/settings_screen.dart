import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/theme/app_colors.dart';
import '../../data/ble/ble_transport.dart';
import '../../data/models/protocol_models.dart';

class SettingsScreen extends ConsumerStatefulWidget {
  const SettingsScreen({super.key});

  @override
  ConsumerState<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends ConsumerState<SettingsScreen> {
  final _nameController = TextEditingController();
  bool _isSaving = false;
  String? _feedback;

  @override
  void initState() {
    super.initState();
    final status = ref.read(deviceStatusProvider).value;
    if (status != null) {
      _nameController.text = status.robotName;
    }
  }

  @override
  void dispose() {
    _nameController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final statusAsync = ref.watch(deviceStatusProvider);
    final status = statusAsync.value ?? DeviceStatus.disconnected();

    return Scaffold(
      appBar: AppBar(
        title: const Text('Robot Settings ⚙️'),
      ),
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
          children: [
            // Robot Name Card
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'Robot Identity',
                      style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16),
                    ),
                    const SizedBox(height: 12),
                    TextField(
                      controller: _nameController,
                      decoration: InputDecoration(
                        labelText: 'Robot Name',
                        hintText: 'e.g. Mochi, Luna, Chibi',
                        border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                        filled: true,
                        fillColor: AppColors.warmMilkWhite,
                      ),
                    ),
                    const SizedBox(height: 12),
                    SizedBox(
                      width: double.infinity,
                      child: ElevatedButton(
                        onPressed: _isSaving ? null : _saveName,
                        child: _isSaving
                            ? const CircularProgressIndicator(color: AppColors.darkNavy)
                            : const Text('Update Name 🐾'),
                      ),
                    ),
                    if (_feedback != null) ...[
                      const SizedBox(height: 8),
                      Text(
                        _feedback!,
                        style: const TextStyle(fontWeight: FontWeight.bold, color: AppColors.darkNavy),
                      ),
                    ],
                  ],
                ),
              ),
            ),

            const SizedBox(height: 14),

            // Device Info Card
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'Hardware & Firmware Info',
                      style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16),
                    ),
                    const Divider(height: 20),
                    _buildInfoRow('Hardware Target', status.hardwareId),
                    _buildInfoRow('Firmware Version', status.firmwareVersion),
                    _buildInfoRow('OTA Architecture', status.otaSupported ? 'Dual-App (Ready)' : 'Single-App (Factory)'),
                    _buildInfoRow('Display Engine', 'ST7735 128x160 20 FPS'),
                    _buildInfoRow('Wake Word Model', 'WakeNet10 (wn10_alexa)'),
                  ],
                ),
              ),
            ),

            const SizedBox(height: 14),

            // Safety & Reset Actions
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'System Actions',
                      style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16),
                    ),
                    const SizedBox(height: 12),
                    OutlinedButton.icon(
                      style: OutlinedButton.styleFrom(
                        foregroundColor: AppColors.stopRed,
                        side: const BorderSide(color: AppColors.stopRed),
                        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
                      ),
                      onPressed: () => _confirmReboot(context),
                      icon: const Icon(Icons.restart_alt),
                      label: const Text('Reboot R-bot'),
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

  Widget _buildInfoRow(String title, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(title, style: TextStyle(color: AppColors.subText, fontSize: 13)),
          Text(value, style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 13)),
        ],
      ),
    );
  }

  Future<void> _saveName() async {
    final newName = _nameController.text.trim();
    if (newName.isEmpty) return;

    setState(() {
      _isSaving = true;
      _feedback = null;
    });

    final transport = ref.read(bleTransportProvider);
    final cmd = BleCommand.setConfig(id: 110, deviceName: newName);

    try {
      final res = await transport.sendCommand(cmd);
      if (res.ok) {
        setState(() => _feedback = '✅ Saved robot name: $newName');
      } else {
        setState(() => _feedback = '⚠️ Failed: ${res.errorMessage}');
      }
    } catch (e) {
      setState(() => _feedback = '⚠️ Error: $e');
    } finally {
      setState(() => _isSaving = false);
    }
  }

  void _confirmReboot(BuildContext context) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Reboot R-bot?'),
        content: const Text('This will restart the ESP32-S3 microcontroller. Motors will stop immediately.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
          ElevatedButton(
            style: ElevatedButton.styleFrom(backgroundColor: AppColors.stopRed),
            onPressed: () {
              Navigator.pop(ctx);
              // Send explicit reboot command
            },
            child: const Text('Reboot Now', style: TextStyle(color: Colors.white)),
          ),
        ],
      ),
    );
  }
}
