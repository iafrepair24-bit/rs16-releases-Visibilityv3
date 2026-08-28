import 'dart:math';
import 'package:flutter/material.dart';
import '../theme/rs16_theme.dart';
import '../models/session_models.dart';

/// Digital arc speedometer — RS16 identity, not a copy of any analog gauge.
class SpeedometerWidget extends StatelessWidget {
  final double speed;   // km/h
  final double maxSpeed; // km/h (default 280)

  const SpeedometerWidget({
    super.key,
    required this.speed,
    this.maxSpeed = 280,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 240,
      height: 240,
      child: Stack(
        alignment: Alignment.center,
        children: [
          CustomPaint(
            size: const Size(240, 240),
            painter: _ArcPainter(speed: speed, maxSpeed: maxSpeed),
          ),
          Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(
                speed.toStringAsFixed(0),
                style: RS16Typography.racingNumberSize(72).copyWith(
                  color: _speedColor(speed),
                ),
              ),
              Text('km/h', style: RS16Typography.labelSize(14)),
            ],
          ),
        ],
      ),
    );
  }

  Color _speedColor(double s) {
    if (s < 80) return RS16Colors.speedSlow;
    if (s < 150) return RS16Colors.textPrimary;
    return RS16Colors.accent;
  }
}

class _ArcPainter extends CustomPainter {
  final double speed;
  final double maxSpeed;

  _ArcPainter({required this.speed, required this.maxSpeed});

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final radius = size.width / 2 - 12;
    const startAngle = pi * 0.75;
    const sweepAngle = pi * 1.5;

    // Background track
    canvas.drawArc(
      Rect.fromCircle(center: center, radius: radius),
      startAngle,
      sweepAngle,
      false,
      Paint()
        ..color = RS16Colors.surfaceElevated
        ..strokeWidth = 14
        ..style = PaintingStyle.stroke
        ..strokeCap = StrokeCap.round,
    );

    // Filled arc
    final fraction = (speed / maxSpeed).clamp(0.0, 1.0);
    final gradient = SweepGradient(
      startAngle: startAngle,
      endAngle: startAngle + sweepAngle,
      colors: const [RS16Colors.speedSlow, RS16Colors.lapCurrent, RS16Colors.accent],
      stops: const [0.0, 0.6, 1.0],
    ).createShader(Rect.fromCircle(center: center, radius: radius));

    canvas.drawArc(
      Rect.fromCircle(center: center, radius: radius),
      startAngle,
      sweepAngle * fraction,
      false,
      Paint()
        ..shader = gradient
        ..strokeWidth = 14
        ..style = PaintingStyle.stroke
        ..strokeCap = StrokeCap.round,
    );

    // Tick marks
    const totalTicks = 14;
    for (int i = 0; i <= totalTicks; i++) {
      final angle = startAngle + sweepAngle * (i / totalTicks);
      final inner = center + Offset(cos(angle) * (radius - 20), sin(angle) * (radius - 20));
      final outer = center + Offset(cos(angle) * radius, sin(angle) * radius);
      canvas.drawLine(
        inner,
        outer,
        Paint()
          ..color = RS16Colors.divider
          ..strokeWidth = 1.5,
      );
    }
  }

  @override
  bool shouldRepaint(_ArcPainter old) => old.speed != speed;
}

/// G-Force XY scatter dot widget.
class GForceMeter extends StatelessWidget {
  final GForce gForce;
  final double maxG;

  const GForceMeter({
    super.key,
    required this.gForce,
    this.maxG = 3.0,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 120,
      height: 120,
      child: CustomPaint(
        painter: _GForcePainter(gForce: gForce, maxG: maxG),
      ),
    );
  }
}

class _GForcePainter extends CustomPainter {
  final GForce gForce;
  final double maxG;

  _GForcePainter({required this.gForce, required this.maxG});

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final r = size.width / 2;

    // Grid circles
    for (final ring in [0.33, 0.66, 1.0]) {
      canvas.drawCircle(
        center,
        r * ring,
        Paint()
          ..color = RS16Colors.divider
          ..style = PaintingStyle.stroke
          ..strokeWidth = 0.8,
      );
    }
    // Crosshairs
    canvas.drawLine(Offset(0, center.dy), Offset(size.width, center.dy),
        Paint()..color = RS16Colors.divider..strokeWidth = 0.8);
    canvas.drawLine(Offset(center.dx, 0), Offset(center.dx, size.height),
        Paint()..color = RS16Colors.divider..strokeWidth = 0.8);

    // G-dot
    final dotX = center.dx + (gForce.x / maxG) * r;
    final dotY = center.dy - (gForce.y / maxG) * r; // Y flipped
    canvas.drawCircle(
      Offset(dotX.clamp(0.0, size.width), dotY.clamp(0.0, size.height)),
      10,
      Paint()..color = RS16Colors.accent,
    );
    // Inner highlight
    canvas.drawCircle(
      Offset(dotX.clamp(0.0, size.width), dotY.clamp(0.0, size.height)),
      4,
      Paint()..color = RS16Colors.accentLight,
    );
  }

  @override
  bool shouldRepaint(_GForcePainter old) =>
      old.gForce.x != gForce.x || old.gForce.y != gForce.y;
}
