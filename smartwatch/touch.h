/*
 * touch.h  –  CST816S capacitive touch controller driver
 *
 * I2C address : 0x15
 * INT pin     : GPIO 8  (falling edge on touch event)
 * RST pin     : GPIO 17
 *
 * Gesture register 0x01 values:
 *   0x00  No gesture
 *   0x01  Swipe up
 *   0x02  Swipe down
 *   0x03  Swipe left
 *   0x04  Swipe right
 *   0x05  Single tap
 */

#pragma once
#include <Arduino.h>
#include <Wire.h>

#define CST816S_ADDR         0x15
#define CST816S_REG_GESTURE  0x01
#define CST816S_REG_IRQCTL   0xFA   // bit6 EnMotion, bit5 EnChange, bit4 EnTouch, bit0 OnceWLP
#define CST816S_REG_MOTIONMASK 0xEC // motion gating
#define CST816S_REG_NORSCANPER 0xEE // scan period (units of 10ms), default 0x01; larger = less sensitive
#define CST816S_REG_MOTIONSLANGLE 0xEF // swipe angle threshold

#define GESTURE_NONE         0x00
#define GESTURE_SWIPE_UP     0x01
#define GESTURE_SWIPE_DOWN   0x02
#define GESTURE_SWIPE_LEFT   0x03
#define GESTURE_SWIPE_RIGHT  0x04

// Minimum ms between accepted gestures. Increase if swipes still register twice.
#define TOUCH_DEBOUNCE_MS    300

static volatile bool _touch_irq_flag = false;
static unsigned long _touch_last_event_ms = 0;

static void IRAM_ATTR _touch_isr() {
    _touch_irq_flag = true;
}

static void touch_init(int int_pin, int rst_pin) {
    pinMode(rst_pin, OUTPUT);
    digitalWrite(rst_pin, LOW);
    delay(5);
    digitalWrite(rst_pin, HIGH);
    delay(50);

    pinMode(int_pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(int_pin), _touch_isr, FALLING);

    // Fire INT only on motion/gesture events (bit6), not every touch/change.
    Wire.beginTransmission(CST816S_ADDR);
    Wire.write(CST816S_REG_IRQCTL);
    Wire.write(0x40);
    Wire.endTransmission();

    // Slow scan period (default 0x01 = 10ms). 0x05 = 50ms — less twitchy.
    Wire.beginTransmission(CST816S_ADDR);
    Wire.write(CST816S_REG_NORSCANPER);
    Wire.write(0x05);
    Wire.endTransmission();

    Serial.println("[TOUCH] CST816S initialized (motion-only, debounced)");
}

// Returns gesture code if an event fired since last call, GESTURE_NONE otherwise.
// Safe to call from loop(); does not block.
static uint8_t touch_poll() {
    noInterrupts();
    bool fired = _touch_irq_flag;
    _touch_irq_flag = false;
    interrupts();

    if (!fired) return GESTURE_NONE;

    Wire.beginTransmission(CST816S_ADDR);
    Wire.write(CST816S_REG_GESTURE);
    if (Wire.endTransmission(false) != 0) return GESTURE_NONE;
    Wire.requestFrom(CST816S_ADDR, (uint8_t)1);
    uint8_t g = Wire.available() ? Wire.read() : GESTURE_NONE;
    if (g == GESTURE_NONE) return GESTURE_NONE;

    unsigned long now = millis();
    if (now - _touch_last_event_ms < TOUCH_DEBOUNCE_MS) return GESTURE_NONE;
    _touch_last_event_ms = now;
    return g;
}
