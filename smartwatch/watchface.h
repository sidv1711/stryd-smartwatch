#pragma once

// Aggregated sensor + watch state passed between modules
struct WatchState {
  // Sensors
  uint32_t steps;
  float    temperature;   // °C from MCP9808
  float    pressure;      // hPa from LPS25HB
  float    altitude;      // metres (derived from pressure)

  // Time (set via BLE, ticked locally)
  uint8_t  hour;
  uint8_t  minute;
  uint8_t  second;
  uint16_t year;
  uint8_t  month;         // 1-12
  uint8_t  day;           // 1-31

  // Weather (from phone via BLE)
  uint8_t  weatherCode;   // WMO weather code
  float    weatherTemp;   // °C outdoor temperature

  // Misc
  bool     charging;
  bool     bleConnected;

  // On-device HAR (filled by activity_classifier ~ every 2.5 s)
  char     activity[16];          // e.g. "walking"
  float    activityConfidence;    // 0.0–1.0 dequantized softmax
};
