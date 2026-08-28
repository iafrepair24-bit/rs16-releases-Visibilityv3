import 'package:flutter/material.dart';

/// RS16 brand color palette and typography.
class RS16Colors {
  RS16Colors._();

  static const background = Color(0xFF0D0D0D);
  static const surface = Color(0xFF1A1A1A);
  static const surfaceElevated = Color(0xFF242424);

  static const accent = Color(0xFFE8321A); // RS16 red-orange
  static const accentLight = Color(0xFFFF5C3A);
  static const accentDark = Color(0xFFB32412);

  static const textPrimary = Color(0xFFFFFFFF);
  static const textSecondary = Color(0xFFB0B0B0);
  static const textMuted = Color(0xFF606060);

  static const lapBest = Color(0xFF22C55E);   // green
  static const lapWorst = Color(0xFFEF4444);  // red
  static const lapCurrent = Color(0xFFFACC15); // yellow

  static const speedSlow = Color(0xFF3B82F6);
  static const speedMid = Color(0xFFFFD700);
  static const speedFast = Color(0xFFE8321A);

  static const divider = Color(0xFF2E2E2E);
}

class RS16Typography {
  RS16Typography._();

  /// Monospace bold – used for all racing numbers.
  static const TextStyle racingNumber = TextStyle(
    fontFamily: 'RobotoMono',
    fontWeight: FontWeight.w700,
    color: RS16Colors.textPrimary,
    letterSpacing: 1.2,
  );

  static TextStyle racingNumberSize(double size) =>
      racingNumber.copyWith(fontSize: size);

  /// Clean sans-serif for labels.
  static const TextStyle label = TextStyle(
    fontFamily: 'Roboto',
    fontWeight: FontWeight.w400,
    color: RS16Colors.textSecondary,
    letterSpacing: 0.8,
  );

  static TextStyle labelSize(double size) => label.copyWith(fontSize: size);

  static const TextStyle labelBold = TextStyle(
    fontFamily: 'Roboto',
    fontWeight: FontWeight.w600,
    color: RS16Colors.textPrimary,
    letterSpacing: 0.6,
  );
}

ThemeData rs16Theme() {
  return ThemeData(
    brightness: Brightness.dark,
    scaffoldBackgroundColor: RS16Colors.background,
    colorScheme: const ColorScheme.dark(
      primary: RS16Colors.accent,
      secondary: RS16Colors.accentLight,
      surface: RS16Colors.surface,
      onPrimary: RS16Colors.textPrimary,
      onSurface: RS16Colors.textPrimary,
    ),
    appBarTheme: const AppBarTheme(
      backgroundColor: RS16Colors.background,
      elevation: 0,
      titleTextStyle: TextStyle(
        fontFamily: 'Roboto',
        fontWeight: FontWeight.w700,
        fontSize: 18,
        color: RS16Colors.textPrimary,
        letterSpacing: 1.0,
      ),
      iconTheme: IconThemeData(color: RS16Colors.textPrimary),
    ),
    bottomNavigationBarTheme: const BottomNavigationBarThemeData(
      backgroundColor: RS16Colors.surface,
      selectedItemColor: RS16Colors.accent,
      unselectedItemColor: RS16Colors.textMuted,
      type: BottomNavigationBarType.fixed,
    ),
    dividerColor: RS16Colors.divider,
    cardColor: RS16Colors.surface,
    iconTheme: const IconThemeData(color: RS16Colors.textPrimary, size: 22),
    textTheme: TextTheme(
      displayLarge: RS16Typography.racingNumberSize(48),
      displayMedium: RS16Typography.racingNumberSize(36),
      displaySmall: RS16Typography.racingNumberSize(28),
      headlineMedium: RS16Typography.labelSize(20).copyWith(
        fontWeight: FontWeight.w700,
        color: RS16Colors.textPrimary,
      ),
      bodyLarge: RS16Typography.labelSize(16).copyWith(
        color: RS16Colors.textPrimary,
      ),
      bodyMedium: RS16Typography.labelSize(14),
      labelLarge: RS16Typography.labelSize(12),
    ),
  );
}
