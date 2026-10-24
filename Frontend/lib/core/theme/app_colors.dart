import 'package:flutter/material.dart';

/// R-bot Kawaii Pastel Theme Palette
/// Master Prompt Specifications:
/// - Warm milk white: #FFF9F2
/// - Pastel pink: #FFB8D2
/// - Light blush: #FFD8E7
/// - Lavender: #D8C9FF
/// - Soft sky blue: #8EDCFF
class AppColors {
  AppColors._();

  static const Color warmMilkWhite = Color(0xFFFFF9F2);
  static const Color pastelPink = Color(0xFFFFB8D2);
  static const Color lightBlush = Color(0xFFFFD8E7);
  static const Color lavender = Color(0xFFD8C9FF);
  static const Color softSkyBlue = Color(0xFF8EDCFF);

  // Structural & Neutral Elements
  static const Color darkNavy = Color(0xFF2D2A4A);
  static const Color subText = Color(0xFF7A7593);
  static const Color cardBg = Colors.white;
  static const Color cardBorder = Color(0xFFFFE6EF);

  // Status & Safety Semantics
  static const Color stopRed = Color(0xFFFF4D6D);
  static const Color stopRedDark = Color(0xFFD90429);
  static const Color successGreen = Color(0xFF48CAE4);
  static const Color warningOrange = Color(0xFFFFB703);
  static const Color mintFresh = Color(0xFFB7E4C7);
}

class AppTheme {
  AppTheme._();

  static ThemeData get lightTheme {
    return ThemeData(
      useMaterial3: true,
      scaffoldBackgroundColor: AppColors.warmMilkWhite,
      colorScheme: const ColorScheme.light(
        primary: AppColors.pastelPink,
        onPrimary: AppColors.darkNavy,
        secondary: AppColors.lavender,
        onSecondary: AppColors.darkNavy,
        tertiary: AppColors.softSkyBlue,
        surface: AppColors.cardBg,
        onSurface: AppColors.darkNavy,
        error: AppColors.stopRed,
        onError: Colors.white,
      ),
      appBarTheme: const AppBarTheme(
        backgroundColor: AppColors.warmMilkWhite,
        foregroundColor: AppColors.darkNavy,
        elevation: 0,
        centerTitle: true,
        titleTextStyle: TextStyle(
          color: AppColors.darkNavy,
          fontSize: 20,
          fontWeight: FontWeight.bold,
          letterSpacing: 0.3,
        ),
      ),
      cardTheme: CardThemeData(
        color: AppColors.cardBg,
        elevation: 2,
        shadowColor: AppColors.pastelPink.withAlpha(50),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(20),
          side: const BorderSide(color: AppColors.cardBorder, width: 1.5),
        ),
      ),
      elevatedButtonTheme: ElevatedButtonThemeData(
        style: ElevatedButton.styleFrom(
          elevation: 0,
          backgroundColor: AppColors.pastelPink,
          foregroundColor: AppColors.darkNavy,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(16),
          ),
          textStyle: const TextStyle(
            fontWeight: FontWeight.bold,
            fontSize: 15,
          ),
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
        ),
      ),
    );
  }
}
