"""
prepare_data.py — download WISDM, window it, save data/windows.npz.

Output schema (numpy .npz):
    X       float32  (N, 128, 6)   accel xyz + gyro xyz, normalized
    y       <U16     (N,)          activity name string
    classes <U16     (C,)          ordered class list
    mean    float64  (6,)          per-channel mean (pre-normalize)
    std     float64  (6,)          per-channel std  (pre-normalize)
"""

import os
import sys
import zipfile
import urllib.request

import numpy as np
import pandas as pd

# ── Config ────────────────────────────────────────────────────────────
DATASET_URL = (
    "https://archive.ics.uci.edu/static/public/507/"
    "wisdm+smartphone+and+smartwatch+activity+and+biometrics+dataset.zip"
)
DATA_DIR  = "data"
ZIP_PATH  = os.path.join(DATA_DIR, "wisdm.zip")
EXTRACTED = os.path.join(DATA_DIR, "wisdm-dataset")

# Single-letter activity codes used by WISDM, mapped to our class names.
# Edit this to widen / narrow the classifier; CLASSES below must match.
ACTIVITY_MAP = {
    "A": "walking",
    "B": "jogging",
    "C": "stairs",
    "D": "sitting",
    "E": "standing",
}
CLASSES = ["walking", "jogging", "stairs", "sitting", "standing"]

TARGET_HZ       = 50    # resample target — close to firmware's 52 Hz IMU rate
WINDOW_SAMPLES  = 128   # ~2.56 s @ 50 Hz
WINDOW_STRIDE   = 64    # 50 % overlap


# ── Download / extract ────────────────────────────────────────────────
def download():
    os.makedirs(DATA_DIR, exist_ok=True)
    if os.path.exists(ZIP_PATH):
        print(f"[data] {ZIP_PATH} already present, skipping download")
        return
    print(f"[data] downloading WISDM (~3 GB) -> {ZIP_PATH}")
    print("[data] this is a one-time hit; subsequent runs use the cache")
    urllib.request.urlretrieve(DATASET_URL, ZIP_PATH, reporthook=_progress)
    print()  # newline after progress


def extract():
    if os.path.isdir(os.path.join(EXTRACTED, "raw", "watch")):
        print(f"[data] {EXTRACTED} already extracted, skipping")
        return
    print("[data] extracting outer archive...")
    with zipfile.ZipFile(ZIP_PATH) as zf:
        zf.extractall(DATA_DIR)
    # UCI ships this as a zip-of-zip: outer wrapper contains the dataset zip
    # plus a description PDF. Extract the inner one too.
    inner = os.path.join(DATA_DIR, "wisdm-dataset.zip")
    if os.path.exists(inner):
        print("[data] extracting inner dataset zip...")
        with zipfile.ZipFile(inner) as zf:
            zf.extractall(DATA_DIR)


def _progress(count, block, total):
    if total <= 0:
        return
    pct = min(100, 100 * count * block / total)
    sys.stdout.write(f"\r[data]   {pct:5.1f}%")
    sys.stdout.flush()


# ── Parsing ───────────────────────────────────────────────────────────
def read_sensor_file(path):
    """WISDM lines look like: '1600,A,12345678,0.1,-0.5,9.8;\\n'."""
    df = pd.read_csv(
        path, header=None,
        names=["subj", "act", "t", "x", "y", "z"],
        converters={"z": lambda s: float(s.rstrip(";"))},
    )
    return df


def process_subject(subj_id, watch_dir):
    accel_path = os.path.join(watch_dir, "accel", f"data_{subj_id}_accel_watch.txt")
    gyro_path  = os.path.join(watch_dir, "gyro",  f"data_{subj_id}_gyro_watch.txt")
    if not (os.path.exists(accel_path) and os.path.exists(gyro_path)):
        return [], []

    a = read_sensor_file(accel_path)
    g = read_sensor_file(gyro_path)

    keep_codes = set(ACTIVITY_MAP.keys())
    a = a[a["act"].isin(keep_codes)]
    g = g[g["act"].isin(keep_codes)]

    windows, labels = [], []
    for code in keep_codes:
        a_act = a[a["act"] == code].sort_values("t")
        g_act = g[g["act"] == code].sort_values("t")
        if len(a_act) < 100 or len(g_act) < 100:
            continue

        # WISDM timestamps are nanoseconds. Build a common uniform grid.
        t0 = max(a_act["t"].iloc[0], g_act["t"].iloc[0])
        t1 = min(a_act["t"].iloc[-1], g_act["t"].iloc[-1])
        if t1 <= t0:
            continue
        n = int((t1 - t0) * TARGET_HZ / 1e9)
        if n < WINDOW_SAMPLES:
            continue

        t_grid = np.linspace(t0, t1, n)
        a_arr = np.column_stack([
            np.interp(t_grid, a_act["t"].values, a_act[c].values)
            for c in ("x", "y", "z")
        ])
        g_arr = np.column_stack([
            np.interp(t_grid, g_act["t"].values, g_act[c].values)
            for c in ("x", "y", "z")
        ])
        combined = np.concatenate([a_arr, g_arr], axis=1).astype(np.float32)

        for start in range(0, len(combined) - WINDOW_SAMPLES + 1, WINDOW_STRIDE):
            windows.append(combined[start:start + WINDOW_SAMPLES])
            labels.append(ACTIVITY_MAP[code])

    return windows, labels


# ── Main ──────────────────────────────────────────────────────────────
def main():
    download()
    extract()

    watch_dir = os.path.join(EXTRACTED, "raw", "watch")
    if not os.path.isdir(watch_dir):
        sys.exit(f"[err] expected {watch_dir} after extract — dataset layout changed?")

    accel_files = os.listdir(os.path.join(watch_dir, "accel"))
    subj_ids = sorted({
        f.split("_")[1] for f in accel_files
        if f.startswith("data_") and f.endswith("_accel_watch.txt")
    })
    print(f"[data] processing {len(subj_ids)} subjects...")

    all_X, all_y = [], []
    for i, sid in enumerate(subj_ids, 1):
        windows, labels = process_subject(sid, watch_dir)
        all_X.extend(windows)
        all_y.extend(labels)
        if i % 10 == 0 or i == len(subj_ids):
            print(f"[data]   {i}/{len(subj_ids)} done, {len(all_X)} windows so far")

    X = np.stack(all_X).astype(np.float32)
    y = np.array(all_y)

    # Per-channel z-score normalization. Stats live in the .npz so the firmware
    # can normalize live samples the same way.
    mean = X.mean(axis=(0, 1))
    std  = X.std(axis=(0, 1)) + 1e-8
    X = ((X - mean) / std).astype(np.float32)

    out_path = os.path.join(DATA_DIR, "windows.npz")
    np.savez_compressed(out_path, X=X, y=y,
                        classes=np.array(CLASSES), mean=mean, std=std)
    print(f"[data] saved {out_path}: X={X.shape}, y={y.shape}")
    for cls in CLASSES:
        print(f"[data]   {cls:10s}: {(y == cls).sum()}")


if __name__ == "__main__":
    main()
