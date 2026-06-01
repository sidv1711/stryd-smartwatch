/*
 * activity_classifier.h - TFLite Micro wrapper for on-device HAR.
 *
 * Buffers 6-axis IMU samples in a ring at the firmware's native 50 Hz
 * cadence and runs inference once per full 128-sample window (~2.56 s)
 * using the int8 model in model_data.h.
 *
 * Required Arduino library: TensorFlowLite_ESP32 (tanakamasayuki)
 *   Library Manager -> search "TensorFlowLite_ESP32" -> install.
 *   API-compatible alternatives: Chirale_TensorFlowLite.
 *
 * RAM footprint:
 *   tensor_arena            ~100 KB  (set by kArenaSize)
 *   ring buffer (128*6*4)   ~3   KB
 *
 * Both static; fits easily in ESP32-S3 SRAM (512 KB).
 */

#pragma once
#include <Arduino.h>
#include <math.h>

// TensorFlow Lite Micro
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "model_data.h"   // kModelData, kModelLen, kClassLabels, kInputMean/Std

// ── Sample-format conversion ─────────────────────────────────────────
// WISDM training data is in m/s^2 (accel) and rad/s (gyro). The LSM6DSO
// returns raw int16 counts; bring them into the same unit space before
// the network sees them so kInputMean/kInputStd line up.
constexpr float ACCEL_LSB_TO_G    = 1.0f / 16384.0f;     // ±2g
constexpr float G_TO_MS2          = 9.80665f;
constexpr float GYRO_LSB_TO_DPS   = 1.0f / 114.286f;     // ±250 dps
constexpr float DPS_TO_RADS       = 3.14159265358979f / 180.0f;

constexpr float ACCEL_RAW_TO_MS2  = ACCEL_LSB_TO_G  * G_TO_MS2;
constexpr float GYRO_RAW_TO_RADS  = GYRO_LSB_TO_DPS * DPS_TO_RADS;

// ── Internals ────────────────────────────────────────────────────────
namespace {
    constexpr int   kWindowSamples = 128;
    constexpr int   kChannels      = 6;
    constexpr int   kArenaSize     = 100 * 1024;

    alignas(16) uint8_t tensor_arena[kArenaSize];

    float    ring[kWindowSamples][kChannels];
    int      ring_head    = 0;
    uint32_t samples_seen = 0;

    tflite::MicroInterpreter* g_interp = nullptr;
    TfLiteTensor*             g_input  = nullptr;
    TfLiteTensor*             g_output = nullptr;
}

// ── Public API ───────────────────────────────────────────────────────

// Call once after imu_init(). Returns true on success.
static bool activity_classifier_init() {
    const tflite::Model* model = tflite::GetModel(kModelData);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        Serial.printf("[ACT] schema mismatch: model %lu vs runtime %u\n",
                      (unsigned long)model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    static tflite::MicroMutableOpResolver<7> resolver;
    resolver.AddConv2D();
    resolver.AddMaxPool2D();
    resolver.AddExpandDims();
    resolver.AddReshape();
    resolver.AddMean();
    resolver.AddFullyConnected();
    resolver.AddSoftmax();

    static tflite::MicroInterpreter static_interp(
        model, resolver, tensor_arena, kArenaSize);
    g_interp = &static_interp;

    if (g_interp->AllocateTensors() != kTfLiteOk) {
        Serial.println("[ACT] AllocateTensors failed");
        return false;
    }
    g_input  = g_interp->input(0);
    g_output = g_interp->output(0);

    Serial.printf("[ACT] ready. arena used: %u / %u bytes\n",
                  (unsigned)g_interp->arena_used_bytes(), kArenaSize);
    return true;
}

// Push one raw IMU sample. Call at ~50 Hz from loop().
// Returns true and fills out_class_idx / out_confidence when a fresh
// prediction lands (every kWindowSamples calls).
static bool activity_classifier_push(int16_t ax, int16_t ay, int16_t az,
                                     int16_t gx, int16_t gy, int16_t gz,
                                     int& out_class_idx, float& out_confidence)
{
    if (g_interp == nullptr) return false;

    // Convert to m/s^2 / rad/s, then z-score normalize using training stats.
    const float raw[6] = {
        ax * ACCEL_RAW_TO_MS2,
        ay * ACCEL_RAW_TO_MS2,
        az * ACCEL_RAW_TO_MS2,
        gx * GYRO_RAW_TO_RADS,
        gy * GYRO_RAW_TO_RADS,
        gz * GYRO_RAW_TO_RADS,
    };
    for (int c = 0; c < kChannels; c++) {
        ring[ring_head][c] = (raw[c] - kInputMean[c]) / kInputStd[c];
    }
    ring_head = (ring_head + 1) % kWindowSamples;
    samples_seen++;

    // Only run inference once per full window.
    if (samples_seen < (uint32_t)kWindowSamples) return false;
    if (samples_seen % kWindowSamples != 0)      return false;

    // Quantize float window -> int8 input tensor in time order.
    const float scale = g_input->params.scale;
    const int   zero  = g_input->params.zero_point;
    int8_t* in = g_input->data.int8;
    for (int i = 0; i < kWindowSamples; i++) {
        const int src = (ring_head + i) % kWindowSamples;  // oldest first
        for (int c = 0; c < kChannels; c++) {
            int q = (int)lroundf(ring[src][c] / scale) + zero;
            if (q < -128) q = -128;
            if (q >  127) q =  127;
            in[i * kChannels + c] = (int8_t)q;
        }
    }

    if (g_interp->Invoke() != kTfLiteOk) {
        Serial.println("[ACT] Invoke failed");
        return false;
    }

    // Argmax over int8 softmax output; dequantize the winner for confidence.
    const int8_t* probs = g_output->data.int8;
    int best = 0;
    for (int i = 1; i < kNumClasses; i++) {
        if (probs[i] > probs[best]) best = i;
    }
    out_class_idx  = best;
    out_confidence = (probs[best] - g_output->params.zero_point) * g_output->params.scale;
    return true;
}
