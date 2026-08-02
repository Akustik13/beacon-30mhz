import 'package:flutter/material.dart';

abstract class AppColors {
  static const primary   = Color(0xFF1565C0);
  static const secondary = Color(0xFF2E7D32);
  static const warning   = Color(0xFFF57F17);
  static const error     = Color(0xFFC62828);
  static const demo      = Color(0xFFE65100);
  // Dark palette
  static const surface   = Color(0xFF1E1E2E);
  static const card      = Color(0xFF2A2A3E);
  static const text      = Color(0xFFE0E0E0);
  static const textSub   = Color(0xFF9E9E9E);
  static const divider   = Color(0xFF3A3A4E);
  // Light palette
  static const lightSurface = Color(0xFFF5F5F5);
  static const lightCard    = Color(0xFFFFFFFF);
  static const lightText    = Color(0xFF1A1A2E);
  static const lightTextSub = Color(0xFF757575);
  static const lightDivider = Color(0xFFE0E0E0);
}

ThemeData buildLightTheme() {
  return ThemeData(
    useMaterial3: true,
    brightness: Brightness.light,
    colorScheme: const ColorScheme.light(
      primary: AppColors.primary,
      secondary: AppColors.secondary,
      error: AppColors.error,
      surface: AppColors.lightSurface,
      onSurface: AppColors.lightText,
    ),
    scaffoldBackgroundColor: AppColors.lightSurface,
    cardTheme: CardThemeData(
      color: AppColors.lightCard,
      elevation: 0,
      margin: EdgeInsets.zero,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.all(Radius.circular(12)),
        side: BorderSide(color: AppColors.lightDivider),
      ),
    ),
    appBarTheme: const AppBarTheme(
      backgroundColor: AppColors.lightCard,
      elevation: 0,
      scrolledUnderElevation: 0,
      centerTitle: false,
      titleTextStyle: TextStyle(
        color: AppColors.lightText,
        fontSize: 18,
        fontWeight: FontWeight.bold,
      ),
      iconTheme: IconThemeData(color: AppColors.lightText),
    ),
    navigationBarTheme: NavigationBarThemeData(
      backgroundColor: AppColors.lightCard,
      indicatorColor: Color(0xFF1565C0).withValues(alpha: 0.18),
      indicatorShape: StadiumBorder(),
      labelTextStyle: WidgetStateProperty.all(
        TextStyle(fontSize: 11, color: AppColors.lightText),
      ),
      iconTheme: WidgetStateProperty.all(IconThemeData(size: 20)),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        minimumSize: const Size(double.infinity, 52),
        backgroundColor: AppColors.primary,
        foregroundColor: Colors.white,
        textStyle: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        minimumSize: const Size(48, 48),
        side: const BorderSide(color: AppColors.primary),
        foregroundColor: AppColors.primary,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
      ),
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: AppColors.lightSurface,
      border: OutlineInputBorder(
        borderRadius: BorderRadius.all(Radius.circular(10)),
        borderSide: BorderSide(color: AppColors.lightDivider),
      ),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.all(Radius.circular(10)),
        borderSide: BorderSide(color: AppColors.lightDivider),
      ),
      labelStyle: TextStyle(color: AppColors.lightTextSub),
      contentPadding: EdgeInsets.symmetric(horizontal: 14, vertical: 14),
    ),
    dividerTheme: const DividerThemeData(color: AppColors.lightDivider, space: 1),
    textTheme: const TextTheme(
      titleLarge:  TextStyle(fontSize: 20, fontWeight: FontWeight.bold,  color: AppColors.lightText),
      titleMedium: TextStyle(fontSize: 16, fontWeight: FontWeight.w600,  color: AppColors.lightText),
      titleSmall:  TextStyle(fontSize: 14, fontWeight: FontWeight.w600,  color: AppColors.lightText),
      bodyLarge:   TextStyle(fontSize: 16, color: AppColors.lightText),
      bodyMedium:  TextStyle(fontSize: 14, color: AppColors.lightText),
      bodySmall:   TextStyle(fontSize: 13, color: AppColors.lightTextSub),
      labelSmall:  TextStyle(fontSize: 11, color: AppColors.lightTextSub),
    ),
  );
}

ThemeData buildAppTheme() {
  return ThemeData(
    useMaterial3: true,
    brightness: Brightness.dark,
    colorScheme: const ColorScheme.dark(
      primary: AppColors.primary,
      secondary: AppColors.secondary,
      error: AppColors.error,
      surface: AppColors.surface,
      onSurface: AppColors.text,
    ),
    scaffoldBackgroundColor: AppColors.surface,
    cardTheme: const CardThemeData(
      color: AppColors.card,
      elevation: 0,
      margin: EdgeInsets.zero,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.all(Radius.circular(12)),
        side: BorderSide(color: AppColors.divider),
      ),
    ),
    appBarTheme: const AppBarTheme(
      backgroundColor: AppColors.surface,
      elevation: 0,
      scrolledUnderElevation: 0,
      centerTitle: false,
      titleTextStyle: TextStyle(
        color: AppColors.text,
        fontSize: 18,
        fontWeight: FontWeight.bold,
      ),
      iconTheme: IconThemeData(color: AppColors.text),
    ),
    navigationBarTheme: NavigationBarThemeData(
      backgroundColor: AppColors.card,
      indicatorColor: AppColors.primary.withValues(alpha: 0.25),
      indicatorShape: const StadiumBorder(),
      labelTextStyle: WidgetStateProperty.all(
        const TextStyle(fontSize: 11, color: AppColors.text),
      ),
      iconTheme: WidgetStateProperty.all(
        const IconThemeData(size: 20),
      ),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        minimumSize: const Size(double.infinity, 52),
        backgroundColor: AppColors.primary,
        foregroundColor: Colors.white,
        textStyle: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        minimumSize: const Size(48, 48),
        side: const BorderSide(color: AppColors.primary),
        foregroundColor: AppColors.primary,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
      ),
    ),
    inputDecorationTheme: const InputDecorationTheme(
      filled: true,
      fillColor: Color(0xFF16162A),
      border: OutlineInputBorder(
        borderRadius: BorderRadius.all(Radius.circular(10)),
        borderSide: BorderSide(color: AppColors.divider),
      ),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.all(Radius.circular(10)),
        borderSide: BorderSide(color: AppColors.divider),
      ),
      labelStyle: TextStyle(color: AppColors.textSub),
      contentPadding: EdgeInsets.symmetric(horizontal: 14, vertical: 14),
    ),
    dividerTheme: const DividerThemeData(color: AppColors.divider, space: 1),
    textTheme: const TextTheme(
      titleLarge: TextStyle(fontSize: 20, fontWeight: FontWeight.bold, color: AppColors.text),
      titleMedium: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: AppColors.text),
      titleSmall: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: AppColors.text),
      bodyLarge: TextStyle(fontSize: 16, color: AppColors.text),
      bodyMedium: TextStyle(fontSize: 14, color: AppColors.text),
      bodySmall: TextStyle(fontSize: 13, color: AppColors.textSub),
      labelSmall: TextStyle(fontSize: 11, color: AppColors.textSub),
    ),
  );
}
