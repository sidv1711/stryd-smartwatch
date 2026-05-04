/*
 * imu.h / imu.cpp  –  LSM6DSO32 driver
 *
 * Uses I2C (SDO/SA0 tied to GND → address 0x6A)
 * Configures accel + gyro at 52 Hz for HAR data collection,
 * and keeps the embedded pedometer running in parallel.
 *
 * INT1 → step-detected interrupt (GPIO 7)
 * INT2 → wrist-tilt / free-fall (GPIO 4)  — unused for now
 *
 * Pedometer is tuned by ST for ODR ≥ 26 Hz; running at 52 Hz works
 * (the embedded block downsamples internally) but step counts may
 * be a few percent off vs. dedicated 26 Hz operation. Acceptable.
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
#define LSM_TAP_CFG2      0x58   // interrupt enable
#define LSM_OUTX_L_G      0x22   // start of 12-byte gyro+accel block
#define LSM_STEP_COUNTER_L 0x62  // 16-bit step count (little-endian)
#define LSM_STEP_COUNTER_H 0x63
#define LSM_FUNC_CFG_ACCESS 0x01 // switch to embedded func bank

// Embedded function register bank
#define LSM_EMB_FUNC_EN_A_BANK  0x04  // bit3 = PEDO_EN
#define LSM_INT1_CTRL_EMB       0x0A  // route step to INT1

// ── Sample format ────────────────────────────────────────────
// Raw int16 counts. At ±2g accel: 1 g ≈ 16384 counts.
//                  At ±250 dps gyro: 1 dps ≈ 114.3 counts.
typedef struct {
  int16_t gx, gy, gz;
  int16_t ax, ay, az;
} imu_sample_t;

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

  // Software reset (SW_RESET=1). The reset clears every CTRL register
  // back to defaults — BDU and IF_INC included — so we re-program
  // CTRL3_C explicitly *after* the reset settles.
  lsm_write(LSM_CTRL3_C, 0x01);
  delay(20);

  // CTRL3_C: BDU=1 (block data update — prevents torn multi-byte reads),
  //          IF_INC=1 (auto-increment for burst reads).
  lsm_write(LSM_CTRL3_C, 0x44);

  // Accel: 52 Hz, ±2g (the populated part is LSM6DSO, not DSO32 —
  // they share WHO_AM_I=0x6C but FS_XL=00 maps to ±2g on DSO).
  // CTRL1_XL: ODR_XL=0011 (52Hz), FS_XL=00 (±2g on DSO)
  lsm_write(LSM_CTRL1_XL, 0x30);

  // Gyro: 52 Hz, ±250 dps — wrist rotation rarely exceeds 250 dps.
  // CTRL2_G: ODR_G=0011 (52Hz), FS_G=00 (±250 dps)
  lsm_write(LSM_CTRL2_G, 0x30);

  // Enable pedometer in embedded function bank (runs in parallel
  // with raw reads — the chip handles the algorithm internally)
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
  // STEP_COUNTER_L/H live in the embedded function bank on
  // LSM6DSO32 — switch banks before reading, then switch back.
  lsm_write(LSM_FUNC_CFG_ACCESS, 0x80);

  Wire.beginTransmission(LSM_ADDR);
  Wire.write(LSM_STEP_COUNTER_L);
  Wire.endTransmission(false);
  Wire.requestFrom(LSM_ADDR, 2);
  uint32_t steps = 0;
  if (Wire.available() >= 2) {
    uint8_t lo = Wire.read();
    uint8_t hi = Wire.read();
    steps = (uint32_t)((hi << 8) | lo);
  }

  lsm_write(LSM_FUNC_CFG_ACCESS, 0x00);
  return steps;
}

bool imu_step_detected() {
  if (_step_flag) {
    _step_flag = false;
    return true;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────
// Read one 6-axis sample (gyro + accel as raw int16 counts).
// 12 bytes burst-read starting at OUTX_L_G; auto-increment is
// already on (CTRL3_C IF_INC=1 set during init).
bool imu_read_sample(imu_sample_t* out) {
  Wire.beginTransmission(LSM_ADDR);
  Wire.write(LSM_OUTX_L_G);
  Wire.endTransmission(false);
  Wire.requestFrom(LSM_ADDR, 12);
  if (Wire.available() < 12) return false;

  uint8_t b[12];
  for (int i = 0; i < 12; i++) b[i] = Wire.read();

  out->gx = (int16_t)((b[1]  << 8) | b[0]);
  out->gy = (int16_t)((b[3]  << 8) | b[2]);
  out->gz = (int16_t)((b[5]  << 8) | b[4]);
  out->ax = (int16_t)((b[7]  << 8) | b[6]);
  out->ay = (int16_t)((b[9]  << 8) | b[8]);
  out->az = (int16_t)((b[11] << 8) | b[10]);
  return true;
}