import 'package:flutter/material.dart';
import '../../core/theme/app_colors.dart';

class KawaiiCatIllustration extends StatelessWidget {
  final double size;
  final String activeFace;

  const KawaiiCatIllustration({
    super.key,
    this.size = 140,
    this.activeFace = 'kawaii_classic',
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      width: size,
      height: size,
      decoration: BoxDecoration(
        color: AppColors.warmMilkWhite,
        shape: BoxShape.circle,
        border: Border.all(color: AppColors.cardBorder, width: 3),
        boxShadow: [
          BoxShadow(
            color: AppColors.pastelPink.withAlpha(80),
            blurRadius: 18,
            offset: const Offset(0, 6),
          ),
        ],
      ),
      child: Stack(
        alignment: Alignment.center,
        children: [
          // Background soft halo
          Container(
            width: size * 0.85,
            height: size * 0.85,
            decoration: const BoxDecoration(
              color: Colors.white,
              shape: BoxShape.circle,
            ),
          ),
          // Cat Ears
          Positioned(
            top: size * 0.15,
            left: size * 0.22,
            child: _buildEar(isLeft: true),
          ),
          Positioned(
            top: size * 0.15,
            right: size * 0.22,
            child: _buildEar(isLeft: false),
          ),
          // Cat Face Content (Eyes + Mouth + Cheeks)
          Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              SizedBox(height: size * 0.18),
              // Eyes and Cheeks
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  _buildCheek(),
                  const SizedBox(width: 4),
                  _buildEye(),
                  const SizedBox(width: 14),
                  _buildEye(),
                  const SizedBox(width: 4),
                  _buildCheek(),
                ],
              ),
              const SizedBox(height: 6),
              // Cute :3 Mouth
              const Text(
                ':3',
                style: TextStyle(
                  fontSize: 16,
                  fontWeight: FontWeight.bold,
                  color: AppColors.darkNavy,
                ),
              ),
            ],
          ),
          // Ribbon Bow
          Positioned(
            top: size * 0.22,
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
              decoration: BoxDecoration(
                color: AppColors.pastelPink,
                borderRadius: BorderRadius.circular(8),
              ),
              child: const Icon(
                Icons.star,
                size: 12,
                color: Colors.white,
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildEar({required bool isLeft}) {
    return Transform.rotate(
      angle: isLeft ? -0.3 : 0.3,
      child: Container(
        width: size * 0.22,
        height: size * 0.25,
        decoration: BoxDecoration(
          color: AppColors.pastelPink,
          borderRadius: const BorderRadius.only(
            topLeft: Radius.circular(20),
            topRight: Radius.circular(20),
          ),
          border: Border.all(color: AppColors.cardBorder, width: 2),
        ),
        child: Center(
          child: Container(
            width: size * 0.12,
            height: size * 0.14,
            decoration: const BoxDecoration(
              color: AppColors.lightBlush,
              borderRadius: BorderRadius.only(
                topLeft: Radius.circular(10),
                topRight: Radius.circular(10),
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildCheek() {
    return Container(
      width: 10,
      height: 7,
      decoration: BoxDecoration(
        color: AppColors.lightBlush,
        borderRadius: BorderRadius.circular(5),
      ),
    );
  }

  Widget _buildEye() {
    return Container(
      width: 16,
      height: 22,
      decoration: BoxDecoration(
        color: AppColors.darkNavy,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: AppColors.softSkyBlue, width: 1.5),
      ),
      child: Align(
        alignment: const Alignment(-0.3, -0.4),
        child: Container(
          width: 5,
          height: 5,
          decoration: const BoxDecoration(
            color: Colors.white,
            shape: BoxShape.circle,
          ),
        ),
      ),
    );
  }
}
