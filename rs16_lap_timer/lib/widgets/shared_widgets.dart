import 'package:flutter/material.dart';
import '../theme/rs16_theme.dart';

/// RS16 logo badge shown in every screen's app bar.
class RS16Logo extends StatelessWidget {
  final double size;
  const RS16Logo({super.key, this.size = 28});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        border: Border.all(color: RS16Colors.accent, width: 1.5),
        borderRadius: BorderRadius.circular(4),
      ),
      child: Text(
        'RS16',
        style: RS16Typography.racingNumberSize(size * 0.5).copyWith(
          color: RS16Colors.accent,
          letterSpacing: 2,
        ),
      ),
    );
  }
}

/// Animated digital number that smoothly transitions.
class AnimatedRacingNumber extends StatelessWidget {
  final String value;
  final double fontSize;
  final Color color;

  const AnimatedRacingNumber({
    super.key,
    required this.value,
    required this.fontSize,
    this.color = RS16Colors.textPrimary,
  });

  @override
  Widget build(BuildContext context) {
    return AnimatedSwitcher(
      duration: const Duration(milliseconds: 120),
      transitionBuilder: (child, anim) =>
          FadeTransition(opacity: anim, child: child),
      child: Text(
        value,
        key: ValueKey(value),
        style: RS16Typography.racingNumberSize(fontSize).copyWith(color: color),
      ),
    );
  }
}

/// GPS satellite signal bar indicator.
class GpsSignalIndicator extends StatelessWidget {
  final int satellites; // 0-12
  const GpsSignalIndicator({super.key, required this.satellites});

  @override
  Widget build(BuildContext context) {
    final bars = (satellites / 3).ceil().clamp(0, 4);
    final color = satellites >= 6
        ? RS16Colors.lapBest
        : satellites >= 3
            ? RS16Colors.lapCurrent
            : RS16Colors.lapWorst;

    return Row(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.end,
      children: List.generate(4, (i) {
        final filled = i < bars;
        return Container(
          width: 4,
          height: 6.0 + i * 3.0,
          margin: const EdgeInsets.only(right: 2),
          decoration: BoxDecoration(
            color: filled ? color : RS16Colors.textMuted,
            borderRadius: BorderRadius.circular(1),
          ),
        );
      }),
    );
  }
}

/// A card container with RS16 surface color.
class RS16Card extends StatelessWidget {
  final Widget child;
  final EdgeInsets padding;

  const RS16Card({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(16),
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: padding,
      decoration: BoxDecoration(
        color: RS16Colors.surface,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: RS16Colors.divider, width: 0.5),
      ),
      child: child,
    );
  }
}

/// A stat tile used in session summary cards.
class StatTile extends StatelessWidget {
  final String label;
  final String value;
  final String? unit;
  final Color valueColor;

  const StatTile({
    super.key,
    required this.label,
    required this.value,
    this.unit,
    this.valueColor = RS16Colors.textPrimary,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label.toUpperCase(), style: RS16Typography.labelSize(10)),
        const SizedBox(height: 4),
        Row(
          crossAxisAlignment: CrossAxisAlignment.baseline,
          textBaseline: TextBaseline.alphabetic,
          children: [
            Text(
              value,
              style: RS16Typography.racingNumberSize(22)
                  .copyWith(color: valueColor),
            ),
            if (unit != null) ...[
              const SizedBox(width: 3),
              Text(unit!, style: RS16Typography.labelSize(11)),
            ],
          ],
        ),
      ],
    );
  }
}
