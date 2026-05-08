/*
 * display.h  –  GC9A01 240×240 round display driver
 *
 * SPI pins (from schematic):
 *   CS   → IO10   DC   → IO13
 *   MOSI → IO11   RST  → IO14
 *   SCK  → IO12   BL   → IO15
 *
 * Uses Adafruit GFX + GC9A01A library.
 * Install via Library Manager:
 *   "Adafruit GC9A01A"  (pulls in Adafruit GFX automatically)
 */

#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>

// Forward-declare the global display object used by watchface.h
extern Adafruit_GC9A01A tft;

static int _bl_pin = -1;

// ─────────────────────────────────────────────────────────────
void display_init(int cs, int dc, int rst, int bl, int mosi, int sck) {
  _bl_pin = bl;
  pinMode(bl, OUTPUT);
  digitalWrite(bl, LOW);  // backlight off until ready

  // Hardware SPI with custom pins
  SPI.begin(sck, -1, mosi, cs);  // SCK, MISO (none), MOSI, SS

  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(GC9A01A_BLACK);

  // Backlight on
  digitalWrite(bl, HIGH);

  Serial.println("[DISP] GC9A01 initialized. 240×240 round.");
}

void display_backlight(bool on) {
  if (_bl_pin >= 0) digitalWrite(_bl_pin, on ? HIGH : LOW);
}