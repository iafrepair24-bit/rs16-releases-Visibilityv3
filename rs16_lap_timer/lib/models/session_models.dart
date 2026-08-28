/// A single recorded lap.
class LapRecord {
  final int lapNumber;
  final Duration lapTime;
  final double topSpeed; // km/h
  final DateTime timestamp;

  const LapRecord({
    required this.lapNumber,
    required this.lapTime,
    required this.topSpeed,
    required this.timestamp,
  });

  Map<String, dynamic> toMap() => {
        'lap_number': lapNumber,
        'lap_time_ms': lapTime.inMilliseconds,
        'top_speed': topSpeed,
        'timestamp': timestamp.toIso8601String(),
      };

  factory LapRecord.fromMap(Map<String, dynamic> m) => LapRecord(
        lapNumber: m['lap_number'] as int,
        lapTime: Duration(milliseconds: m['lap_time_ms'] as int),
        topSpeed: (m['top_speed'] as num).toDouble(),
        timestamp: DateTime.parse(m['timestamp'] as String),
      );

  String get formattedTime {
    final min = lapTime.inMinutes.remainder(60).toString().padLeft(2, '0');
    final sec = lapTime.inSeconds.remainder(60).toString().padLeft(2, '0');
    final ms = (lapTime.inMilliseconds.remainder(1000) ~/ 10)
        .toString()
        .padLeft(2, '0');
    return '$min:$sec.$ms';
  }
}

/// A complete racing session.
class SessionRecord {
  final int? id;
  final String trackName;
  final DateTime startTime;
  final DateTime? endTime;
  final List<LapRecord> laps;

  const SessionRecord({
    this.id,
    required this.trackName,
    required this.startTime,
    this.endTime,
    this.laps = const [],
  });

  LapRecord? get bestLap => laps.isEmpty
      ? null
      : laps.reduce((a, b) => a.lapTime < b.lapTime ? a : b);

  LapRecord? get worstLap => laps.isEmpty
      ? null
      : laps.reduce((a, b) => a.lapTime > b.lapTime ? a : b);

  Duration get averageLapTime {
    if (laps.isEmpty) return Duration.zero;
    final total = laps.fold<int>(
        0, (sum, l) => sum + l.lapTime.inMilliseconds);
    return Duration(milliseconds: total ~/ laps.length);
  }

  double get topSpeed =>
      laps.isEmpty ? 0 : laps.map((l) => l.topSpeed).reduce((a, b) => a > b ? a : b);
}

/// Live GPS data point.
class GpsPoint {
  final double latitude;
  final double longitude;
  final double altitude;   // meters
  final double speed;      // km/h
  final double accuracy;   // meters
  final DateTime timestamp;

  const GpsPoint({
    required this.latitude,
    required this.longitude,
    required this.altitude,
    required this.speed,
    required this.accuracy,
    required this.timestamp,
  });
}

/// Live G-force reading.
class GForce {
  final double x; // lateral  (positive = right)
  final double y; // longitudinal (positive = acceleration)

  const GForce({required this.x, required this.y});

  double get magnitude => (x * x + y * y).abs();
}
