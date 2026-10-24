import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../core/theme/app_colors.dart';
import '../../data/ble/ble_transport.dart';
import '../../data/models/protocol_models.dart';
import '../../data/models/watch_face_theme.dart';

class WatchFaceStudioScreen extends ConsumerStatefulWidget {
  const WatchFaceStudioScreen({super.key});

  @override
  ConsumerState<WatchFaceStudioScreen> createState() => _WatchFaceStudioScreenState();
}

class _WatchFaceStudioScreenState extends ConsumerState<WatchFaceStudioScreen> {
  String _selectedThemeId = 'kawaii_classic';
  bool _isApplying = false;
  String? _applyFeedback;

  @override
  Widget build(BuildContext context) {
    final deviceStatusAsync = ref.watch(deviceStatusProvider);
    final activeThemeId = deviceStatusAsync.value?.activeFace ?? 'kawaii_classic';

    return Scaffold(
      appBar: AppBar(
        title: const Text('Watch Face Studio 🎨'),
      ),
      body: SafeArea(
        child: Column(
          children: [
            const SizedBox(height: 12),
            // Live Preview Card (ST7735 128x160 Aspect Ratio Simulation)
            _buildLivePreview(activeThemeId),
            const SizedBox(height: 16),
            // Theme Catalog Gallery
            Expanded(
              child: RadioGroup<String>(
                groupValue: _selectedThemeId,
                onChanged: (val) {
                  if (val != null) setState(() => _selectedThemeId = val);
                },
                child: ListView.separated(
                padding: const EdgeInsets.symmetric(horizontal: 20),
                itemCount: WatchFaceTheme.catalog.length,
                separatorBuilder: (_, _) => const SizedBox(height: 12),
                itemBuilder: (context, index) {
                  final theme = WatchFaceTheme.catalog[index];
                  final isSelected = theme.id == _selectedThemeId;
                  final isCurrentlyActive = theme.id == activeThemeId;

                  return GestureDetector(
                    onTap: () {
                      setState(() {
                        _selectedThemeId = theme.id;
                        _applyFeedback = null;
                      });
                    },
                    child: Container(
                      padding: const EdgeInsets.all(14),
                      decoration: BoxDecoration(
                        color: Colors.white,
                        borderRadius: BorderRadius.circular(20),
                        border: Border.all(
                          color: isSelected ? AppColors.pastelPink : AppColors.cardBorder,
                          width: isSelected ? 2.5 : 1.5,
                        ),
                        boxShadow: [
                          if (isSelected)
                            BoxShadow(
                              color: AppColors.pastelPink.withAlpha(80),
                              blurRadius: 10,
                              offset: const Offset(0, 4),
                            ),
                        ],
                      ),
                      child: Row(
                        children: [
                          Container(
                            width: 50,
                            height: 50,
                            decoration: BoxDecoration(
                              color: theme.primaryColor.withAlpha(60),
                              borderRadius: BorderRadius.circular(16),
                            ),
                            child: Icon(theme.icon, color: AppColors.darkNavy, size: 28),
                          ),
                          const SizedBox(width: 14),
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Row(
                                  children: [
                                    Text(
                                      theme.title,
                                      style: const TextStyle(
                                        fontWeight: FontWeight.bold,
                                        fontSize: 15,
                                        color: AppColors.darkNavy,
                                      ),
                                    ),
                                    if (isCurrentlyActive) ...[
                                      const SizedBox(width: 8),
                                      Container(
                                        padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                                        decoration: BoxDecoration(
                                          color: AppColors.mintFresh,
                                          borderRadius: BorderRadius.circular(8),
                                        ),
                                        child: const Text(
                                          'ACTIVE',
                                          style: TextStyle(
                                            fontSize: 9,
                                            fontWeight: FontWeight.bold,
                                            color: AppColors.darkNavy,
                                          ),
                                        ),
                                      ),
                                    ],
                                  ],
                                ),
                                const SizedBox(height: 4),
                                Text(
                                  theme.description,
                                  style: TextStyle(fontSize: 12, color: AppColors.subText),
                                  maxLines: 2,
                                  overflow: TextOverflow.ellipsis,
                                ),
                              ],
                            ),
                          ),
                          Radio<String>(
                            value: theme.id,
                            activeColor: AppColors.pastelPink,
                          ),
                        ],
                      ),
                    ),
                  );
                },
              ),
            ),
          ),

            if (_applyFeedback != null)
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 6),
                child: Text(
                  _applyFeedback!,
                  style: const TextStyle(fontWeight: FontWeight.bold, color: AppColors.darkNavy),
                ),
              ),

            // Action Bar
            Padding(
              padding: const EdgeInsets.all(20),
              child: SizedBox(
                width: double.infinity,
                height: 50,
                child: ElevatedButton(
                  onPressed: _isApplying ? null : _applyThemeToRobot,
                  child: _isApplying
                      ? const CircularProgressIndicator(color: AppColors.darkNavy)
                      : const Text('Send Theme to R-bot ✨'),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildLivePreview(String activeThemeId) {
    final previewTheme = WatchFaceTheme.findById(_selectedThemeId);

    return Container(
      width: 140,
      height: 175, // 128x160 proportional TFT simulation
      decoration: BoxDecoration(
        color: const Color(0xFF1E1A2B), // ST7735 screen bezel
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: AppColors.cardBorder, width: 4),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withAlpha(30),
            blurRadius: 10,
            offset: const Offset(0, 5),
          ),
        ],
      ),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          // ST7735 Top Status Header Mock
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
            color: Colors.black26,
            child: const Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Icon(Icons.wifi, size: 10, color: Colors.white70),
                Text('128x160', style: TextStyle(color: Colors.white70, fontSize: 8)),
                Icon(Icons.battery_charging_full, size: 10, color: Colors.white70),
              ],
            ),
          ),
          const Spacer(),
          Icon(previewTheme.icon, size: 44, color: previewTheme.primaryColor),
          const SizedBox(height: 6),
          Text(
            previewTheme.title,
            style: const TextStyle(
              color: Colors.white,
              fontWeight: FontWeight.bold,
              fontSize: 11,
            ),
          ),
          const Spacer(),
          // Bottom Pill Banner
          Container(
            margin: const EdgeInsets.only(bottom: 6),
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 2),
            decoration: BoxDecoration(
              color: previewTheme.primaryColor,
              borderRadius: BorderRadius.circular(6),
            ),
            child: Text(
              previewTheme.moods.first,
              style: const TextStyle(
                color: AppColors.darkNavy,
                fontWeight: FontWeight.bold,
                fontSize: 8,
              ),
            ),
          ),
        ],
      ),
    );
  }

  Future<void> _applyThemeToRobot() async {
    setState(() {
      _isApplying = true;
      _applyFeedback = null;
    });

    final transport = ref.read(bleTransportProvider);
    final cmd = BleCommand.setWatchFace(id: 105, themeId: _selectedThemeId);

    try {
      final res = await transport.sendCommand(cmd);
      if (res.ok) {
        setState(() {
          _applyFeedback = '✅ Robot accepted theme: $_selectedThemeId';
        });
      } else {
        setState(() {
          _applyFeedback = '⚠️ Rejected: ${res.errorMessage ?? "Unknown error"}';
        });
      }
    } catch (e) {
      setState(() {
        _applyFeedback = '⚠️ Error: $e';
      });
    } finally {
      setState(() => _isApplying = false);
    }
  }
}
