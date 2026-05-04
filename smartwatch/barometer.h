/*
 * barometer.h  –  LPS25HB driver
 *
 * I2C address: SDO/SA0 → GND = 0x5C  (per schematic pin 5 to GND)
 * Provides pressure (hPa) and derived altitude (metres)
 */

#pragma once
#include <Arduino.h>
#include <Wire.h>

#define LPS_ADDR        0x5C
#define LPS_WHO_AM_I    0x0F   // should return 0xBD
#define LPS_CTRL_REG1   0x20
#define LPS_CTRL_REG2   0x21
#define LPS_STATUS_REG  0x27
#define LPS_PRES_OUT_XL 0x28
#define LPS_PRES_OUT_L  0x29
#define LPS_PRES_OUT_H  0x2A

// Sea-level reference pressure (can be calibrated)
#define SEA_LEVEL_HPA   1013.25f

// ─────────────────────────────────────────────────────────────
static uint8_t lps_read(uint8_t reg) {
  Wire.beginTransmission(LPS_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(LPS_ADDR, 1);
  return Wire.available() ? Wire.read() : 0xFF;
}

static void lps_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(LPS_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

// ─────────────────────────────────────────────────────────────
void barometer_init() {
  delay(5);
  uint8_t who = lps_read(LPS_WHO_AM_I);
  if (who != 0xBD) {
    Serial.printf("[BARO] WHO_AM_I = 0x%02X (expected 0xBD)\n", who);
  } else {
    Serial.println("[BARO] LPS25HB found.");
  }

  // Power on, 25 Hz ODR, block data update
  // CTRL_REG1: PD=1, ODR=100 (25Hz), DIFF_EN=0, BDU=1, RESET_AZ=0, SIM=0
  // 0xC4 = 1100_0100  →  PD=1 (bit7), ODR=100 (bits6:4 = 25Hz), BDU=1 (bit2)
  // BDU prevents torn reads of the 24-bit pressure during multi-byte fetch.
  lps_write(LPS_CTRL_REG1, 0xC4);

  // Internal averaging: 512 pressure samples, 64 temp samples
  // CTRL_REG2: default (FIFO off)
}

// ─────────────────────────────────────────────────────────────
float barometer_get_pressure_hpa() {
  // Auto-increment read: XL, L, H (set bit 7 for multi-byte)
  Wire.beginTransmission(LPS_ADDR);
  Wire.write(LPS_PRES_OUT_XL | 0x80);
  Wire.endTransmission(false);
  Wire.requestFrom(LPS_ADDR, 3);
  if (Wire.available() >= 3) {
    uint8_t xl = Wire.read();
    uint8_t l  = Wire.read();
    uint8_t h  = Wire.read();
    int32_t raw = (int32_t)((h << 16) | (l << 8) | xl);
    // Sign-extend from 24-bit
    if (raw & 0x800000) raw |= 0xFF000000;
    return raw / 4096.0f;  // datasheet: 1 hPa = 4096 LSB
  }
  return 0.0f;
}

// Convert an already-fetched pressure reading to altitude.
// Use this when you've called barometer_get_pressure_hpa() once
// and want both values without paying for a second I2C transaction.
float barometer_altitude_from_pressure(float p) {
  if (p <= 0) return 0.0f;
  return 44330.0f * (1.0f - powf(p / SEA_LEVEL_HPA, 0.1903f));
}

float barometer_get_altitude_m() {
  return barometer_altitude_from_pressure(barometer_get_pressure_hpa());
}