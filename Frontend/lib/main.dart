import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'core/theme/app_colors.dart';
import 'features/home/home_screen.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const ProviderScope(child: RbotApp()));
}

class RbotApp extends StatelessWidget {
  const RbotApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'R-bot Companion',
      debugShowCheckedModeBanner: false,
      theme: AppTheme.lightTheme,
      home: const HomeScreen(),
    );
  }
}
