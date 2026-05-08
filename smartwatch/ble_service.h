/*
 * ble_service.h  –  BLE GATT server
 *
 * Services and Characteristics:
 *
 * ┌─ Health Service (custom UUID)
 * │   ├─ Step Count          [NOTIFY, READ]  uint32
 * │   ├─ Skin Temperature    [NOTIFY, READ]  float (°C × 100 as int16)
 * │   ├─ Pressure            [NOTIFY, READ]  float (hPa × 10 as uint32)
 * │   └─ Altitude            [NOTIFY, READ]  float (m × 10 as int32)
 * │
 * ├─ Time Service (standard 0x1805)
 * │   └─ Current Time        [WRITE, READ]   uint8[7]: y_hi,y_lo,month,day,h,m,s
 * │
 * └─ Weather Service (custom UUID)
 *     ├─ Weather Code        [WRITE, READ]   uint8 (WMO code)
 *     └─ Outdoor Temp        [WRITE, READ]   int16 (°C × 100)
 *
 * Phone app writes time + weather; watch notifies health data.
 */

#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "watchface.h"

// ── UUIDs ─────────────────────────────────────────────────────
#define BLE_DEVICE_NAME           "SmartWatch"

// Health service
#define UUID_HEALTH_SVC           "12345678-0000-1000-8000-00805f9b34fb"
#define UUID_CHAR_STEPS           "12345678-0001-1000-8000-00805f9b34fb"
#define UUID_CHAR_SKIN_TEMP       "12345678-0002-1000-8000-00805f9b34fb"
#define UUID_CHAR_PRESSURE        "12345678-0003-1000-8000-00805f9b34fb"
#define UUID_CHAR_ALTITUDE        "12345678-0004-1000-8000-00805f9b34fb"

// Standard Current Time Service (uses the SIG base UUID 00805f9b34fb)
#define UUID_TIME_SVC             "00001805-0000-1000-8000-00805f9b34fb"
#define UUID_CHAR_CURRENT_TIME    "00002A2B-0000-1000-8000-00805f9b34fb"

// Weather service
#define UUID_WEATHER_SVC          "87654321-0000-1000-8000-00805f9b34fb"
#define UUID_CHAR_WEATHER_CODE    "87654321-0001-1000-8000-00805f9b34fb"
#define UUID_CHAR_OUTDOOR_TEMP    "87654321-0002-1000-8000-00805f9b34fb"

// ─────────────────────────────────────────────────────────────
static BLEServer*         _ble_server       = nullptr;
static BLECharacteristic* _char_steps       = nullptr;
static BLECharacteristic* _char_skin_temp   = nullptr;
static BLECharacteristic* _char_pressure    = nullptr;
static BLECharacteristic* _char_altitude    = nullptr;
static BLECharacteristic* _char_time        = nullptr;
static BLECharacteristic* _char_wx_code     = nullptr;
static BLECharacteristic* _char_wx_temp     = nullptr;

static bool               _ble_connected    = false;
static bool               _time_set         = false;

// Seconds elapsed since last time sync (for local tick)
static unsigned long      _time_sync_millis = 0;
static uint8_t            _synced_h = 0, _synced_m = 0, _synced_s = 0;
static uint16_t           _synced_year  = 0;
static uint8_t            _synced_month = 0;
static uint8_t            _synced_day   = 0;

// ─────────────────────────────────────────────────────────────
class BleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    _ble_connected = true;
    Serial.println("[BLE] Client connected.");
  }
  void onDisconnect(BLEServer* s) override {
    _ble_connected = false;
    Serial.println("[BLE] Client disconnected. Restarting advertising.");
    s->startAdvertising();
  }
};

// Write handler for Current Time characteristic
class TimeWriteCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    std::string val = c->getValue().c_str();
    if (val.size() >= 7) {
      // Payload: year_hi, year_lo, month, day, hour, minute, second
      _synced_year  = ((uint8_t)val[0] << 8) | (uint8_t)val[1];
      _synced_month = (uint8_t)val[2];
      _synced_day   = (uint8_t)val[3];
      _synced_h     = (uint8_t)val[4];
      _synced_m     = (uint8_t)val[5];
      _synced_s     = (uint8_t)val[6];
      _time_sync_millis = millis();
      _time_set = true;
      Serial.printf("[BLE] Time synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                    _synced_year, _synced_month, _synced_day,
                    _synced_h, _synced_m, _synced_s);
    }
  }
};

// Write handler for weather
class WeatherCodeCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    std::string val = c->getValue().c_str();
    if (val.size() >= 1) {
      Serial.printf("[BLE] Weather code: %d\n", (uint8_t)val[0]);
    }
  }
};

class WeatherTempCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    std::string val = c->getValue().c_str();
    if (val.size() >= 2) {
      int16_t t = (int16_t)(((uint8_t)val[1] << 8) | (uint8_t)val[0]);
      Serial.printf("[BLE] Outdoor temp: %.2f°C\n", t / 100.0f);
    }
  }
};

// ─────────────────────────────────────────────────────────────
static BLECharacteristic* make_char(BLEService* svc, const char* uuid,
                                    uint32_t props) {
  BLECharacteristic* c = svc->createCharacteristic(uuid, props);
  if (props & BLECharacteristic::PROPERTY_NOTIFY) {
    c->addDescriptor(new BLE2902());
  }
  return c;
}

void ble_init() {
  BLEDevice::init(BLE_DEVICE_NAME);
  _ble_server = BLEDevice::createServer();
  _ble_server->setCallbacks(new BleServerCallbacks());

  // ── Health Service ─────────────────────────────────────
  BLEService* health_svc = _ble_server->createService(UUID_HEALTH_SVC);

  _char_steps = make_char(health_svc, UUID_CHAR_STEPS,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

  _char_skin_temp = make_char(health_svc, UUID_CHAR_SKIN_TEMP,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

  _char_pressure = make_char(health_svc, UUID_CHAR_PRESSURE,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

  _char_altitude = make_char(health_svc, UUID_CHAR_ALTITUDE,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

  health_svc->start();

  // ── Time Service ───────────────────────────────────────
  BLEService* time_svc = _ble_server->createService(UUID_TIME_SVC);
  _char_time = make_char(time_svc, UUID_CHAR_CURRENT_TIME,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  _char_time->setCallbacks(new TimeWriteCallback());
  time_svc->start();

  // ── Weather Service ────────────────────────────────────
  BLEService* wx_svc = _ble_server->createService(UUID_WEATHER_SVC);
  _char_wx_code = make_char(wx_svc, UUID_CHAR_WEATHER_CODE,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  _char_wx_code->setCallbacks(new WeatherCodeCallback());

  _char_wx_temp = make_char(wx_svc, UUID_CHAR_OUTDOOR_TEMP,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  _char_wx_temp->setCallbacks(new WeatherTempCallback());
  wx_svc->start();

  // ── Advertising ────────────────────────────────────────
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(UUID_HEALTH_SVC);
  adv->addServiceUUID(UUID_TIME_SVC);
  adv->addServiceUUID(UUID_WEATHER_SVC);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising as '" BLE_DEVICE_NAME "'");
}

// ─────────────────────────────────────────────────────────────
// Called every loop to update time from BLE-synced base + elapsed seconds
void ble_tick_time(WatchState& ws) {
  if (!_time_set) return;

  unsigned long elapsed_s = (millis() - _time_sync_millis) / 1000UL;
  uint32_t total_s = (uint32_t)_synced_h * 3600
                   + (uint32_t)_synced_m * 60
                   + _synced_s
                   + elapsed_s;

  total_s %= 86400;  // wrap at midnight
  ws.hour   = total_s / 3600;
  ws.minute = (total_s % 3600) / 60;
  ws.second = total_s % 60;
  ws.year   = _synced_year;
  ws.month  = _synced_month;
  ws.day    = _synced_day;
}

// ─────────────────────────────────────────────────────────────
// Notify all subscribed clients with latest sensor data
void ble_notify_all(const WatchState& ws) {
  if (!_ble_connected) return;

  // Steps — uint32 little-endian
  uint8_t step_buf[4];
  step_buf[0] = ws.steps & 0xFF;
  step_buf[1] = (ws.steps >> 8)  & 0xFF;
  step_buf[2] = (ws.steps >> 16) & 0xFF;
  step_buf[3] = (ws.steps >> 24) & 0xFF;
  _char_steps->setValue(step_buf, 4);
  _char_steps->notify();

  // Skin temp — int16 (°C × 100)
  int16_t t_raw = (int16_t)(ws.temperature * 100.0f);
  uint8_t temp_buf[2] = { (uint8_t)(t_raw & 0xFF), (uint8_t)(t_raw >> 8) };
  _char_skin_temp->setValue(temp_buf, 2);
  _char_skin_temp->notify();

  // Pressure — uint32 (hPa × 10)
  uint32_t p_raw = (uint32_t)(ws.pressure * 10.0f);
  uint8_t pres_buf[4] = {
    (uint8_t)(p_raw & 0xFF), (uint8_t)(p_raw >> 8),
    (uint8_t)(p_raw >> 16),  (uint8_t)(p_raw >> 24)
  };
  _char_pressure->setValue(pres_buf, 4);
  _char_pressure->notify();

  // Altitude — int32 (m × 10)
  int32_t a_raw = (int32_t)(ws.altitude * 10.0f);
  uint8_t alt_buf[4] = {
    (uint8_t)(a_raw & 0xFF), (uint8_t)(a_raw >> 8),
    (uint8_t)(a_raw >> 16),  (uint8_t)(a_raw >> 24)
  };
  _char_altitude->setValue(alt_buf, 4);
  _char_altitude->notify();
}

// ─────────────────────────────────────────────────────────────
// Read weather/time written by phone app into WatchState
void ble_process(WatchState& ws) {
  ws.bleConnected = _ble_connected;

  if (_char_wx_code) {
    std::string v = _char_wx_code->getValue().c_str();
    if (!v.empty()) ws.weatherCode = (uint8_t)v[0];
  }
  if (_char_wx_temp) {
    std::string v = _char_wx_temp->getValue().c_str();
    if (v.size() >= 2) {
      int16_t t = (int16_t)(((uint8_t)v[1] << 8) | (uint8_t)v[0]);
      ws.weatherTemp = t / 100.0f;
    }
  }
}

bool ble_is_connected() { return _ble_connected; }