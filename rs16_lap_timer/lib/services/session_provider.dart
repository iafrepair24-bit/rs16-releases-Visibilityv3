import 'dart:async';
import 'dart:math';
import 'package:flutter/foundation.dart';
import '../models/session_models.dart';

enum RecordingState { idle, recording, paused }

/// Central state provider for the live racing session.
/// In production this receives data from [BleService] and [GpsService].
/// For now it uses a mock data generator so the UI is fully functional.
class SessionProvider extends ChangeNotifier {
  // ── Live data ──────────────────────────────────────────────────────────────
  double _speed = 0;
  double _altitude = 0;
  int _gpsSignal = 0; // satellites in view 0-12
  GForce _gForce = const GForce(x: 0, y: 0);
  GpsPoint? _lastPoint;

  // ── Session state ──────────────────────────────────────────────────────────
  RecordingState _state = RecordingState.idle;
  final List<LapRecord> _laps = [];
  final List<GpsPoint> _trackPoints = [];
  DateTime? _lapStartTime;
  Duration _currentLapElapsed = Duration.zero;
  int _lapCount = 0;
  double _lapTopSpeed = 0;

  Timer? _mockTimer;
  Timer? _lapTimer;

  // ── Getters ────────────────────────────────────────────────────────────────
  double get speed => _speed;
  double get altitude => _altitude;
  int get gpsSignal => _gpsSignal;
  GForce get gForce => _gForce;
  RecordingState get state => _state;
  List<LapRecord> get laps => List.unmodifiable(_laps);
  List<GpsPoint> get trackPoints => List.unmodifiable(_trackPoints);
  Duration get currentLapElapsed => _currentLapElapsed;
  GpsPoint? get lastPoint => _lastPoint;

  LapRecord? get bestLap =>
      _laps.isEmpty ? null : _laps.reduce((a, b) => a.lapTime < b.lapTime ? a : b);

  Duration? get deltaFromBest {
    final best = bestLap;
    if (best == null) return null;
    return _currentLapElapsed - best.lapTime;
  }

  // ── Controls ───────────────────────────────────────────────────────────────

  void startRecording() {
    _state = RecordingState.recording;
    _lapStartTime = DateTime.now();
    _lapCount = 0;
    _laps.clear();
    _trackPoints.clear();
    _lapTopSpeed = 0;
    _startLapTimer();
    _startMockGps();
    notifyListeners();
  }

  void splitLap() {
    if (_state != RecordingState.recording) return;
    _lapCount++;
    final now = DateTime.now();
    _laps.add(LapRecord(
      lapNumber: _lapCount,
      lapTime: _currentLapElapsed,
      topSpeed: _lapTopSpeed,
      timestamp: now,
    ));
    _lapStartTime = now;
    _lapTopSpeed = 0;
    _currentLapElapsed = Duration.zero;
    notifyListeners();
  }

  void stopRecording() {
    if (_laps.isNotEmpty || _lapCount > 0) splitLap();
    _state = RecordingState.idle;
    _lapTimer?.cancel();
    _mockTimer?.cancel();
    notifyListeners();
  }

  void reset() {
    _state = RecordingState.idle;
    _lapTimer?.cancel();
    _mockTimer?.cancel();
    _laps.clear();
    _trackPoints.clear();
    _currentLapElapsed = Duration.zero;
    _lapCount = 0;
    _speed = 0;
    notifyListeners();
  }

  // ── Mock GPS generator ─────────────────────────────────────────────────────

  final _rng = Random();
  double _mockLat = -6.2146;
  double _mockLon = 106.8451;
  double _mockHeading = 0;

  void _startMockGps() {
    _gpsSignal = 8 + _rng.nextInt(4);
    _mockTimer = Timer.periodic(const Duration(milliseconds: 200), (_) {
      _mockHeading += (_rng.nextDouble() - 0.5) * 10;
      final rad = _mockHeading * pi / 180;
      _mockLat += cos(rad) * 0.00003;
      _mockLon += sin(rad) * 0.00003;

      _speed = 60 + _rng.nextDouble() * 120; // 60-180 km/h
      if (_speed > _lapTopSpeed) _lapTopSpeed = _speed;
      _altitude = 8 + _rng.nextDouble() * 5;
      _gForce = GForce(
        x: (_rng.nextDouble() - 0.5) * 2.5,
        y: (_rng.nextDouble() - 0.5) * 3.0,
      );

      final pt = GpsPoint(
        latitude: _mockLat,
        longitude: _mockLon,
        altitude: _altitude,
        speed: _speed,
        accuracy: 2.0 + _rng.nextDouble() * 2,
        timestamp: DateTime.now(),
      );
      _lastPoint = pt;
      _trackPoints.add(pt);

      notifyListeners();
    });
  }

  void _startLapTimer() {
    _lapTimer?.cancel();
    _lapTimer = Timer.periodic(const Duration(milliseconds: 50), (_) {
      if (_lapStartTime != null) {
        _currentLapElapsed = DateTime.now().difference(_lapStartTime!);
        notifyListeners();
      }
    });
  }

  @override
  void dispose() {
    _mockTimer?.cancel();
    _lapTimer?.cancel();
    super.dispose();
  }
}
