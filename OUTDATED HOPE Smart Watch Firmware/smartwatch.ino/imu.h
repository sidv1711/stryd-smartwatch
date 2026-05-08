/*
 * imu.h / imu.cpp  –  LSM6DSO32 driver
 *
 * Uses I2C (SDO/SA0 tied to GND → address 0x6A)
 * Configures accelerometer for step counting via embedded pedometer.
 * INT1 → step-detected interrupt (GPIO 7)
 * INT2 → wrist-tilt / free-fall (GPIO 4)  — unused for now
 */

#pragma once
#include <Arduino.h>
#include <Wire.h>

// ── Register Map ─────────────────────────────────────────────
#define LSM_ADDR          0x6A
#define LSM_WHO_AM_I      0x0F   // should return 0x6C
#define LSM_CTRL1_XL      0x10   // accel ODR + FS
#define LSM_CTRL2_G       0x11   // gyro ODR + FS
#define LSM_CTRL3_C       0x12
#define LSM_CTRL10_C      0x19   // embedded functions enable
#define LSM_TAP_CFG0      0x56   // latch + activity
#define LSM_TAP_CFG2      0x58   // interrupt enable
#define LSM_EMB_FUNC_EN_A 0x04   // pedometer enable (bank B)
#define LSM_STEP_COUNTER_L 0x62  // 16-bit step count (little-endian)
#define LSM_STEP_COUNTER_H 0x63
#define LSM_FUNC_CFG_ACCESS 0x01 // switch to embedded func bank

// Embedded function register bank
#define LSM_EMB_FUNC_EN_A_BANK  0x04
#define LSM_INT1_CTRL_EMB       0x0A  // route step to INT1

static int  _imu_int1_pin = -1;
static int  _imu_int2_pin = -1;
static volatile bool _step_flag = false;

// ─────────────────────────────────────────────────────────────
static uint8_t lsm_read(uint8_t reg) {
  Wire.beginTransmission(LSM_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(LSM_ADDR, 1);
  return Wire.available() ? Wire.read() : 0xFF;
}

static void lsm_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(LSM_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static void lsm_write_emb(uint8_t reg, uint8_t val) {
  // Switch to embedded function register bank
  lsm_write(LSM_FUNC_CFG_ACCESS, 0x80);
  lsm_write(reg, val);
  lsm_write(LSM_FUNC_CFG_ACCESS, 0x00);  // back to user bank
}

static void IRAM_ATTR imu_step_isr() {
  _step_flag = true;
}

// ─────────────────────────────────────────────────────────────
void imu_init(int int1_pin, int int2_pin) {
  _imu_int1_pin = int1_pin;
  _imu_int2_pin = int2_pin;

  delay(10);
  uint8_t who = lsm_read(LSM_WHO_AM_I);
  if (who != 0x6C) {
    Serial.printf("[IMU] WHO_AM_I = 0x%02X (expected 0x6C) — check wiring!\n", who);
  } else {
    Serial.println("[IMU] LSM6DSO32 found.");
  }

  // Reset device
  lsm_write(LSM_CTRL3_C, 0x01);
  delay(20);

  // Accel: 26 Hz, ±4g (good for steps)
  lsm_write(LSM_CTRL1_XL, 0x22);  // ODR=26Hz, FS=±4g

  // Gyro off (saves power; re-enable if you need orientation)
  lsm_write(LSM_CTRL2_G, 0x00);

  // Enable embedded functions
  lsm_write(LSM_CTRL10_C, 0x04);

  // Enable pedometer in embedded function bank
  lsm_write_emb(LSM_EMB_FUNC_EN_A_BANK, 0x08);  // bit3 = pedo_en

  // Route step-detector interrupt to INT1
  lsm_write_emb(LSM_INT1_CTRL_EMB, 0x08);        // bit3 = step_det_en

  // Enable interrupts on TAP_CFG2
  lsm_write(LSM_TAP_CFG2, 0x80);                 // INTERRUPTS_ENABLE

  // Attach interrupt pin
  if (_imu_int1_pin >= 0) {
    pinMode(_imu_int1_pin, INPUT);
    attachInterrupt(digitalPinToInterrupt(_imu_int1_pin), imu_step_isr, RISING);
  }

  Serial.println("[IMU] Pedometer configured.");
}

// ─────────────────────────────────────────────────────────────
uint32_t imu_get_steps() {
  // Read 16-bit step counter from LSM6DSO32
  Wire.beginTransmission(LSM_ADDR);
  Wire.write(LSM_STEP_COUNTER_L);
  Wire.endTransmission(false);
  Wire.requestFrom(LSM_ADDR, 2);
  if (Wire.available() >= 2) {
    uint8_t lo = Wire.read();
    uint8_t hi = Wire.read();
    return (uint32_t)((hi << 8) | lo);
  }
  return 0;
}

bool imu_step_detected() {
  if (_step_flag) {
    _step_flag = false;
    return true;
  }
  return false;
}