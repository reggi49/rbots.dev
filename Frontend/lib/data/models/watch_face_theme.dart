import 'package:flutter/material.dart';

/// Supported Watch Face Themes matching the Kawaii Cat Engine
/// Master Prompt: "Do not claim arbitrary widgets can be transferred unless supported by firmware"
class WatchFaceTheme {
  final String id;
  final String title;
  final String description;
  final IconData icon;
  final Color primaryColor;
  final Color secondaryColor;
  final List<String> moods;

  const WatchFaceTheme({
    required this.id,
    required this.title,
    required this.description,
    required this.icon,
    required this.primaryColor,
    required this.secondaryColor,
    required this.moods,
  });

  static const List<WatchFaceTheme> catalog = [
    WatchFaceTheme(
      id: 'kawaii_classic',
      title: 'Kawaii Classic',
      description: 'Classic anime eyes with layered cyan-violet pupils, pink bow & sweet :3 smile',
      icon: Icons.pets,
      primaryColor: Color(0xFFFFB8D2),
      secondaryColor: Color(0xFF8EDCFF),
      moods: ['IDLE', 'BLINK', 'SPEAKING', 'LISTENING'],
    ),
    WatchFaceTheme(
      id: 'happy_cat',
      title: 'Happy Cat',
      description: 'Curved joyful crescent eyes with blooming blush cheeks and raised waving paws',
      icon: Icons.sentiment_very_satisfied,
      primaryColor: Color(0xFFFF94B8),
      secondaryColor: Color(0xFFFFE066),
      moods: ['HAPPY', 'HAPPY_BIG', 'EXCITED'],
    ),
    WatchFaceTheme(
      id: 'sleepy_cat',
      title: 'Sleepy Cat',
      description: 'Peaceful resting eyes on a soft pink cushion with drifting Zzz and dimming',
      icon: Icons.bedtime,
      primaryColor: Color(0xFFD8C9FF),
      secondaryColor: Color(0xFF705090),
      moods: ['SLEEPY', 'IDLE'],
    ),
    WatchFaceTheme(
      id: 'love_cat',
      title: 'Love Cat',
      description: 'Twinkling heart-shaped pupils with floating floating ruby hearts and deep blush',
      icon: Icons.favorite,
      primaryColor: Color(0xFFFF4D80),
      secondaryColor: Color(0xFFFFD8E7),
      moods: ['LOVE', 'HAPPY'],
    ),
    WatchFaceTheme(
      id: 'curious_cat',
      title: 'Curious Cat',
      description: 'Asymmetric perked ears with inquisitive head tilt and lifted paw',
      icon: Icons.search,
      primaryColor: Color(0xFF8EDCFF),
      secondaryColor: Color(0xFFB7E4C7),
      moods: ['CURIOUS_L', 'CURIOUS_R', 'CONFUSED'],
    ),
    WatchFaceTheme(
      id: 'playful_cat',
      title: 'Playful Cat',
      description: 'Winking eye with mischievous tongue blep (:P) and floating pastel candy',
      icon: Icons.mood,
      primaryColor: Color(0xFFFFB703),
      secondaryColor: Color(0xFFFFB8D2),
      moods: ['PLAYFUL', 'WINK_LEFT', 'WINK_RIGHT'],
    ),
  ];

  static WatchFaceTheme findById(String id) {
    return catalog.firstWhere(
      (theme) => theme.id == id,
      orElse: () => catalog.first,
    );
  }
}
