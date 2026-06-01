/*
 * watchface_impl.h  –  3-screen watch face for GC9A01 240×240 round display
 *
 * Screen 0  Clock       : large time, date, BLE/charge status
 * Screen 1  Health      : step count, progress bar toward 10 000 goal, skin temp
 * Screen 2  Environment : outdoor weather + temp, pressure, altitude
 *
 * Navigation: CST816S swipe-left → next screen, swipe-right → previous screen.
 * Dot indicators at y=214 show the active screen.
 *
 * Color palette (RGB565):
 *   Background  0x0000  black
 *   Time/text   0xFFFF  white
 *   Steps       0x07FF  cyan
 *   Skin temp   0xFD60  orange
 *   Pressure    0x7BEF  light grey
 *   Altitude    0x2795  teal
 *   Weather     0xAFFF  sky blue
 *   Green       0x07E0  / Red 0xF800
 *   Dim         0x39E7  dark grey
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

// ── Display geometry ──────────────────────────────────────────
#define DISP_W       240
#define DISP_H       240
#define CX           (DISP_W / 2)
#define CY           (DISP_H / 2)

#define SCREEN_COUNT 4
#define STEP_GOAL    10000UL

// ── Low-level helpers ─────────────────────────────────────────

static void draw_centered(const char* text, int y, uint16_t color, uint8_t size) {
    tft.setTextColor(color, C_BG);
    tft.setTextSize(size);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
    tft.setCursor((DISP_W - w) / 2, y);
    tft.print(text);
}

static void draw_divider(int y, uint16_t color) {
    tft.drawFastHLine(40, y, 160, color);
}

static void draw_row(int y, const char* label, const char* value,
                     uint16_t label_color, uint16_t val_color) {
    tft.fillRect(20, y, 200, 18, C_BG);
    tft.setTextSize(1);
    tft.setTextColor(label_color, C_BG);
    tft.setCursor(20, y);
    tft.print(label);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(value, 0, y, &x1, &y1, &w, &h);
    tft.setTextColor(val_color, C_BG);
    tft.setCursor(220 - w, y);
    tft.print(value);
}

// Three dots at bottom; active dot is filled white, others are dim outlines
static void draw_dots(uint8_t active) {
    const int y  = 214;
    const int r  = 4;
    const int sp = 14;              // spacing between dot centres
    const int x0 = CX - sp * (SCREEN_COUNT - 1) / 2;   // leftmost dot, kept symmetric

    for (int i = 0; i < SCREEN_COUNT; i++) {
        int x = x0 + i * sp;
        tft.fillCircle(x, y, r + 1, C_BG);     // clear
        if (i == (int)active) {
            tft.fillCircle(x, y, r, C_TIME);
        } else {
            tft.drawCircle(x, y, r, C_DIM);
        }
    }
}

static void draw_progress_bar(int x, int y, int w, int h,
                               float progress, uint16_t fill_color) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    int inner_w = w - 2;
    int filled  = (int)(progress * inner_w);
    tft.fillRect(x, y, w, h, C_BG);
    tft.drawRect(x, y, w, h, C_DIM);
    if (filled > 0) tft.fillRect(x + 1, y + 1, filled, h - 2, fill_color);
}

// ── Data helpers ──────────────────────────────────────────────

static const char* weather_description(uint8_t code) {
    if (code == 0)  return "Clear";
    if (code <= 3)  return "Partly Cloudy";
    if (code <= 19) return "Foggy";
    if (code <= 29) return "Drizzle";
    if (code <= 39) return "Dust/Smoke";
    if (code <= 49) return "Fog";
    if (code <= 59) return "Drizzle";
    if (code <= 69) return "Rain";
    if (code <= 79) return "Snow";
    if (code <= 84) return "Showers";
    if (code <= 94) return "Thunderstorm";
    return "Unknown";
}

// Tomohiko Sakamoto's algorithm; returns 0=Sun … 6=Sat
static int day_of_week(int y, int m, int d) {
    static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (m < 3) y--;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

// Format uint32 steps with thousands comma (e.g. "8,432")
static void fmt_steps(char* buf, size_t len, uint32_t s) {
    if (s >= 1000) {
        snprintf(buf, len, "%lu,%03lu",
                 (unsigned long)(s / 1000), (unsigned long)(s % 1000));
    } else {
        snprintf(buf, len, "%lu", (unsigned long)s);
    }
}

// ─────────────────────────────────────────────────────────────
// Screen 0 – Clock
// Layout (240×240):
//   y= 75  HH:MM  size-4 white
//   y=120  Day DD Mon  size-2 dim
//   y=176  BLE / CHG  size-2
//   y=214  dot indicators
static void draw_screen0(const WatchState& ws) {
    char buf[32];

    // Time
    tft.fillRect(18, 68, 204, 42, C_BG);
    snprintf(buf, sizeof(buf), "%02d:%02d", ws.hour, ws.minute);
    draw_centered(buf, 75, C_TIME, 4);

    // Date
    tft.fillRect(30, 112, 180, 22, C_BG);
    if (ws.year > 0 && ws.month >= 1 && ws.month <= 12 && ws.day >= 1) {
        static const char* mn[] = {
            "Jan","Feb","Mar","Apr","May","Jun",
            "Jul","Aug","Sep","Oct","Nov","Dec"
        };
        static const char* dn[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        int dow = day_of_week(ws.year, ws.month, ws.day);
        snprintf(buf, sizeof(buf), "%s %d %s %d",
                 dn[dow], ws.day, mn[ws.month - 1], ws.year);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    draw_centered(buf, 118, C_DIM, 1);

    // BLE + charge status
    tft.fillRect(50, 169, 140, 22, C_BG);
    tft.setTextSize(2);
    tft.setTextColor(ws.bleConnected ? C_GREEN : C_RED, C_BG);
    tft.setCursor(56, 176);
    tft.print(ws.bleConnected ? "BLE" : "BLE?");
    tft.setTextColor(ws.charging ? C_GREEN : C_DIM, C_BG);
    tft.setCursor(128, 176);
    tft.print(ws.charging ? "CHG" : "BAT");
}

// ─────────────────────────────────────────────────────────────
// Screen 1 – Health
// Layout:
//   y= 60  "STEPS" label  size-1 dim
//   y= 78  step count     size-4 cyan
//   y=128  progress bar   160×14
//   y=147  "N / 10,000"   size-1 dim
//   y=168  "SKIN TEMP"    size-1 dim
//   y=182  temperature    size-2 orange
//   y=214  dots
static void draw_screen1(const WatchState& ws) {
    char buf[32];

    // Label
    tft.fillRect(20, 55, 200, 14, C_BG);
    draw_centered("STEPS", 58, C_DIM, 1);

    // Step count
    tft.fillRect(18, 72, 204, 42, C_BG);
    fmt_steps(buf, sizeof(buf), ws.steps);
    draw_centered(buf, 78, C_STEPS, 4);

    // Progress bar toward daily goal
    float progress = (float)ws.steps / (float)STEP_GOAL;
    draw_progress_bar(40, 128, 160, 14, progress, C_STEPS);

    // Goal fraction text
    tft.fillRect(20, 145, 200, 14, C_BG);
    char sbuf[16];
    fmt_steps(sbuf, sizeof(sbuf), ws.steps);
    snprintf(buf, sizeof(buf), "%s / 10,000", sbuf);
    draw_centered(buf, 147, C_DIM, 1);

    // Skin temperature
    tft.fillRect(20, 162, 200, 14, C_BG);
    draw_centered("SKIN TEMP", 164, C_DIM, 1);
    tft.fillRect(20, 178, 200, 22, C_BG);
    snprintf(buf, sizeof(buf), "%.1f C", ws.temperature);
    draw_centered(buf, 182, C_TEMP, 2);
}

// ─────────────────────────────────────────────────────────────
// Screen 2 – Environment
// Layout:
//   y= 65  weather description  size-2 sky-blue
//   y= 93  outdoor temp         size-4 sky-blue
//   y=138  divider
//   y=150  pressure row         size-1
//   y=170  altitude row         size-1
//   y=214  dots
static void draw_screen2(const WatchState& ws) {
    char buf[32];

    // Weather description
    tft.fillRect(20, 58, 200, 22, C_BG);
    draw_centered(weather_description(ws.weatherCode), 64, C_WEATHER, 2);

    // Outdoor temperature (large)
    tft.fillRect(18, 85, 204, 42, C_BG);
    snprintf(buf, sizeof(buf), "%.0f C", ws.weatherTemp);
    draw_centered(buf, 92, C_WEATHER, 4);

    draw_divider(138, C_DIM);

    // Pressure
    snprintf(buf, sizeof(buf), "%.0f hPa", ws.pressure);
    draw_row(150, "PRESSURE", buf, C_DIM, C_PRESSURE);

    // Altitude
    snprintf(buf, sizeof(buf), "%.0f m", ws.altitude);
    draw_row(170, "ALTITUDE", buf, C_DIM, C_ALTITUDE);
}

// Screen 3 — Activity: predicted exercise + confidence from the on-device classifier
static void draw_screen3(const WatchState& ws) {
    char buf[32];

    tft.fillRect(20, 58, 200, 22, C_BG);
    draw_centered("ACTIVITY", 64, C_DIM, 2);

    // Activity name, large + green
    tft.fillRect(10, 92, 220, 38, C_BG);
    const char* act = ws.activity[0] ? ws.activity : "--";
    draw_centered(act, 98, C_GREEN, 3);

    draw_divider(138, C_DIM);

    tft.fillRect(20, 150, 200, 36, C_BG);
    if (ws.activity[0]) {
        snprintf(buf, sizeof(buf), "%.0f%%", ws.activityConfidence * 100.0f);
        draw_centered(buf, 156, C_STEPS, 3);
        draw_centered("CONFIDENCE", 184, C_DIM, 1);
    } else {
        draw_centered("warming up...", 162, C_DIM, 2);
    }
}

// ─────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────

// Called once at boot to blank the display and draw the bezel ring.
void watchface_draw_initial() {
    tft.fillScreen(C_BG);
    tft.drawCircle(CX, CY, 118, C_DIM);
    tft.drawCircle(CX, CY, 116, 0x18C3);
}

// Full redraw: clear, ring, dots, then the requested screen.
// Call whenever the active screen changes.
void watchface_switch_screen(uint8_t screen, const WatchState& ws) {
    tft.fillScreen(C_BG);
    tft.drawCircle(CX, CY, 118, C_DIM);
    tft.drawCircle(CX, CY, 116, 0x18C3);
    draw_dots(screen);
    switch (screen) {
        case 0: draw_screen0(ws); break;
        case 1: draw_screen1(ws); break;
        case 2: draw_screen2(ws); break;
        case 3: draw_screen3(ws); break;
    }
}

// Partial update: refresh only the data areas of the current screen.
// Call on every sensor-update tick (no full clear, no dot redraw).
void watchface_update(uint8_t screen, const WatchState& ws) {
    switch (screen) {
        case 0: draw_screen0(ws); break;
        case 1: draw_screen1(ws); break;
        case 2: draw_screen2(ws); break;
        case 3: draw_screen3(ws); break;
    }
}
