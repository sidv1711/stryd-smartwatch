# Stryd HAR Training Pipeline

Train a tiny 1-D CNN to classify wrist-IMU activities, quantize to int8, and
emit a C header you can drop into the firmware for TensorFlow Lite Micro.

## Quickstart

```bash
cd tools/ml
python3 -m venv venv && source venv/bin/activate
pip install -r requirements.txt

# Downloads the WISDM watch dataset (~3 GB) and builds the windowed
# training set. Saves data/windows.npz.
python prepare_data.py

# Trains, evaluates, quantizes, and exports a C header.
# Outputs:
#   artifacts/model.keras    (full Keras model)
#   artifacts/model.tflite   (int8 quantized)
#   artifacts/model_data.h   (C array for TFLM, plus per-channel norm stats)
python train.py
```

Total wall time on a laptop CPU: ~10 min to prepare data, ~5 min to train.

## Pipeline

1. **Download** the [WISDM Smartphone and Smartwatch Activity dataset](https://archive.ics.uci.edu/dataset/507/) (UCI, 2019).
2. **Filter** to wrist-watch accelerometer + gyroscope, five fitness-relevant
   activities: walking, jogging, stairs, sitting, standing.
3. **Resample** both streams onto a common 50 Hz grid via linear interpolation.
4. **Window** into 128-sample (~2.56 s) windows with 50 % overlap.
5. **Normalize** per-channel using train-set mean / std (saved for inference).
6. **Train** a small 1-D CNN — Conv16 → Pool → Conv32 → Pool → Conv64 → GAP →
   Dense. ~10 k parameters.
7. **Quantize** post-training to int8 using a representative subset of the
   training data.
8. **Export** the `.tflite` flatbuffer as a `const unsigned char[]` in a C
   header, alongside the per-channel mean/std the firmware needs to normalize
   live samples.

## Output sizes

| Artifact | Size |
|---|---|
| `model.tflite` (int8) | ~12 KB |
| TFLM activation arena (this network) | ~80 KB SRAM |
| Inference latency on ESP32-S3 LX7 @ 240 MHz | ~25 ms (est.) |

Well inside the ESP32-S3's 512 KB SRAM budget.

## Deploying to the firmware (next session)

1. Copy `artifacts/model_data.h` into `smartwatch/`.
2. Add a new `smartwatch/activity_classifier.h` that:
   - Sets up the TFLM interpreter once at boot
   - Buffers 128 samples × 6 axes from the existing IMU read path
   - Runs inference every ~2.5 s
   - Writes the predicted activity into `WatchState`
3. Add a new screen to `watchface_impl.h` that renders the activity name.
4. Expose `activity` as a BLE notify characteristic.

## Notes

- **Public-dataset training is a baseline.** The classifier will work best
  after fine-tuning on your own wrist data. That step needs BLE IMU
  streaming firmware-side, which we'll wire up next.
- **20 Hz → 50 Hz resampling.** WISDM watch data is recorded at 20 Hz; we
  upsample to roughly match the firmware's 52 Hz IMU cadence. Tiny artifact,
  fine for v1.
- **Why GlobalAveragePooling.** Keeps the dense head tiny (64 → num_classes
  instead of flatten → num_classes), which keeps the int8 model well under
  20 KB.
- **Why only 5 activities.** WISDM has 18, but most of the rest are hand
  gestures (eating, brushing teeth, clapping) that we don't care about for
  fitness. Easy to widen later — just add codes to `ACTIVITY_MAP` in
  `prepare_data.py`.
