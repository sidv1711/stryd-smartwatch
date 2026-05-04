/*
 * watchface_impl.h  –  Watch face rendering for GC9A01 240×240 round display
 *
 * Layout:
 *
 *           ╭──────────────────────╮
 *           │      12:34:56        │  ← large digital time (center)
 *           │   ──────────────     │
 *           │  👟 8,432 steps      │  ← steps
 *           │  🌡 36.7°C  skin    │  ← skin temp
 *           │  📊 1013 hPa         │  ← pressure
 *           │  ⬆ 142 m alt        │  ← altitude
 *           │  ─────────────       │
 *           │  ☁ 18°C  Cloudy     │  ← weather (from phone)
 *           │  ⚡ BLE  🔋chg      │  ← status bar
 *           ╰──────────────────────╯
 *
 * Fonts: Adafruit GFX built-in + optional custom fonts in fonts/
 *
 * Color palette (RGB565):
 *   Background  0x0000  black
 *   Time        0xFFFF  white
 *   Steps       0x07FF  cyan
 *   Temp        0xFD60  orange
 *   Pressure    0x7BEF  light grey
 *   Altitude    0x3666  teal
 *   Weather     0xAFFF  sky blue
 *   Status      0x07E0  green / 0xF800 red
 */

#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include "watchface.h"

// ── Color constants (RGB565) ──────────────────────────────────
#define C_BG        0x0000
#define C_TIME      0xFFFF
#define C_STEPS     0x07FF
#define C_TEMP      0xFD60
#define C_PRESSURE  0x7BEF
#define C_ALTITUDE  0x2795
#define C_WEATHER   0xAFFF
#define C_GREEN     0x07E0
#define C_RED       0xF800
#define C_DIM       0x39E7

// ── Display object ────────────────────────────────────────────
// Defined exactly once, in smartwatch.ino. Declared extern in display.h
// so any TU that includes display.h (or this header) can use `tft`.
// Defining it here would cause multiple-definition linker errors as
// soon as a second .cpp/.ino includes this header.

// Display dimensions
#define DISP_W  240
#define DISP_H  240
#define CX      (DISP_W / 2)
#define CY      (DISP_H / 2)

// ─────────────────────────────────────────────────────────────
// Helper: print centered text
static void draw_centered(const char* text, int y, uint16_t color, uint8_t size) {
  tft.setTextColor(color, C_BG);
  tft.setTextSize(size);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  tft.setCursor((DISP_W - w) / 2, y);
  tft.print(text);
}

// Helper: draw a horizontal divider
static void draw_divider(int y, uint16_t color) {
  tft.drawFastHLine(40, y, 160, color);
}

// Helper: left-aligned row with label + value
static void draw_row(int y, const char* label, const char* value,
                     uint16_t label_color, uint16_t val_color) {
  // Clear row
  tft.fillRect(20, y, 200, 18, C_BG);

  tft.setTextSize(1);
  tft.setTextColor(label_color, C_BG);
  tft.setCursor(20, y);
  tft.print(label);

  tft.setTextColor(val_color, C_BG);
  // right-align value
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(value, 0, y, &x1, &y1, &w, &h);
  tft.setCursor(220 - w, y);
  tft.print(value);
}

// ─────────────────────────────────────────────────────────────
static const char* weather_description(uint8_t code) {
  // WMO weather interpretation codes (subset)
  if (code == 0)               return "Clear";
  if (code <= 3)               return "Partly Cloudy";
  if (code <= 19)              return "Foggy";
  if (code <= 29)              return "Drizzle";
  if (code <= 39)              return "Dust/Smoke";
  if (code <= 49)              return "Fog";
  if (code <= 59)              return "Drizzle";
  if (code <= 69)              return "Rain";
  if (code <= 79)              return "Snow";
  if (code <= 84)              return "Showers";
  if (code <= 94)              return "Thunderstorm";
  return "Unknown";
}

// ─────────────────────────────────────────────────────────────
void watchface_draw_initial() {
  tft.fillScreen(C_BG);
  // Draw decorative ring
  tft.drawCircle(CX, CY, 118, C_DIM);
  tft.drawCircle(CX, CY, 116, 0x18C3);
}

// ─────────────────────────────────────────────────────────────
void watchface_update(const WatchState& ws) {
  char buf[32];

  // ── Time ────────────────────────────────────────────────
  tft.fillRect(20, 22, 200, 36, C_BG);
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ws.hour, ws.minute, ws.second);
  draw_centered(buf, 22, C_TIME, 3);

  draw_divider(62, C_DIM);

  // ── Steps ───────────────────────────────────────────────
  tft.fillRect(20, 68, 200, 14, C_BG);
  snprintf(buf, sizeof(buf), "%lu steps", (unsigned long)ws.steps);
  draw_row(68, "STEPS", buf, C_DIM, C_STEPS);

  // ── Skin Temperature ────────────────────────────────────
  tft.fillRect(20, 86, 200, 14, C_BG);
  snprintf(buf, sizeof(buf), "%.1f C skin", ws.temperature);
  draw_row(86, "TEMP", buf, C_DIM, C_TEMP);

  // ── Pressure ────────────────────────────────────────────
  tft.fillRect(20, 104, 200, 14, C_BG);
  snprintf(buf, sizeof(buf), "%.0f hPa", ws.pressure);
  draw_row(104, "PRESSURE", buf, C_DIM, C_PRESSURE);

  // ── Altitude ────────────────────────────────────────────
  tft.fillRect(20, 122, 200, 14, C_BG);
  snprintf(buf, sizeof(buf), "%.0f m", ws.altitude);
  draw_row(122, "ALTITUDE", buf, C_DIM, C_ALTITUDE);

  draw_divider(140, C_DIM);

  // ── Weather ─────────────────────────────────────────────
  tft.fillRect(20, 146, 200, 14, C_BG);
  snprintf(buf, sizeof(buf), "%.0f C  %s", ws.weatherTemp,
           weather_description(ws.weatherCode));
  draw_row(146, "WEATHER", buf, C_DIM, C_WEATHER);

  draw_divider(164, C_DIM);

  // ── Status bar ──────────────────────────────────────────
  tft.fillRect(20, 170, 200, 12, C_BG);
  tft.setTextSize(1);

  // BLE status
  tft.setTextColor(ws.bleConnected ? C_GREEN : C_RED, C_BG);
  tft.setCursor(20, 170);
  tft.print(ws.bleConnected ? "BLE" : "BLE?");

  // Charging indicator
  tft.setTextColor(ws.charging ? C_GREEN : C_DIM, C_BG);
  tft.setCursor(70, 170);
  tft.print(ws.charging ? "CHG" : "BAT");
}