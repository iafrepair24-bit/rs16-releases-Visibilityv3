import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/session_provider.dart';
import '../theme/rs16_theme.dart';
import '../widgets/shared_widgets.dart';
import '../models/session_models.dart';

class LapSplitScreen extends StatelessWidget {
  const LapSplitScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Consumer<SessionProvider>(
      builder: (context, session, _) {
        return Scaffold(
          appBar: AppBar(
            title: const Text('LAP SPLITS'),
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
                // ── Header row ─────────────────────────────────────────────
                Container(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
                  color: RS16Colors.surface,
                  child: Row(
                    children: [
                      _HeaderCell('LAP', flex: 1),
                      _HeaderCell('TIME', flex: 3),
                      _HeaderCell('DELTA', flex: 3),
                      _HeaderCell('TOP SPD', flex: 3),
                    ],
                  ),
                ),
                const Divider(height: 1),

                // ── Lap list ──────────────────────────────────────────────
                Expanded(
                  child: session.laps.isEmpty
                      ? const _EmptyState()
                      : ListView.builder(
                          itemCount: session.laps.length,
                          itemBuilder: (ctx, i) {
                            // Display newest lap on top
                            final lap = session.laps[
                                session.laps.length - 1 - i];
                            return _LapRow(
                              lap: lap,
                              bestLap: session.bestLap,
                              worstLap: _worstLap(session.laps),
                              isLatest: i == 0 &&
                                  session.state == RecordingState.recording,
                            );
                          },
                        ),
                ),

                // ── Current live lap strip ────────────────────────────────
                if (session.state == RecordingState.recording)
                  _CurrentLapStrip(session: session),
              ],
            ),
          ),
        );
      },
    );
  }

  LapRecord? _worstLap(List<LapRecord> laps) => laps.isEmpty
      ? null
      : laps.reduce((a, b) => a.lapTime > b.lapTime ? a : b);
}

class _HeaderCell extends StatelessWidget {
  final String text;
  final int flex;
  const _HeaderCell(this.text, {required this.flex});

  @override
  Widget build(BuildContext context) {
    return Expanded(
      flex: flex,
      child: Text(text,
          style: RS16Typography.labelSize(10),
          textAlign: TextAlign.center),
    );
  }
}

class _LapRow extends StatelessWidget {
  final LapRecord lap;
  final LapRecord? bestLap;
  final LapRecord? worstLap;
  final bool isLatest;

  const _LapRow({
    required this.lap,
    required this.bestLap,
    required this.worstLap,
    required this.isLatest,
  });

  Color get _rowColor {
    if (bestLap != null && lap.lapTime == bestLap!.lapTime) {
      return RS16Colors.lapBest.withOpacity(0.12);
    }
    if (worstLap != null && lap.lapTime == worstLap!.lapTime) {
      return RS16Colors.lapWorst.withOpacity(0.10);
    }
    return Colors.transparent;
  }

  Color get _timeColor {
    if (bestLap != null && lap.lapTime == bestLap!.lapTime) {
      return RS16Colors.lapBest;
    }
    if (worstLap != null && lap.lapTime == worstLap!.lapTime) {
      return RS16Colors.lapWorst;
    }
    if (isLatest) return RS16Colors.lapCurrent;
    return RS16Colors.textPrimary;
  }

  String get _delta {
    if (bestLap == null) return '--';
    final diff = lap.lapTime - bestLap!.lapTime;
    final sign = diff.isNegative ? '-' : '+';
    final d = diff.abs();
    final sec = d.inSeconds.remainder(60).toString().padLeft(2, '0');
    final ms =
        (d.inMilliseconds.remainder(1000) ~/ 10).toString().padLeft(2, '0');
    return '$sign${d.inMinutes.remainder(60)}:$sec.$ms';
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      color: _rowColor,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      child: Row(
        children: [
          Expanded(
            flex: 1,
            child: Text(
              lap.lapNumber.toString(),
              style: RS16Typography.racingNumberSize(14),
              textAlign: TextAlign.center,
            ),
          ),
          Expanded(
            flex: 3,
            child: Text(
              lap.formattedTime,
              style: RS16Typography.racingNumberSize(16)
                  .copyWith(color: _timeColor),
              textAlign: TextAlign.center,
            ),
          ),
          Expanded(
            flex: 3,
            child: Text(
              _delta,
              style: RS16Typography.racingNumberSize(14).copyWith(
                color: bestLap != null && lap.lapTime == bestLap!.lapTime
                    ? RS16Colors.lapBest
                    : RS16Colors.textSecondary,
              ),
              textAlign: TextAlign.center,
            ),
          ),
          Expanded(
            flex: 3,
            child: Text(
              '${lap.topSpeed.toStringAsFixed(1)} km/h',
              style: RS16Typography.racingNumberSize(13)
                  .copyWith(color: RS16Colors.textSecondary),
              textAlign: TextAlign.center,
            ),
          ),
        ],
      ),
    );
  }
}

class _CurrentLapStrip extends StatelessWidget {
  final SessionProvider session;
  const _CurrentLapStrip({required this.session});

  String _fmt(Duration d) {
    final min = d.inMinutes.remainder(60).toString().padLeft(2, '0');
    final sec = d.inSeconds.remainder(60).toString().padLeft(2, '0');
    final ms =
        (d.inMilliseconds.remainder(1000) ~/ 10).toString().padLeft(2, '0');
    return '$min:$sec.$ms';
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      color: RS16Colors.surface,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      child: Row(
        children: [
          Text('CURRENT LAP', style: RS16Typography.labelSize(11)),
          const SizedBox(width: 12),
          Text(
            _fmt(session.currentLapElapsed),
            style: RS16Typography.racingNumberSize(18)
                .copyWith(color: RS16Colors.lapCurrent),
          ),
          const Spacer(),
          Text(
            'LAP ${session.laps.length + 1}',
            style: RS16Typography.racingNumberSize(14)
                .copyWith(color: RS16Colors.textSecondary),
          ),
        ],
      ),
    );
  }
}

class _EmptyState extends StatelessWidget {
  const _EmptyState();

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.flag_outlined, size: 48, color: RS16Colors.textMuted),
          const SizedBox(height: 12),
          Text('No laps recorded yet',
              style: RS16Typography.labelSize(14)
                  .copyWith(color: RS16Colors.textMuted)),
          const SizedBox(height: 4),
          Text('Start recording on the Dashboard',
              style: RS16Typography.labelSize(12)
                  .copyWith(color: RS16Colors.textMuted)),
        ],
      ),
    );
  }
}
