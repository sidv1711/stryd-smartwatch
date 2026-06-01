"""
weather_push.py — Push current weather to the SmartWatch over BLE.

Fetches current weather from Open-Meteo (no API key required) and writes
it to the watch's BLE Weather Service so the watchface stays up to date
without manual nRF Connect pokes.

Run on a laptop that's within BLE range of the watch.

Usage:
    pip install bleak
    python weather_push.py                # one-shot push
    python weather_push.py --loop         # push every WX_INTERVAL_S seconds
    python weather_push.py --lat 37.77 --lon -122.42

Protocol (must match smartwatch/ble_service.h):
    Service UUID    87654321-0000-1000-8000-00805f9b34fb
      Weather Code  87654321-0001-...    1 byte WMO code
      Outdoor Temp  87654321-0002-...    int16 little-endian, °C × 100
"""

import argparse
import asyncio
import json
import struct
import sys
import urllib.parse
import urllib.request

from bleak import BleakClient, BleakScanner

DEVICE_NAME   = "SmartWatch"
SVC_UUID      = "87654321-0000-1000-8000-00805f9b34fb"
WX_CODE_UUID  = "87654321-0001-1000-8000-00805f9b34fb"
WX_TEMP_UUID  = "87654321-0002-1000-8000-00805f9b34fb"

WX_INTERVAL_S    = 15 * 60       # 15 min between pushes in --loop mode
SCAN_TIMEOUT_S   = 10
CONNECT_RETRIES  = 3


def fetch_weather(lat: float, lon: float) -> tuple[int, float]:
    """Return (wmo_code, temperature_celsius) for the given coordinates."""
    qs = urllib.parse.urlencode({
        "latitude":         lat,
        "longitude":        lon,
        "current_weather":  "true",
    })
    url = f"https://api.open-meteo.com/v1/forecast?{qs}"
    with urllib.request.urlopen(url, timeout=10) as r:
        body = json.load(r)
    cw = body["current_weather"]
    return int(cw["weathercode"]), float(cw["temperature"])


async def push_once(lat: float, lon: float) -> None:
    code, temp = fetch_weather(lat, lon)
    print(f"[wx] open-meteo: code={code} temp={temp:.2f}°C")

    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=SCAN_TIMEOUT_S)
    if device is None:
        raise RuntimeError(f"BLE device named {DEVICE_NAME!r} not found in {SCAN_TIMEOUT_S}s")

    # Temperature is sent as int16 little-endian, value × 100.
    temp_int = max(-32768, min(32767, int(round(temp * 100))))
    temp_bytes = struct.pack("<h", temp_int)

    last_err: Exception | None = None
    for attempt in range(1, CONNECT_RETRIES + 1):
        try:
            async with BleakClient(device) as client:
                await client.write_gatt_char(WX_CODE_UUID, bytes([code & 0xFF]), response=True)
                await client.write_gatt_char(WX_TEMP_UUID, temp_bytes,           response=True)
                print(f"[wx] pushed: code=0x{code:02X}  temp_bytes={temp_bytes.hex(' ')}")
                return
        except Exception as e:
            last_err = e
            print(f"[wx] attempt {attempt}/{CONNECT_RETRIES} failed: {e}")
            await asyncio.sleep(2)
    raise RuntimeError(f"all {CONNECT_RETRIES} BLE attempts failed: {last_err}")


async def main() -> int:
    p = argparse.ArgumentParser(description="Push weather to SmartWatch over BLE.")
    p.add_argument("--lat",  type=float, default=37.7749, help="latitude (default: SF)")
    p.add_argument("--lon",  type=float, default=-122.4194, help="longitude (default: SF)")
    p.add_argument("--loop", action="store_true", help=f"push every {WX_INTERVAL_S}s")
    args = p.parse_args()

    while True:
        try:
            await push_once(args.lat, args.lon)
        except Exception as e:
            print(f"[wx] error: {e}", file=sys.stderr)
        if not args.loop:
            return 0
        await asyncio.sleep(WX_INTERVAL_S)


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
