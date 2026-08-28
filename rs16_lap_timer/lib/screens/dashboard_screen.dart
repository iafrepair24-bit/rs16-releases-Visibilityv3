import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/session_provider.dart';
import '../theme/rs16_theme.dart';
import '../widgets/shared_widgets.dart';
import '../widgets/speedometer_widget.dart';

class DashboardScreen extends StatelessWidget {
  const DashboardScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Consumer<SessionProvider>(
      builder: (context, session, _) {
        return Scaffold(
          appBar: AppBar(
            title: const Text('LIVE DASHBOARD'),
            actions: const [
              Padding(
                padding: EdgeInsets.only(right: 12),
                child: RS16Logo(),
              ),
            ],
          ),
          body: SafeArea(
            child: Column(
              children: [
                // ── Top info bar ──────────────────────────────────────────
                _TopInfoBar(session: session),
                const Divider(height: 1),

                // ── Speedometer + G-force ─────────────────────────────────
                Expanded(
                  child: Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 16),
                    child: Row(
                      children: [
                        // Speedometer (takes most space)
                        Expanded(
                          flex: 3,
                          child: Center(
                            child: SpeedometerWidget(speed: session.speed),
                          ),
                        ),
                        // G-force + side metrics
                        Expanded(
                          flex: 2,
                          child: _SidePanel(session: session),
                        ),
                      ],
                    ),
                  ),
                ),

                // ── Lap info strip ────────────────────────────────────────
                _LapInfoStrip(session: session),

                // ── START / SPLIT / STOP buttons ──────────────────────────
                _ControlButtons(session: session),

                const SizedBox(height: 16),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _TopInfoBar extends StatelessWidget {
  final SessionProvider session;
  const _TopInfoBar({required this.session});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      color: RS16Colors.surface,
      child: Row(
        children: [
          GpsSignalIndicator(satellites: session.gpsSignal),
          const SizedBox(width: 8),
          Text('${session.gpsSignal} SAT',
              style: RS16Typography.labelSize(11)),
          const Spacer(),
          const Icon(Icons.terrain_outlined, size: 14,
              color: RS16Colors.textSecondary),
          const SizedBox(width: 4),
          Text(
            '${session.altitude.toStringAsFixed(1)} m',
            style: RS16Typography.racingNumberSize(13)
                .copyWith(color: RS16Colors.textSecondary),
          ),
        ],
      ),
    );
  }
}

class _SidePanel extends StatelessWidget {
  final SessionProvider session;
  const _SidePanel({required this.session});

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        Text('G-FORCE', style: RS16Typography.labelSize(10)),
        const SizedBox(height: 8),
        GForceMeter(gForce: session.gForce),
        const SizedBox(height: 12),
        _GLabel('LAT', session.gForce.x),
        const SizedBox(height: 4),
        _GLabel('LON', session.gForce.y),
      ],
    );
  }
}

class _GLabel extends StatelessWidget {
  final String axis;
  final double value;
  const _GLabel(this.axis, this.value);

  @override
  Widget build(BuildContext context) {
    final color =
        value.abs() > 2.0 ? RS16Colors.accent : RS16Colors.textSecondary;
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        Text('$axis ', style: RS16Typography.labelSize(10)),
        Text(
          '${value >= 0 ? '+' : ''}${value.toStringAsFixed(2)}g',
          style: RS16Typography.racingNumberSize(12).copyWith(color: color),
        ),
      ],
    );
  }
}

class _LapInfoStrip extends StatelessWidget {
  final SessionProvider session;
  const _LapInfoStrip({required this.session});

  String _fmt(Duration d) {
    final min = d.inMinutes.remainder(60).toString().padLeft(2, '0');
    final sec = d.inSeconds.remainder(60).toString().padLeft(2, '0');
    final ms =
        (d.inMilliseconds.remainder(1000) ~/ 10).toString().padLeft(2, '0');
    return '$min:$sec.$ms';
  }

  @override
  Widget build(BuildContext context) {
    final delta = session.deltaFromBest;
    final deltaStr = delta == null
        ? '--'
        : (delta.isNegative ? '-' : '+') + _fmt(delta.abs());
    final deltaColor =
        delta == null || delta.isNegative ? RS16Colors.lapBest : RS16Colors.lapWorst;

    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: RS16Card(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            _LapCell(
              label: 'LAP',
              value: (session.laps.length + 1).toString(),
            ),
            _LapCell(
              label: 'CURRENT',
              value: _fmt(session.currentLapElapsed),
              valueColor: RS16Colors.lapCurrent,
            ),
            _LapCell(
              label: 'BEST',
              value: session.bestLap == null
                  ? '--:--.--'
                  : session.bestLap!.formattedTime,
              valueColor: RS16Colors.lapBest,
            ),
            _LapCell(
              label: 'DELTA',
              value: deltaStr,
              valueColor: deltaColor,
            ),
          ],
        ),
      ),
    );
  }
}

class _LapCell extends StatelessWidget {
  final String label;
  final String value;
  final Color valueColor;

  const _LapCell({
    required this.label,
    required this.value,
    this.valueColor = RS16Colors.textPrimary,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Text(label, style: RS16Typography.labelSize(10)),
        const SizedBox(height: 4),
        AnimatedRacingNumber(
          value: value,
          fontSize: 16,
          color: valueColor,
        ),
      ],
    );
  }
}

class _ControlButtons extends StatelessWidget {
  final SessionProvider session;
  const _ControlButtons({required this.session});

  @override
  Widget build(BuildContext context) {
    final isRecording = session.state == RecordingState.recording;

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16),
      child: Row(
        children: [
          // START / STOP
          Expanded(
            flex: 3,
            child: SizedBox(
              height: 64,
              child: ElevatedButton(
                onPressed: isRecording
                    ? session.stopRecording
                    : session.startRecording,
                style: ElevatedButton.styleFrom(
                  backgroundColor:
                      isRecording ? RS16Colors.lapWorst : RS16Colors.accent,
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(8)),
                ),
                child: Text(
                  isRecording ? 'STOP' : 'START',
                  style: RS16Typography.racingNumberSize(20),
                ),
              ),
            ),
          ),
          if (isRecording) ...[
            const SizedBox(width: 12),
            Expanded(
              flex: 2,
              child: SizedBox(
                height: 64,
                child: OutlinedButton(
                  onPressed: session.splitLap,
                  style: OutlinedButton.styleFrom(
                    side: const BorderSide(
                        color: RS16Colors.lapCurrent, width: 2),
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(8)),
                  ),
                  child: Text(
                    'LAP',
                    style: RS16Typography.racingNumberSize(18)
                        .copyWith(color: RS16Colors.lapCurrent),
                  ),
                ),
              ),
            ),
          ],
        ],
      ),
    );
  }
}
