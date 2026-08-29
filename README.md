# RS16 V1 — Qstarz-like GPS Logger

Firmware ESP32-S3 yang meniru fungsi perangkat GPS logger Qstarz.

## Hardware

| Komponen       | Keterangan                        | Pin                    |
|----------------|-----------------------------------|------------------------|
| ESP32-S3       | MCU utama                         | —                      |
| GPS MG-902     | u-blox M9, 25 Hz, NMEA            | UART2 RX=17 TX=18      |
| IMU MPU6050    | Akselerometer + Gyro, 200 Hz      | I2C SDA=5 SCL=6        |
| LCD ILI9341    | TFT 2.8" 320×240 SPI              | CS=8 DC=9 RST=13       |
| Buzzer         | NPN C945                          | GPIO 15                |
| Baterai ADC    | Divider ÷2, Vref 3.3 V            | GPIO 14                |
| Tombol         | UP / DOWN / SELECT / BACK, active LOW | GPIO 1/2/4/7       |

## Fitur (mirip Qstarz BT-Q1000X / BT-Q818)

| Fitur                   | Keterangan                                      |
|-------------------------|-------------------------------------------------|
| **Kecepatan real-time** | Tampilan besar, update 10 Hz                    |
| **Track logging**       | CSV kompatibel format Qstarz, di SPIFFS         |
| **Statistik sesi**      | Jarak, kecepatan maks, rata-rata, durasi        |
| **Peta track**          | Tampilan track sederhana di LCD                 |
| **Daftar file log**     | Menampilkan semua file + ukuran                 |
| **Monitor baterai**     | Tegangan + persentase                           |
| **Heading / kompas**    | Dari GPS course (> 2 km/h)                      |
| **IMU Pitch/Roll**      | Dari MPU6050                                    |
| **Buzzer feedback**     | Start/stop recording                            |

## Format Log (CSV — Qstarz compatible)

```
INDEX, RCR, DATE, TIME, VALID, LATITUDE, N/S, LONGITUDE, E/W,
HEIGHT, SPEED, HEADING, DSTA, DAGE, PDOP, HDOP, VDOP,
NSAT(USED/VIEW), SAT INFO, DISTANCE
```

## Navigasi Layar

```
[UP] / [DOWN]   → Ganti halaman (Home → Track → Stats → Files)
[SELECT]        → Start / Stop recording (dari layar Home atau Track)
[BACK]          → Kembali ke Home
```

## Halaman LCD

1. **Home**  — Kecepatan besar, max speed, jarak, elapsed, heading
2. **Track** — Peta rute real-time (dot merah = posisi saat ini)
3. **Stats** — Semua parameter sesi lengkap
4. **Files** — Daftar file log di SPIFFS

## Build & Flash

```bash
pip install platformio
pio run --target upload
pio device monitor
```

## Transfer Log ke PC

File log `.csv` langsung bisa dibuka di **Qstarz GNSS Viewer** atau **Google Earth** (via GPS Babel).
