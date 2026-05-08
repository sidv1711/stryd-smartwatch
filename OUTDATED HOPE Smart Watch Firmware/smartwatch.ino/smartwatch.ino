#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "watchface.h"       // WatchState struct
#include "imu.h"             // LSM6DSO32
#include "barometer.h"       // LPS25HB
#include "temperature.h"     // MCP9808
#include "ble_service.h"     // BLE GATT
#include "watchface_impl.h"  // display drawing (also defines tft object)

// ── Pin Definitions ──────────────────────────────────────────
#define PIN_DISP_CS    10
#define PIN_SPI_MOSI   11
#define PIN_SPI_SCK    12
#define PIN_DISP_DC    13
#define PIN_DISP_RST   14
#define PIN_DISP_BL    15
#define PIN_I2C_SDA    39
#define PIN_I2C_SCL    38
#define PIN_IMU_INT1   7
#define PIN_IMU_INT2   4
#define PIN_BARO_INT   5
#define PIN_CHG_STAT   6

// ── Timing ───────────────────────────────────────────────────
#define SENSOR_INTERVAL_MS   1000
#define DISPLAY_INTERVAL_MS  1000
#define BLE_NOTIFY_INTERVAL  5000

// ── Global State ─────────────────────────────────────────────
WatchState g_watch;

unsigned long g_lastSensorRead  = 0;
unsigned long g_lastDisplayDraw = 0;
unsigned long g_lastBleNotify   = 0;

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[BOOT] Smartwatch starting...");

  // Charge status pin
  pinMode(PIN_CHG_STAT, INPUT_PULLUP);

  // I2C
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  // Sensors
  imu_init(PIN_IMU_INT1, PIN_IMU_INT2);
  barometer_init();
  temperature_init();

  // Display
  display_init(PIN_DISP_CS, PIN_DISP_DC, PIN_DISP_RST, PIN_DISP_BL,
               PIN_SPI_MOSI, PIN_SPI_SCK);

  // BLE
  ble_init();

  // Seed initial state
  memset(&g_watch, 0, sizeof(g_watch));

  watchface_draw_initial();

  Serial.println("[BOOT] Ready.");
}

// ─────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // Read sensors every 1s
  if (now - g_lastSensorRead >= SENSOR_INTERVAL_MS) {
    g_lastSensorRead = now;

    g_watch.steps       = imu_get_steps();
    g_watch.temperature = temperature_get_celsius();
    g_watch.pressure    = barometer_get_pressure_hpa();
    g_watch.altitude    = barometer_get_altitude_m();
    g_watch.charging    = (digitalRead(PIN_CHG_STAT) == LOW);

    ble_tick_time(g_watch);
  }

  // Redraw display every 1s
  if (now - g_lastDisplayDraw >= DISPLAY_INTERVAL_MS) {
    g_lastDisplayDraw = now;
    watchface_update(g_watch);
  }

  // Notify BLE clients every 5s
  if (now - g_lastBleNotify >= BLE_NOTIFY_INTERVAL) {
    g_lastBleNotify = now;
    ble_notify_all(g_watch);
  }

  // Process incoming BLE data (time sync, weather)
  ble_process(g_watch);
}