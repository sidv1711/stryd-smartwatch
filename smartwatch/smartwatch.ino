#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ── Pin Definitions ──────────────────────────────────────────
// Defined before the module includes so headers can reference them.
#define PIN_DISP_CS    10
#define PIN_SPI_MOSI   11
#define PIN_SPI_SCK    12
#define PIN_DISP_DC    13
#define PIN_DISP_RST   14
#define PIN_DISP_BL    15
#define PIN_I2C_SDA    1
#define PIN_I2C_SCL    2
#define PIN_IMU_INT1   7
#define PIN_IMU_INT2   4
#define PIN_BARO_INT   5
#define PIN_CHG_STAT   6
#define PIN_TOUCH_INT  8
#define PIN_TOUCH_RST  17

#include "watchface.h"       // WatchState struct
#include "imu.h"             // LSM6DSO32
#include "barometer.h"       // LPS25HB
#include "temperature.h"     // MCP9808
#include "ble_service.h"     // BLE GATT
#include "display.h"         // declares `extern Adafruit_GC9A01A tft`
#include "watchface_impl.h"  // watchface drawing (uses tft)
#include "touch.h"           // CST816S touch controller
#include "wifi_weather.h"    // WiFi + Open-Meteo weather streaming

// ── Display object ───────────────────────────────────────────
// One-and-only definition; headers refer to it via `extern`.
Adafruit_GC9A01A tft(PIN_DISP_CS, PIN_DISP_DC, PIN_DISP_RST);

// ── Timing ───────────────────────────────────────────────────
#define SENSOR_INTERVAL_MS   1000
#define DISPLAY_INTERVAL_MS  1000
#define BLE_NOTIFY_INTERVAL  5000

// ── Self-Test ────────────────────────────────────────────────
// Set to 0 in production builds to skip the boot diagnostic.
#define BOOT_SELF_TEST       1

// ── Global State ─────────────────────────────────────────────
WatchState g_watch;

unsigned long g_lastSensorRead  = 0;
unsigned long g_lastDisplayDraw = 0;
unsigned long g_lastBleNotify   = 0;

uint8_t  g_currentScreen = 0;
bool     g_screenChanged = true;   // force full draw on first loop

// ─────────────────────────────────────────────────────────────
// Boot diagnostic: scans I2C bus, samples each sensor, sanity-
// checks readings, and runs an RGBW display fill so a tech can
// confirm hardware health without external tools. Output is on
// the serial console at 115200 baud.
void self_test() {
#if BOOT_SELF_TEST
  Serial.println("\n=== STRYD SELF-TEST ===");

  // ── Memory ───────────────────────────────────────────────
  Serial.printf("[MEM] Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("[MEM] Free PSRAM: %u bytes\n", ESP.getFreePsram());
  if (ESP.getFreePsram() < 1000000) {
    Serial.println("[MEM] !! PSRAM low or not detected — check board settings");
  }

  // ── I2C scan ─────────────────────────────────────────────
  // Expected: 0x18 (MCP9808), 0x6A (LSM6DSO32), 0x5C (LPS25HB)
  Serial.println("[I2C] Scanning...");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C]   Found 0x%02X\n", addr);
      found++;
    }
  }
  Serial.printf("[I2C] %u device(s) found\n", found);

  // ── IMU live data ────────────────────────────────────────
  // Raw counts → m/s^2 / dps using the FS settings from imu_init().
  // Chip is LSM6DSO (FS_XL=00 → ±2g, FS_G=00 → ±250 dps).
  const float ACCEL_LSB_TO_G  = 1.0f / 16384.0f;  // ±2g
  const float G_TO_MS2        = 9.80665f;
  const float GYRO_LSB_TO_DPS = 1.0f / 114.286f;  // ±250 dps (8.75 mdps/LSB)

  Serial.println("[IMU] Sampling...");
  for (int i = 0; i < 5; i++) {
    imu_sample_t s;
    if (imu_read_sample(&s)) {
      float ax = s.ax * ACCEL_LSB_TO_G * G_TO_MS2;
      float ay = s.ay * ACCEL_LSB_TO_G * G_TO_MS2;
      float az = s.az * ACCEL_LSB_TO_G * G_TO_MS2;
      float gx = s.gx * GYRO_LSB_TO_DPS;
      float gy = s.gy * GYRO_LSB_TO_DPS;
      float gz = s.gz * GYRO_LSB_TO_DPS;
      Serial.printf("[IMU]   ax=%6.2f ay=%6.2f az=%6.2f m/s^2  "
                    "gx=%6.2f gy=%6.2f gz=%6.2f dps\n",
                    ax, ay, az, gx, gy, gz);
    } else {
      Serial.println("[IMU]   read failed");
    }
    delay(100);
  }

  // Gravity sanity check + scale-diagnostic.
  // Prints raw counts and tests three FS interpretations so we can
  // identify what range the chip is actually configured at.
  Serial.printf("[IMU] WHO_AM_I = 0x%02X (expect 0x6C)\n", lsm_read(LSM_WHO_AM_I));
  Serial.printf("[IMU] CTRL1_XL = 0x%02X (wrote 0x30)\n", lsm_read(LSM_CTRL1_XL));
  Serial.printf("[IMU] CTRL2_G  = 0x%02X (wrote 0x30)\n", lsm_read(LSM_CTRL2_G));
  Serial.printf("[IMU] CTRL3_C  = 0x%02X\n",              lsm_read(LSM_CTRL3_C));

  imu_sample_t s;
  if (imu_read_sample(&s)) {
    float magsq = (float)s.ax*s.ax + (float)s.ay*s.ay + (float)s.az*s.az;
    float mag_lsb = sqrtf(magsq);
    Serial.printf("[IMU] raw  ax=%6d ay=%6d az=%6d  |raw|=%6.0f LSB\n",
                  s.ax, s.ay, s.az, mag_lsb);
    Serial.printf("[IMU] scaled |a|: ±2g=%5.2f  ±4g=%5.2f  ±8g=%5.2f  ±16g=%5.2f m/s^2\n",
                  mag_lsb / 16384.0f * G_TO_MS2,
                  mag_lsb /  8192.0f * G_TO_MS2,
                  mag_lsb /  4096.0f * G_TO_MS2,
                  mag_lsb /  2048.0f * G_TO_MS2);
    Serial.println("[IMU]   (whichever is closest to 9.8 reveals the chip's actual FS)");
  }

  // ── Barometer ────────────────────────────────────────────
  float p = barometer_get_pressure_hpa();
  float alt = barometer_altitude_from_pressure(p);
  Serial.printf("[BARO] %.2f hPa, alt=%.1f m (expect ~1010 hPa near sea level)\n",
                p, alt);
  if (p < 800.0f || p > 1100.0f) {
    Serial.println("[BARO] !! Pressure out of range");
  }

  // ── Skin Temperature ─────────────────────────────────────
  float tC = temperature_get_celsius();
  Serial.printf("[TEMP] %.2f C\n", tC);
  if (tC < -10.0f || tC > 60.0f) {
    Serial.println("[TEMP] !! Temperature out of expected wear range");
  }

  // ── Charge status ────────────────────────────────────────
  Serial.printf("[PWR] CHG_STAT = %d (LOW=charging, HIGH=not charging or no battery)\n",
                digitalRead(PIN_CHG_STAT));

  // ── Display fill test ────────────────────────────────────
  Serial.println("[DISP] RGBW fill test...");
  tft.fillScreen(GC9A01A_RED);   delay(400);
  tft.fillScreen(GC9A01A_GREEN); delay(400);
  tft.fillScreen(GC9A01A_BLUE);  delay(400);
  tft.fillScreen(GC9A01A_WHITE); delay(400);
  tft.fillScreen(GC9A01A_BLACK);
  Serial.println("[DISP] Done");

  Serial.println("=== SELF-TEST COMPLETE ===\n");
#endif
}

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[BOOT] Smartwatch starting...");

  // Charge status pin
  pinMode(PIN_CHG_STAT, INPUT_PULLUP);

  // I2C
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // Temporary I2C scan: verify CST816S touch controller is reachable
  Wire.beginTransmission(0x15);
  if (Wire.endTransmission() == 0) {
    Serial.println("[TOUCH] CST816S found at 0x15!");
  } else {
    Serial.println("[TOUCH] CST816S NOT found - check wiring");
  }

  Wire.setClock(400000);

  // Touch controller
  touch_init(PIN_TOUCH_INT, PIN_TOUCH_RST);

  // Sensors
  imu_init(PIN_IMU_INT1, PIN_IMU_INT2);
  barometer_init();
  temperature_init();

  // Display
  display_init(PIN_DISP_CS, PIN_DISP_DC, PIN_DISP_RST, PIN_DISP_BL,
               PIN_SPI_MOSI, PIN_SPI_SCK);

  // BLE
  ble_init();

  // WiFi + weather streaming (non-blocking; fetches every ~10 min once connected)
  wifi_weather_init();

  // Boot diagnostic — runs after every subsystem is initialized so
  // it can exercise sensors and display end-to-end.
  self_test();

  // Seed initial state
  memset(&g_watch, 0, sizeof(g_watch));

  watchface_draw_initial();

  Serial.println("[BOOT] Ready.");
}

// ─────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // ── Touch / screen navigation ────────────────────────────
  uint8_t gesture = touch_poll();
  if (gesture == GESTURE_SWIPE_LEFT) {
    g_currentScreen = (g_currentScreen + 1) % SCREEN_COUNT;
    g_screenChanged = true;
  } else if (gesture == GESTURE_SWIPE_RIGHT) {
    g_currentScreen = (g_currentScreen + SCREEN_COUNT - 1) % SCREEN_COUNT;
    g_screenChanged = true;
  }
  if (g_screenChanged) {
    g_screenChanged = false;
    watchface_switch_screen(g_currentScreen, g_watch);
  }

  // Read sensors every 1s
  if (now - g_lastSensorRead >= SENSOR_INTERVAL_MS) {
    g_lastSensorRead = now;

    g_watch.steps       = imu_get_steps();
    g_watch.temperature = temperature_get_celsius();
    g_watch.pressure    = barometer_get_pressure_hpa();
    g_watch.altitude    = barometer_altitude_from_pressure(g_watch.pressure);
    g_watch.charging    = (digitalRead(PIN_CHG_STAT) == LOW);

    ble_tick_time(g_watch);
  }

  // Redraw display every 1s
  if (now - g_lastDisplayDraw >= DISPLAY_INTERVAL_MS) {
    g_lastDisplayDraw = now;
    watchface_update(g_currentScreen, g_watch);
  }

  // Notify BLE clients every 5s
  if (now - g_lastBleNotify >= BLE_NOTIFY_INTERVAL) {
    g_lastBleNotify = now;
    ble_notify_all(g_watch);
  }

  // Process incoming BLE data (time sync, weather)
  ble_process(g_watch);

  // Refresh weather over WiFi (no-op until connected & interval elapsed)
  wifi_weather_poll(g_watch);
}