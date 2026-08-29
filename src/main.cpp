/*
 * RS16 V1 — Qstarz-like GPS Logger Firmware
 * Hardware:
 *   ESP32-S3
 *   GPS  : MG-902 (u-blox M9, 25 Hz)  UART2 RX=GPIO17 TX=GPIO18
 *   IMU  : MPU6050 200 Hz              I2C  SDA=GPIO5  SCL=GPIO6
 *   LCD  : ILI9341 2.8" 320x240 SPI   CS=8  DC=9  RST=13  MOSI=11  CLK=10  MISO=12
 *   BL   : GPIO38 (PWM)
 *   Buzzer: NPN C945 GPIO15
 *   Battery: ADC GPIO14 (divider ÷2, Vref 3.3 V)
 *   Buttons: GPIO1(UP) GPIO2(DOWN) GPIO4(SELECT) GPIO7(BACK)  active LOW
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <SPIFFS.h>
#include <math.h>
#include <time.h>

// ─── PIN DEFINITIONS ────────────────────────────────────────────────────────
#define GPS_RX_PIN   17
#define GPS_TX_PIN   18
#define GPS_BAUD     115200

#define IMU_SDA      5
#define IMU_SCL      6

#define TFT_BL_PIN   38
#define BUZZER_PIN   15
#define BAT_ADC_PIN  14

#define BTN_UP       1
#define BTN_DOWN     2
#define BTN_SELECT   4
#define BTN_BACK     7

// ─── CONSTANTS ───────────────────────────────────────────────────────────────
#define BAT_DIVIDER  2.0f
#define VREF         3.3f
#define ADC_MAX      4095.0f
#define BAT_MAX_V    4.2f
#define BAT_MIN_V    3.3f

#define LOG_INTERVAL_MS  100        // 10 Hz log rate
#define TRACK_MAX_PTS    5000       // max track points in SPIFFS
#define LOG_DIR          "/logs"
#define LOG_EXT          ".csv"

// ─── COLOURS ─────────────────────────────────────────────────────────────────
#define C_BG       TFT_BLACK
#define C_HEADER   0x0451           // dark navy
#define C_ACCENT   0x07E0           // green
#define C_WARN     0xFD20           // orange
#define C_RED      TFT_RED
#define C_WHITE    TFT_WHITE
#define C_LGREY    0xC618
#define C_DGREY    0x4208
#define C_TRACK    0x07FF           // cyan
#define C_YELLOW   0xFFE0
#define C_PURPLE   0xF81F
#define C_ORANGE   0xFD20
#define C_SKY      0x867F           // artificial horizon sky
#define C_GROUND   0x5220           // artificial horizon ground

// ─── OBJECTS ─────────────────────────────────────────────────────────────────
TFT_eSPI     tft;
TinyGPSPlus  gps;
Adafruit_MPU6050 mpu;
HardwareSerial gpsSerial(2);

// ─── SCREEN PAGES ────────────────────────────────────────────────────────────
enum Screen { SCR_HOME, SCR_TRACK, SCR_STATS, SCR_GRAPH, SCR_DRAG, SCR_FILES, SCR_COUNT };
Screen currentScreen = SCR_HOME;

// ─── BUTTON STATE ────────────────────────────────────────────────────────────
struct Button {
    uint8_t pin;
    bool    lastState;
    bool    pressed;
    uint32_t lastDebounce;
};
Button btns[4] = {
    {BTN_UP,     HIGH, false, 0},
    {BTN_DOWN,   HIGH, false, 0},
    {BTN_SELECT, HIGH, false, 0},
    {BTN_BACK,   HIGH, false, 0}
};
#define DEBOUNCE_MS 50

// ─── GPS DATA ────────────────────────────────────────────────────────────────
struct GpsData {
    double   lat = 0, lon = 0, alt = 0;
    float    speed_kmh = 0;         // km/h
    float    speed_max = 0;
    float    course = 0;
    uint8_t  sats = 0;
    float    hdop = 99.0f;
    bool     valid = false;
    uint32_t fix_age = 0;
    char     timeStr[12] = "--:--:--";
    char     dateStr[12] = "--/--/--";
    // session stats
    double   dist_km = 0;
    double   prev_lat = 0, prev_lon = 0;
    bool     has_prev = false;
    uint32_t start_ms = 0;
    uint32_t elapsed_s = 0;
    float    avg_speed = 0;
};
GpsData gd;

// ─── IMU DATA ────────────────────────────────────────────────────────────────
struct ImuData {
    float heading = 0;   // 0-360
    float pitch = 0;
    float roll  = 0;
    float accel_g = 0;
};
ImuData imuData;

// ─── TRACK LOG ───────────────────────────────────────────────────────────────
bool      recording = false;
uint32_t  lastLogMs = 0;
String    logFileName;
File      logFile;
uint32_t  logCount = 0;

// ─── TRACK DISPLAY BUFFER ────────────────────────────────────────────────────
struct TrackPt { int16_t x, y; };
TrackPt   trackBuf[512];
uint16_t  trackHead = 0;
bool      trackFull = false;
double    trackMinLat, trackMaxLat, trackMinLon, trackMaxLon;
bool      trackBoundsInit = false;

// ─── MISC ────────────────────────────────────────────────────────────────────
float batVoltage = 0;
int   batPercent = 0;
uint32_t lastBatMs = 0;
uint32_t lastImuMs = 0;
uint32_t lastRedrawMs = 0;
bool screenDirty = true;

// ─── GRAPH RING BUFFERS (200 samples each) ───────────────────────────────────
#define GBUF_LEN  200
struct GraphBuf {
    float  data[GBUF_LEN];
    uint16_t head = 0;
    float  vmin = 0, vmax = 1;   // auto-scale range
    void push(float v) {
        data[head] = v;
        head = (head + 1) % GBUF_LEN;
        // expand range, slow decay back toward 0
        if (v > vmax) vmax = v;
        if (v < vmin) vmin = v;
    }
    float get(uint16_t i) const {  // i=0 oldest, i=GBUF_LEN-1 newest
        return data[(head + i) % GBUF_LEN];
    }
};
GraphBuf gbSpeed, gbGforce, gbPitch, gbRoll;
uint32_t lastGraphMs = 0;          // sample rate ~50 ms

// ─── DRAG RACE STATE MACHINE ─────────────────────────────────────────────────
enum DragState {
    DRAG_IDLE,    // waiting for arm
    DRAG_ARMED,   // armed, waiting for launch (G-force or speed threshold)
    DRAG_RUNNING, // timer running
    DRAG_DONE     // results ready
};
DragState dragState = DRAG_IDLE;

// Distance milestones (metres)
#define DRAG_60FT_M      18.288f   // 60 feet
#define DRAG_8TH_M      201.168f   // 1/8 mile
#define DRAG_QTR_M      402.336f   // 1/4 mile
#define DRAG_LAUNCH_G     0.25f    // G-force threshold to detect launch
#define DRAG_LAUNCH_KMH   5.0f     // fallback: speed threshold for launch

struct DragSplit {
    float timeS   = 0;
    float speedKmh= 0;
    bool  set     = false;
};

struct DragResult {
    DragSplit s0_60;    // 0-60 km/h
    DragSplit s0_80;    // 0-80 km/h
    DragSplit s0_100;   // 0-100 km/h
    DragSplit s0_150;   // 0-150 km/h
    DragSplit s0_200;   // 0-200 km/h
    DragSplit s0_250;   // 0-250 km/h
    DragSplit s60ft;    // 60 ft
    DragSplit s8th;     // 1/8 mile  (time + trap speed at line)
    DragSplit sqtr;     // 1/4 mile  (time + trap speed at line)
    float reactionTimeS = 0;   // G-force spike to actual launch
    float peakG         = 0;
    uint32_t startMs    = 0;
    double   startLat   = 0, startLon = 0;
    double   distM      = 0;   // cumulative metres this run
    double   prevLat    = 0,   prevLon = 0;
    bool     hasStart   = false;
};
DragResult drag;

// Pre-launch G spike detection (reaction time)
float  dragPreG       = 0;
bool   dragLaunchSeen = false;
uint32_t dragArmMs    = 0;

// ─── FORWARD DECLARATIONS ────────────────────────────────────────────────────
void readButtons();
void processGps();
void readImu();
void readBattery();
void updateStats();
void startRecording();
void stopRecording();
void writeLog();
void updateDrag();
void armDrag();
void resetDrag();
void drawScreen();
void drawHome();
void drawTrack();
void drawStats();
void drawGraph();
void drawDrag();
void drawFiles();
void drawStatusBar();
void drawSatBar();
void drawMiniGraph(int x, int y, int w, int h, GraphBuf &gb, uint16_t col,
                   const char* label, const char* unit, float curVal);
void drawArtificialHorizon(int cx, int cy, int r, float pitch, float roll);
void buzz(uint16_t freq, uint16_t ms);
float haversine(double lat1, double lon1, double lat2, double lon2);
void playStartupJingle();
void drawOpeningAnimation();
void animTypeText(int x, int y, const char* text, uint16_t color, uint8_t size, uint16_t delayMs);
void animLoadBar(int x, int y, int w, int h, uint16_t color, uint16_t ms);
void animGpsWave(int cx, int cy, uint16_t color);
void animStatusLine(int y, const char* label, bool ok);

// ─── SETUP ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // Buttons
    for (auto &b : btns) {
        pinMode(b.pin, INPUT_PULLUP);
    }

    // Buzzer
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // Backlight
    pinMode(TFT_BL_PIN, OUTPUT);
    analogWrite(TFT_BL_PIN, 200);  // ~78% brightness

    // TFT
    tft.init();
    tft.setRotation(0);   // portrait, USB at bottom
    tft.fillScreen(C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_WHITE, C_BG);

    // ── Opening Animation ────────────────────────────────────
    drawOpeningAnimation();

    // GPS
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    // Set 25 Hz update rate via UBX-CFG-RATE (40 ms)
    static const uint8_t ubx_rate_25hz[] = {
        0xB5,0x62,0x06,0x08,0x06,0x00,
        0x28,0x00,  // measRate = 40 ms → 25 Hz
        0x01,0x00,  // navRate = 1
        0x01,0x00,  // timeRef = GPS
        0x3E,0xAA   // checksum
    };
    gpsSerial.write(ubx_rate_25hz, sizeof(ubx_rate_25hz));
    delay(100);
    animStatusLine(242, "GPS  MG-902", true);
    delay(300);

    // IMU
    Wire.begin(IMU_SDA, IMU_SCL);
    bool imuOk = mpu.begin(0x68, &Wire);
    if (!imuOk) {
        Serial.println("MPU6050 not found!");
    } else {
        mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
        mpu.setGyroRange(MPU6050_RANGE_500_DEG);
        mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);
    }
    animStatusLine(256, "IMU  MPU6050", imuOk);
    delay(300);

    // SPIFFS
    bool spiffsOk = SPIFFS.begin(true);
    if (!spiffsOk) {
        Serial.println("SPIFFS mount failed");
    } else {
        SPIFFS.mkdir(LOG_DIR);
    }
    animStatusLine(270, "SPIFFS", spiffsOk);
    delay(300);

    // Final bar fill + jingle
    animLoadBar(20, 290, 200, 8, C_ACCENT, 500);
    playStartupJingle();

    tft.fillScreen(C_BG);
    screenDirty = true;
}

// ─── LOOP ────────────────────────────────────────────────────────────────────
void loop() {
    readButtons();
    processGps();

    uint32_t now = millis();

    // IMU @ 50 Hz
    if (now - lastImuMs >= 20) {
        readImu();
        lastImuMs = now;
    }

    // Graph sample @ 50 ms
    if (now - lastGraphMs >= 50) {
        gbSpeed.push(gd.speed_kmh);
        gbGforce.push(imuData.accel_g);
        gbPitch.push(imuData.pitch);
        gbRoll.push(imuData.roll);
        lastGraphMs = now;
    }

    // Battery @ 1 Hz
    if (now - lastBatMs >= 1000) {
        readBattery();
        lastBatMs = now;
    }

    // Update session stats
    if (recording && gd.valid) {
        updateStats();
    }

    // Drag race update (always active when on drag screen)
    if (dragState == DRAG_ARMED || dragState == DRAG_RUNNING) {
        updateDrag();
    }

    // Write log @ LOG_INTERVAL_MS
    if (recording && gd.valid && (now - lastLogMs >= LOG_INTERVAL_MS)) {
        writeLog();
        lastLogMs = now;
    }

    // Redraw @ 10 Hz (100 ms)
    if (now - lastRedrawMs >= 100) {
        drawScreen();
        lastRedrawMs = now;
    }

    // Handle button actions
    bool btnUp     = btns[0].pressed;
    bool btnDown   = btns[1].pressed;
    bool btnSelect = btns[2].pressed;
    bool btnBack   = btns[3].pressed;

    for (auto &b : btns) b.pressed = false;

    if (btnUp) {
        currentScreen = (Screen)((currentScreen + SCR_COUNT - 1) % SCR_COUNT);
        screenDirty = true;
        buzz(1500, 30);
    }
    if (btnDown) {
        currentScreen = (Screen)((currentScreen + 1) % SCR_COUNT);
        screenDirty = true;
        buzz(1500, 30);
    }
    if (btnSelect) {
        if (currentScreen == SCR_HOME || currentScreen == SCR_TRACK) {
            if (!recording) startRecording();
            else            stopRecording();
        } else if (currentScreen == SCR_DRAG) {
            if (dragState == DRAG_IDLE || dragState == DRAG_DONE) {
                armDrag();
            } else if (dragState == DRAG_ARMED || dragState == DRAG_RUNNING) {
                resetDrag();
            }
        }
    }
    if (btnBack) {
        currentScreen = SCR_HOME;
        screenDirty = true;
    }
}

// ─── BUTTON DEBOUNCE ─────────────────────────────────────────────────────────
void readButtons() {
    uint32_t now = millis();
    for (auto &b : btns) {
        bool cur = digitalRead(b.pin);
        if (cur != b.lastState) {
            b.lastDebounce = now;
        }
        if ((now - b.lastDebounce) > DEBOUNCE_MS) {
            if (cur == LOW && b.lastState == HIGH) {
                b.pressed = true;
            }
        }
        b.lastState = cur;
    }
}

// ─── GPS PROCESSING ──────────────────────────────────────────────────────────
void processGps() {
    while (gpsSerial.available()) {
        char c = gpsSerial.read();
        gps.encode(c);
    }

    if (gps.location.isUpdated()) {
        gd.lat   = gps.location.lat();
        gd.lon   = gps.location.lng();
        gd.valid = gps.location.isValid();
        gd.fix_age = gps.location.age();
    }
    if (gps.altitude.isUpdated())  gd.alt       = gps.altitude.meters();
    if (gps.speed.isUpdated())     gd.speed_kmh = gps.speed.kmph();
    if (gps.course.isUpdated())    gd.course    = gps.course.deg();
    if (gps.satellites.isUpdated()) gd.sats     = gps.satellites.value();
    if (gps.hdop.isUpdated())      gd.hdop      = gps.hdop.hdop();

    if (gps.time.isValid()) {
        snprintf(gd.timeStr, sizeof(gd.timeStr), "%02d:%02d:%02d",
                 gps.time.hour(), gps.time.minute(), gps.time.second());
    }
    if (gps.date.isValid()) {
        snprintf(gd.dateStr, sizeof(gd.dateStr), "%02d/%02d/%04d",
                 gps.date.day(), gps.date.month(), gps.date.year());
    }

    if (gd.valid && gd.speed_kmh > gd.speed_max) {
        gd.speed_max = gd.speed_kmh;
    }

    // Track buffer for map display
    if (gd.valid && recording) {
        if (!trackBoundsInit) {
            trackMinLat = trackMaxLat = gd.lat;
            trackMinLon = trackMaxLon = gd.lon;
            trackBoundsInit = true;
        } else {
            if (gd.lat < trackMinLat) trackMinLat = gd.lat;
            if (gd.lat > trackMaxLat) trackMaxLat = gd.lat;
            if (gd.lon < trackMinLon) trackMinLon = gd.lon;
            if (gd.lon > trackMaxLon) trackMaxLon = gd.lon;
        }
    }
}

// ─── IMU ─────────────────────────────────────────────────────────────────────
void readImu() {
    sensors_event_t a, g, temp;
    if (!mpu.getEvent(&a, &g, &temp)) return;

    float ax = a.acceleration.x;
    float ay = a.acceleration.y;
    float az = a.acceleration.z;

    // Pitch & roll from accelerometer
    imuData.pitch   = atan2f(-ax, sqrtf(ay*ay + az*az)) * 57.2958f;
    imuData.roll    = atan2f(ay, az) * 57.2958f;
    imuData.accel_g = sqrtf(ax*ax + ay*ay + az*az) / 9.81f;

    // Use GPS course as heading (compass-free fallback)
    if (gd.valid && gd.speed_kmh > 2.0f) {
        imuData.heading = gd.course;
    }
}

// ─── BATTERY ─────────────────────────────────────────────────────────────────
void readBattery() {
    uint32_t raw = analogRead(BAT_ADC_PIN);
    batVoltage = (raw / ADC_MAX) * VREF * BAT_DIVIDER;
    float pct = (batVoltage - BAT_MIN_V) / (BAT_MAX_V - BAT_MIN_V) * 100.0f;
    batPercent = constrain((int)pct, 0, 100);
}

// ─── SESSION STATISTICS ──────────────────────────────────────────────────────
void updateStats() {
    if (!gd.has_prev) {
        gd.prev_lat = gd.lat;
        gd.prev_lon = gd.lon;
        gd.has_prev = true;
        return;
    }

    // Only count distance when moving > 2 km/h to reduce GPS drift noise
    if (gd.speed_kmh > 2.0f) {
        float d = haversine(gd.prev_lat, gd.prev_lon, gd.lat, gd.lon);
        gd.dist_km += d;
    }
    gd.prev_lat = gd.lat;
    gd.prev_lon = gd.lon;

    gd.elapsed_s = (millis() - gd.start_ms) / 1000;

    if (gd.elapsed_s > 0) {
        gd.avg_speed = (gd.dist_km / (gd.elapsed_s / 3600.0f));
    }
}

// ─── RECORDING ───────────────────────────────────────────────────────────────
void startRecording() {
    // Find next file number
    int n = 0;
    while (true) {
        logFileName = String(LOG_DIR) + "/track" + String(n, DEC) + LOG_EXT;
        if (!SPIFFS.exists(logFileName)) break;
        n++;
    }

    logFile = SPIFFS.open(logFileName, FILE_WRITE);
    if (!logFile) {
        buzz(500, 300);
        return;
    }
    // Qstarz-compatible CSV header
    logFile.println("INDEX,RCR,DATE,TIME,VALID,LATITUDE,N/S,LONGITUDE,E/W,"
                    "HEIGHT,SPEED,HEADING,DSTA,DAGE,PDOP,HDOP,VDOP,NSAT(USED/VIEW),"
                    "SAT INFO,DISTANCE,");

    recording = true;
    logCount  = 0;
    gd.dist_km   = 0;
    gd.speed_max = 0;
    gd.has_prev  = false;
    gd.start_ms  = millis();
    trackBoundsInit = false;
    trackHead = 0;
    trackFull = false;

    buzz(2000, 100); delay(80); buzz(2000, 100);
    screenDirty = true;
    Serial.println("Recording started: " + logFileName);
}

void stopRecording() {
    if (!recording) return;
    recording = false;
    if (logFile) {
        logFile.flush();
        logFile.close();
    }
    buzz(1000, 150); delay(100); buzz(800, 150);
    screenDirty = true;
    Serial.println("Recording stopped. Points: " + String(logCount));
}

void writeLog() {
    if (!logFile) return;

    // INDEX
    logFile.print(logCount); logFile.print(",");
    // RCR (reason for logging, T=time-based)
    logFile.print("T,");
    // DATE
    logFile.print(gd.dateStr); logFile.print(",");
    // TIME
    logFile.print(gd.timeStr); logFile.print(",");
    // VALID
    logFile.print(gd.valid ? "SPS" : "INVALID"); logFile.print(",");
    // LAT
    logFile.print(fabs(gd.lat), 7); logFile.print(",");
    logFile.print(gd.lat >= 0 ? "N" : "S"); logFile.print(",");
    // LON
    logFile.print(fabs(gd.lon), 7); logFile.print(",");
    logFile.print(gd.lon >= 0 ? "E" : "W"); logFile.print(",");
    // HEIGHT (m)
    logFile.print(gd.alt, 1); logFile.print(",");
    // SPEED (km/h)
    logFile.print(gd.speed_kmh, 3); logFile.print(",");
    // HEADING
    logFile.print(gd.course, 2); logFile.print(",");
    // DSTA, DAGE (unused)
    logFile.print("0,0,");
    // PDOP, HDOP, VDOP
    logFile.print(gd.hdop, 1); logFile.print(",");
    logFile.print(gd.hdop, 1); logFile.print(",");
    logFile.print(gd.hdop, 1); logFile.print(",");
    // NSAT
    logFile.print(gd.sats); logFile.print("("); logFile.print(gd.sats); logFile.print("),");
    // SAT INFO (placeholder)
    logFile.print("---,");
    // DISTANCE (cumulative km)
    logFile.print(gd.dist_km * 1000.0, 2);   // in metres
    logFile.println(",");

    logCount++;

    // Keep track display buffer
    if (trackBoundsInit) {
        double latRange = trackMaxLat - trackMinLat;
        double lonRange = trackMaxLon - trackMinLon;
        if (latRange == 0) latRange = 0.0001;
        if (lonRange == 0) lonRange = 0.0001;
        // Map to 240x200 pixels (leaving header)
        int16_t px = (int16_t)((gd.lon - trackMinLon) / lonRange * 220.0) + 10;
        int16_t py = (int16_t)((trackMaxLat - gd.lat) / latRange * 180.0) + 60;
        trackBuf[trackHead] = {px, py};
        trackHead = (trackHead + 1) % 512;
        if (trackHead == 0) trackFull = true;
    }
}

// ─── DRAW HELPERS ────────────────────────────────────────────────────────────
void drawStatusBar() {
    tft.fillRect(0, 0, 240, 22, C_HEADER);
    tft.setTextColor(C_WHITE, C_HEADER);
    tft.setTextSize(1);

    // Time
    tft.setCursor(4, 7);
    tft.print(gd.timeStr);

    // REC indicator
    if (recording) {
        tft.fillCircle(110, 11, 5, C_RED);
        tft.setTextColor(C_RED, C_HEADER);
        tft.setCursor(118, 7);
        tft.print("REC");
        tft.setTextColor(C_WHITE, C_HEADER);
    }

    // Battery icon
    int bx = 195;
    tft.drawRect(bx, 6, 28, 12, C_LGREY);
    tft.fillRect(bx+28, 8, 3, 6, C_LGREY);
    int fill = (batPercent * 24) / 100;
    uint16_t bcol = batPercent > 30 ? C_ACCENT : C_RED;
    tft.fillRect(bx+2, 8, fill, 8, bcol);
    tft.setCursor(bx+1, 7);
    // Sat count
    tft.setCursor(170, 7);
    tft.setTextColor(gd.sats >= 4 ? C_ACCENT : C_WARN, C_HEADER);
    tft.print(gd.sats);
    tft.setTextColor(C_LGREY, C_HEADER);
    tft.print("S");
}

void drawSatBar() {
    // HDOP quality bar
    tft.setTextSize(1);
    tft.setTextColor(C_LGREY, C_BG);
    tft.setCursor(4, 298);
    tft.printf("HDOP:%.1f  SAT:%d  ALT:%.0fm", gd.hdop, gd.sats, gd.alt);
}

// ─── HOME SCREEN ─────────────────────────────────────────────────────────────
void drawHome() {
    tft.fillRect(0, 22, 240, 298, C_BG);

    // Speed large
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(NULL);

    // Big speed
    char spd[8];
    snprintf(spd, sizeof(spd), "%.0f", gd.speed_kmh);
    tft.setTextSize(6);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString(spd, 120, 75);
    tft.setTextSize(2);
    tft.setTextColor(C_LGREY, C_BG);
    tft.drawString("km/h", 120, 118);

    // Horizontal divider
    tft.drawFastHLine(10, 132, 220, C_DGREY);

    // Row 1: Max speed | Distance
    tft.setTextSize(1);
    tft.setTextColor(C_LGREY, C_BG);
    tft.drawString("MAX SPD", 60, 148);
    tft.drawString("DIST", 180, 148);
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, C_BG);
    char buf[20];
    snprintf(buf, sizeof(buf), "%.0f", gd.speed_max);
    tft.drawString(buf, 60, 165);
    snprintf(buf, sizeof(buf), "%.2f", gd.dist_km);
    tft.drawString(buf, 180, 165);
    tft.setTextSize(1);
    tft.setTextColor(C_DGREY, C_BG);
    tft.drawString("km/h", 60, 180);
    tft.drawString("km", 180, 180);

    // Row 2: Elapsed | Avg speed
    tft.drawFastHLine(10, 192, 220, C_DGREY);
    tft.setTextColor(C_LGREY, C_BG);
    tft.drawString("ELAPSED", 60, 208);
    tft.drawString("AVG SPD", 180, 208);
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, C_BG);
    uint32_t s = gd.elapsed_s;
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", s/3600, (s%3600)/60, s%60);
    tft.drawString(buf, 90, 225);
    snprintf(buf, sizeof(buf), "%.0f", gd.avg_speed);
    tft.drawString(buf, 185, 225);
    tft.setTextSize(1);
    tft.setTextColor(C_DGREY, C_BG);
    tft.drawString("km/h", 185, 238);

    // Row 3: Heading compass + fix
    tft.drawFastHLine(10, 248, 220, C_DGREY);
    tft.setTextColor(C_LGREY, C_BG);
    char dir[4];
    float h = imuData.heading;
    if      (h < 22.5)  strcpy(dir,"N");
    else if (h < 67.5)  strcpy(dir,"NE");
    else if (h < 112.5) strcpy(dir,"E");
    else if (h < 157.5) strcpy(dir,"SE");
    else if (h < 202.5) strcpy(dir,"S");
    else if (h < 247.5) strcpy(dir,"SW");
    else if (h < 292.5) strcpy(dir,"W");
    else if (h < 337.5) strcpy(dir,"NW");
    else                strcpy(dir,"N");
    tft.setTextSize(2);
    tft.setTextColor(C_ACCENT, C_BG);
    snprintf(buf, sizeof(buf), "%.0f° %s", h, dir);
    tft.drawString(buf, 90, 264);

    // Fix status
    tft.setTextSize(1);
    tft.setTextColor(gd.valid ? C_ACCENT : C_WARN, C_BG);
    tft.drawString(gd.valid ? "GPS FIX OK" : "SEARCHING...", 180, 264);

    // SELECT hint
    tft.setTextColor(C_DGREY, C_BG);
    tft.drawString(recording ? "[SELECT] STOP" : "[SELECT] START", 120, 285);

    drawSatBar();
    drawStatusBar();
}

// ─── TRACK MAP SCREEN ────────────────────────────────────────────────────────
void drawTrack() {
    tft.fillRect(0, 22, 240, 298, C_BG);
    tft.drawRect(8, 28, 224, 220, C_DGREY);

    if (!trackBoundsInit || (trackHead == 0 && !trackFull)) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(C_DGREY, C_BG);
        tft.setTextSize(1);
        tft.drawString("No track data", 120, 138);
        tft.drawString(recording ? "Recording..." : "Press SELECT to record", 120, 152);
    } else {
        double latRange = trackMaxLat - trackMinLat;
        double lonRange = trackMaxLon - trackMinLon;
        if (latRange < 1e-7) latRange = 1e-7;
        if (lonRange < 1e-7) lonRange = 1e-7;

        uint16_t count = trackFull ? 512 : trackHead;
        for (uint16_t i = 1; i < count; i++) {
            uint16_t prev = (trackFull) ? (trackHead + i - 1) % 512 : i - 1;
            uint16_t cur  = (trackFull) ? (trackHead + i) % 512 : i;
            tft.drawLine(trackBuf[prev].x, trackBuf[prev].y,
                         trackBuf[cur].x, trackBuf[cur].y, C_TRACK);
        }
        // Current position dot
        uint16_t last = (trackHead == 0 && trackFull) ? 511 : (trackHead - 1 + 512) % 512;
        tft.fillCircle(trackBuf[last].x, trackBuf[last].y, 4, C_RED);
    }

    // Info strip below map
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_BG);
    tft.setTextDatum(TL_DATUM);
    char buf[40];
    snprintf(buf, sizeof(buf), "Dist: %.2f km  Pts: %lu", gd.dist_km, logCount);
    tft.drawString(buf, 8, 254);
    snprintf(buf, sizeof(buf), "Spd: %.0f km/h  Max: %.0f km/h", gd.speed_kmh, gd.speed_max);
    tft.drawString(buf, 8, 268);

    tft.setTextColor(C_DGREY, C_BG);
    tft.drawString(recording ? "[SELECT] STOP REC" : "[SELECT] START REC", 8, 285);

    drawStatusBar();
}

// ─── STATISTICS SCREEN ───────────────────────────────────────────────────────
void drawStats() {
    tft.fillRect(0, 22, 240, 298, C_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);

    // Title
    tft.setTextColor(C_ACCENT, C_BG);
    tft.setCursor(8, 28);
    tft.print("=== SESSION STATS ===");
    tft.drawFastHLine(8, 40, 224, C_DGREY);

    auto row = [&](int y, const char* label, const char* val, uint16_t vcol = C_WHITE) {
        tft.setTextColor(C_LGREY, C_BG);
        tft.setCursor(10, y);
        tft.print(label);
        tft.setTextColor(vcol, C_BG);
        tft.setCursor(140, y);
        tft.print(val);
    };

    char buf[24];
    uint32_t s = gd.elapsed_s;

    row(48,  "Date",        gd.dateStr);
    row(60,  "Start Time",  gd.timeStr);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", s/3600, (s%3600)/60, s%60);
    row(72,  "Duration",    buf);
    snprintf(buf, sizeof(buf), "%.3f km", gd.dist_km);
    row(84,  "Distance",    buf);
    snprintf(buf, sizeof(buf), "%.1f km/h", gd.speed_kmh);
    row(96,  "Speed",       buf, gd.speed_kmh > 50 ? C_WARN : C_WHITE);
    snprintf(buf, sizeof(buf), "%.1f km/h", gd.speed_max);
    row(108, "Max Speed",   buf, C_ACCENT);
    snprintf(buf, sizeof(buf), "%.1f km/h", gd.avg_speed);
    row(120, "Avg Speed",   buf);
    snprintf(buf, sizeof(buf), "%.1f m", gd.alt);
    row(132, "Altitude",    buf);
    snprintf(buf, sizeof(buf), "%.6f", gd.lat);
    row(144, "Latitude",    buf);
    snprintf(buf, sizeof(buf), "%.6f", gd.lon);
    row(156, "Longitude",   buf);
    snprintf(buf, sizeof(buf), "%d sats / %.1f", gd.sats, gd.hdop);
    row(168, "Sats/HDOP",   buf, gd.hdop < 2.0f ? C_ACCENT : C_WARN);
    snprintf(buf, sizeof(buf), "%.1f° (%.0f P/R %.0f)", imuData.heading, imuData.pitch, imuData.roll);
    row(180, "Heading/P/R", buf);
    snprintf(buf, sizeof(buf), "%.1f G", imuData.accel_g);
    row(192, "Accel",       buf, imuData.accel_g > 1.3f ? C_WARN : C_WHITE);
    snprintf(buf, sizeof(buf), "%.2f V (%d%%)", batVoltage, batPercent);
    row(204, "Battery",     buf, batPercent < 20 ? C_RED : C_ACCENT);
    snprintf(buf, sizeof(buf), "%lu points", logCount);
    row(216, "Log Points",  buf);
    row(228, "File",        recording ? logFileName.c_str() : "---");

    tft.drawFastHLine(8, 240, 224, C_DGREY);
    tft.setTextColor(C_DGREY, C_BG);
    tft.setCursor(10, 248);
    tft.print("SPIFFS free: ");
    tft.print(SPIFFS.totalBytes() - SPIFFS.usedBytes());
    tft.print(" B");

    drawStatusBar();
}

// ─── FILES SCREEN ────────────────────────────────────────────────────────────
void drawFiles() {
    tft.fillRect(0, 22, 240, 298, C_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.setCursor(8, 28);
    tft.print("=== LOG FILES ===");
    tft.drawFastHLine(8, 40, 224, C_DGREY);

    int y = 48;
    File root = SPIFFS.open(LOG_DIR);
    if (!root || !root.isDirectory()) {
        tft.setTextColor(C_DGREY, C_BG);
        tft.setCursor(10, y);
        tft.print("No files found.");
    } else {
        File f = root.openNextFile();
        int idx = 0;
        while (f && y < 285) {
            if (!f.isDirectory()) {
                tft.setTextColor(C_WHITE, C_BG);
                tft.setCursor(10, y);
                tft.print(f.name());
                tft.setCursor(160, y);
                tft.setTextColor(C_LGREY, C_BG);
                float kb = f.size() / 1024.0f;
                char sz[12];
                snprintf(sz, sizeof(sz), "%.1f KB", kb);
                tft.print(sz);
                y += 14;
                idx++;
            }
            f.close();
            f = root.openNextFile();
        }
        root.close();
        if (idx == 0) {
            tft.setTextColor(C_DGREY, C_BG);
            tft.setCursor(10, 48);
            tft.print("No log files yet.");
        }
    }
    tft.drawFastHLine(8, y+4, 224, C_DGREY);
    tft.setTextColor(C_DGREY, C_BG);
    tft.setCursor(10, y+10);
    tft.printf("Used: %d / %d KB",
        (int)(SPIFFS.usedBytes()/1024),
        (int)(SPIFFS.totalBytes()/1024));

    drawStatusBar();
}

// ─── DRAG RACE LOGIC ─────────────────────────────────────────────────────────

void resetDrag() {
    dragState = DRAG_IDLE;
    drag = DragResult{};
    dragPreG = 0;
    dragLaunchSeen = false;
    screenDirty = true;
}

void armDrag() {
    resetDrag();
    dragState   = DRAG_ARMED;
    dragArmMs   = millis();
    dragPreG    = imuData.accel_g;
    screenDirty = true;
    // Three short beeps to signal armed
    buzz(2000,60); delay(60); buzz(2000,60); delay(60); buzz(2000,60);
}

// Helper: check & record a speed split
static void checkSpeedSplit(DragSplit &sp, float target, float spd, float t) {
    if (!sp.set && spd >= target) {
        sp.timeS    = t;
        sp.speedKmh = spd;
        sp.set      = true;
    }
}

// Helper: check & record a distance split
static void checkDistSplit(DragSplit &sp, float targetM, float distM, float spd, float t) {
    if (!sp.set && distM >= targetM) {
        sp.timeS    = t;
        sp.speedKmh = spd;
        sp.set      = true;
    }
}

void updateDrag() {
    if (!gd.valid) return;

    float spd = gd.speed_kmh;
    float g   = imuData.accel_g;

    // ── ARMED: detect launch ──────────────────────────────────────────────
    if (dragState == DRAG_ARMED) {
        // Track peak G while stationary (pre-launch rev)
        if (spd < DRAG_LAUNCH_KMH && g > dragPreG) dragPreG = g;

        // Launch detected: significant G spike OR speed crosses threshold
        bool gLaunch   = (g > dragPreG + DRAG_LAUNCH_G) && (g > 0.4f);
        bool spdLaunch = (spd >= DRAG_LAUNCH_KMH);

        if (gLaunch || spdLaunch) {
            dragState       = DRAG_RUNNING;
            drag.startMs    = millis();
            drag.startLat   = gd.lat;
            drag.startLon   = gd.lon;
            drag.prevLat    = gd.lat;
            drag.prevLon    = gd.lon;
            drag.hasStart   = true;
            drag.distM      = 0;
            drag.peakG      = g;
            // Reaction time = time from arm to launch
            drag.reactionTimeS = (drag.startMs - dragArmMs) / 1000.0f;
            screenDirty = true;
            buzz(3000, 80);   // launch beep
        }
        return;
    }

    // ── RUNNING ──────────────────────────────────────────────────────────
    if (dragState == DRAG_RUNNING) {
        float elapsed = (millis() - drag.startMs) / 1000.0f;

        // Accumulate distance
        float dKm = haversine(drag.prevLat, drag.prevLon, gd.lat, gd.lon);
        drag.distM   += dKm * 1000.0f;
        drag.prevLat  = gd.lat;
        drag.prevLon  = gd.lon;

        if (g > drag.peakG) drag.peakG = g;

        // Speed splits
        checkSpeedSplit(drag.s0_60,  60.0f,  spd, elapsed);
        checkSpeedSplit(drag.s0_80,  80.0f,  spd, elapsed);
        checkSpeedSplit(drag.s0_100, 100.0f, spd, elapsed);
        checkSpeedSplit(drag.s0_150, 150.0f, spd, elapsed);
        checkSpeedSplit(drag.s0_200, 200.0f, spd, elapsed);
        checkSpeedSplit(drag.s0_250, 250.0f, spd, elapsed);

        // Distance splits
        checkDistSplit(drag.s60ft, DRAG_60FT_M, drag.distM, spd, elapsed);
        checkDistSplit(drag.s8th,  DRAG_8TH_M,  drag.distM, spd, elapsed);
        checkDistSplit(drag.sqtr,  DRAG_QTR_M,  drag.distM, spd, elapsed);

        // Finish when 1/4 mile done
        if (drag.sqtr.set) {
            dragState   = DRAG_DONE;
            screenDirty = true;
            // Victory jingle
            buzz(2000,80); delay(60); buzz(2500,80); delay(60); buzz(3000,150);
        }
    }
}

// ─── DRAG SCREEN DRAW ────────────────────────────────────────────────────────
void drawDrag() {
    tft.fillRect(0, 22, 240, 298, C_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);

    // ── Header bar ───────────────────────────────────────────────────────
    tft.fillRect(0, 22, 240, 18, 0x3800);   // dark red header
    tft.setTextColor(C_YELLOW, 0x3800);
    tft.setTextSize(1);
    tft.setCursor(4, 27);
    tft.print("DRAG RACE");

    // State label top-right
    const char* stateStr[] = {"IDLE","ARMED","RUNNING","DONE"};
    uint16_t stateCol[] = {C_DGREY, C_YELLOW, C_ACCENT, C_ORANGE};
    tft.setTextColor(stateCol[dragState], 0x3800);
    tft.setCursor(160, 27);
    tft.print(stateStr[dragState]);

    // ── Big current speed ────────────────────────────────────────────────
    tft.setTextDatum(MC_DATUM);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", gd.speed_kmh);
    tft.setTextSize(5);
    tft.setTextColor(
        (dragState == DRAG_RUNNING) ? C_ACCENT :
        (dragState == DRAG_DONE)    ? C_YELLOW : C_LGREY,
        C_BG);
    tft.drawString(buf, 80, 68);
    tft.setTextSize(1);
    tft.setTextColor(C_DGREY, C_BG);
    tft.drawString("km/h", 80, 93);

    // Live elapsed timer (top-right area)
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, C_BG);
    if (dragState == DRAG_RUNNING) {
        float elapsed = (millis() - drag.startMs) / 1000.0f;
        snprintf(buf, sizeof(buf), "%.2f s", elapsed);
    } else if (dragState == DRAG_DONE && drag.sqtr.set) {
        snprintf(buf, sizeof(buf), "%.2f s", drag.sqtr.timeS);
    } else {
        snprintf(buf, sizeof(buf), "--.-s");
    }
    tft.drawString(buf, 185, 62);
    tft.setTextSize(1);
    tft.setTextColor(C_DGREY, C_BG);
    tft.drawString("ELAPSED", 185, 80);

    // Dist bar (progress to 1/4 mile)
    int barY = 102;
    tft.drawRect(10, barY, 220, 10, C_DGREY);
    float pct = constrain(drag.distM / DRAG_QTR_M, 0.0f, 1.0f);
    if (pct > 0) tft.fillRect(11, barY+1, (int)(218*pct), 8, C_ACCENT);
    // milestones
    int x60ft = 10 + (int)(220 * DRAG_60FT_M / DRAG_QTR_M);
    int x8th  = 10 + (int)(220 * DRAG_8TH_M  / DRAG_QTR_M);
    tft.drawFastVLine(x60ft, barY-3, 16, C_YELLOW);
    tft.drawFastVLine(x8th,  barY-3, 16, C_ORANGE);
    tft.setTextColor(C_YELLOW, C_BG);
    tft.setCursor(x60ft-8, barY+13); tft.print("60ft");
    tft.setTextColor(C_ORANGE, C_BG);
    tft.setCursor(x8th-8,  barY+13); tft.print("1/8");

    // ── Results table ────────────────────────────────────────────────────
    tft.drawFastHLine(8, 128, 224, C_DGREY);

    auto splitRow = [&](int y, const char* label, DragSplit &sp) {
        tft.setTextColor(C_LGREY, C_BG);
        tft.setCursor(10, y);
        tft.print(label);
        if (sp.set) {
            tft.setTextColor(C_ACCENT, C_BG);
            tft.setCursor(100, y);
            snprintf(buf, sizeof(buf), "%.3f s", sp.timeS);
            tft.print(buf);
            tft.setTextColor(C_WHITE, C_BG);
            tft.setCursor(170, y);
            snprintf(buf, sizeof(buf), "%.0f km/h", sp.speedKmh);
            tft.print(buf);
        } else {
            tft.setTextColor(C_DGREY, C_BG);
            tft.setCursor(100, y);
            tft.print("---.--- s");
        }
    };

    int ry = 133;
    splitRow(ry,      "0-60  km/h",  drag.s0_60);  ry += 12;
    splitRow(ry,      "0-80  km/h",  drag.s0_80);  ry += 12;
    splitRow(ry,      "0-100 km/h",  drag.s0_100); ry += 12;
    splitRow(ry,      "0-150 km/h",  drag.s0_150); ry += 12;
    splitRow(ry,      "0-200 km/h",  drag.s0_200); ry += 12;
    splitRow(ry,      "0-250 km/h",  drag.s0_250); ry += 12;

    tft.drawFastHLine(8, ry, 224, C_DGREY); ry += 4;
    tft.setTextColor(C_YELLOW, C_BG); tft.setCursor(10, ry);
    tft.print("─ Distance splits ─");                ry += 12;
    splitRow(ry,      "60 ft",        drag.s60ft); ry += 12;
    splitRow(ry,      "1/8 mile",     drag.s8th);  ry += 12;
    splitRow(ry,      "1/4 mile",     drag.sqtr);  ry += 12;

    tft.drawFastHLine(8, ry, 224, C_DGREY); ry += 4;

    // Reaction time
    tft.setTextColor(C_LGREY, C_BG); tft.setCursor(10, ry);
    tft.print("Reaction:");
    if (drag.reactionTimeS > 0) {
        tft.setTextColor(C_PURPLE, C_BG);
        tft.setCursor(100, ry);
        snprintf(buf, sizeof(buf), "%.3f s", drag.reactionTimeS);
        tft.print(buf);
    } else {
        tft.setTextColor(C_DGREY, C_BG);
        tft.setCursor(100, ry); tft.print("---");
    }
    ry += 12;

    // Peak G
    tft.setTextColor(C_LGREY, C_BG); tft.setCursor(10, ry);
    tft.print("Peak G:");
    tft.setTextColor(drag.peakG > 1.2f ? C_ORANGE : C_WHITE, C_BG);
    tft.setCursor(100, ry);
    snprintf(buf, sizeof(buf), "%.2f G", drag.peakG);
    tft.print(buf);

    // Bottom hint
    tft.setTextColor(C_DGREY, C_BG);
    tft.setCursor(8, 302);
    if (dragState == DRAG_IDLE || dragState == DRAG_DONE) {
        tft.print("[SELECT] ARM    [BACK] Home");
    } else {
        tft.print("[SELECT] ABORT/RESET");
    }

    drawStatusBar();
}

// ─── MAIN DRAW DISPATCHER ────────────────────────────────────────────────────
void drawScreen() {
    switch (currentScreen) {
        case SCR_HOME:  drawHome();  break;
        case SCR_TRACK: drawTrack(); break;
        case SCR_STATS: drawStats(); break;
        case SCR_GRAPH: drawGraph(); break;
        case SCR_DRAG:  drawDrag();  break;
        case SCR_FILES: drawFiles(); break;
        default: break;
    }
}

// ─── BUZZER ──────────────────────────────────────────────────────────────────
void buzz(uint16_t freq, uint16_t ms) {
    uint32_t period = 1000000UL / freq / 2;
    uint32_t end = micros() + (uint32_t)ms * 1000UL;
    while (micros() < end) {
        digitalWrite(BUZZER_PIN, HIGH);
        delayMicroseconds(period);
        digitalWrite(BUZZER_PIN, LOW);
        delayMicroseconds(period);
    }
}

// ─── HAVERSINE DISTANCE (km) ─────────────────────────────────────────────────
float haversine(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371.0;
    double dLat = (lat2 - lat1) * DEG_TO_RAD;
    double dLon = (lon2 - lon1) * DEG_TO_RAD;
    double a = sin(dLat/2)*sin(dLat/2) +
               cos(lat1*DEG_TO_RAD)*cos(lat2*DEG_TO_RAD)*
               sin(dLon/2)*sin(dLon/2);
    return (float)(R * 2.0 * atan2(sqrt(a), sqrt(1.0-a)));
}
