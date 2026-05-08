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

#define GESTURE_NONE         0x00
#define GESTURE_SWIPE_UP     0x01
#define GESTURE_SWIPE_DOWN   0x02
#define GESTURE_SWIPE_LEFT   0x03
#define GESTURE_SWIPE_RIGHT  0x04

static volatile bool _touch_irq_flag = false;

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
    Serial.println("[TOUCH] CST816S initialized");
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
    return Wire.available() ? Wire.read() : GESTURE_NONE;
}
