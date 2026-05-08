/*
 * temperature.h  –  MCP9808 driver
 *
 * I2C address: A0=A1=A2=GND → 0x18
 * Returns temperature in °C (0.0625°C resolution)
 */

#pragma once
#include <Arduino.h>
#include <Wire.h>

#define MCP_ADDR         0x18
#define MCP_REG_TEMP     0x05
#define MCP_REG_CONFIG   0x01
#define MCP_REG_MFR_ID   0x06   // should return 0x0054

// ─────────────────────────────────────────────────────────────
static void mcp_write16(uint8_t reg, uint16_t val) {
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(reg);
  Wire.write((val >> 8) & 0xFF);
  Wire.write(val & 0xFF);
  Wire.endTransmission();
}

static uint16_t mcp_read16(uint8_t reg) {
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(MCP_ADDR, 2);
  if (Wire.available() >= 2) {
    uint16_t hi = Wire.read();
    uint16_t lo = Wire.read();
    return (hi << 8) | lo;
  }
  return 0xFFFF;
}

// ─────────────────────────────────────────────────────────────
void temperature_init() {
  delay(5);
  uint16_t mfr = mcp_read16(MCP_REG_MFR_ID);
  if (mfr != 0x0054) {
    Serial.printf("[TEMP] MFR ID = 0x%04X (expected 0x0054)\n", mfr);
  } else {
    Serial.println("[TEMP] MCP9808 found.");
  }
  // Continuous conversion, default config (±0.25°C accuracy)
  mcp_write16(MCP_REG_CONFIG, 0x0000);
}

// ─────────────────────────────────────────────────────────────
float temperature_get_celsius() {
  uint16_t raw = mcp_read16(MCP_REG_TEMP);
  if (raw == 0xFFFF) return 0.0f;

  // Clear flag bits [15:13]
  raw &= 0x1FFF;

  float temp = (raw & 0x0FFF) / 16.0f;
  if (raw & 0x1000) temp -= 256.0f;  // negative temperature

  return temp;
}