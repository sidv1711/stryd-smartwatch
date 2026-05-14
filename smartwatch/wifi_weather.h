/*
 * wifi_weather.h - WiFi-driven Open-Meteo weather streaming
 *
 * Connects to WiFi (non-blocking), polls api.open-meteo.com every
 * WEATHER_REFRESH_MS, parses current temperature_2m + weather_code, and
 * writes them into WatchState. Coexists with BLE on the ESP32 radio.
 */

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "watchface.h"
#include "secrets.h"

#define WEATHER_REFRESH_MS   (10UL * 60UL * 1000UL)  // 10 minutes
#define WIFI_RETRY_MS        (30UL * 1000UL)         // retry connect every 30s
#define WIFI_CONNECT_TIMEOUT_MS  8000UL

static unsigned long _wx_last_fetch_ms = 0;
static unsigned long _wx_last_wifi_try_ms = 0;
static bool          _wx_wifi_started = false;

static void wifi_weather_init() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);

    // Debug: list every nearby SSID so we can see exactly what the hotspot is
    // broadcasting (catches curly apostrophes, trailing spaces, etc).
    Serial.println("[WIFI] scanning visible networks...");
    int n = WiFi.scanNetworks();
    Serial.printf("[WIFI] found %d networks:\n", n);
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        Serial.printf("  [%d] \"%s\" (rssi=%d, ch=%d) bytes:",
                      i, ssid.c_str(), WiFi.RSSI(i), WiFi.channel(i));
        for (size_t j = 0; j < ssid.length(); j++) {
            Serial.printf(" %02X", (uint8_t)ssid[j]);
        }
        Serial.println();
    }
    WiFi.scanDelete();

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    _wx_wifi_started = true;
    _wx_last_wifi_try_ms = millis();
    Serial.printf("[WIFI] connecting to %s...\n", WIFI_SSID);
}

static const char* _wifi_status_str(wl_status_t s) {
    switch (s) {
        case WL_IDLE_STATUS:     return "IDLE";
        case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL (network not found)";
        case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
        case WL_CONNECTED:       return "CONNECTED";
        case WL_CONNECT_FAILED:  return "CONNECT_FAILED (wrong password?)";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED:    return "DISCONNECTED";
        default:                 return "UNKNOWN";
    }
}

// Non-blocking: kicks a reconnect attempt if we've been disconnected for a while.
static void _wifi_supervise() {
    static wl_status_t last_status = WL_IDLE_STATUS;
    wl_status_t s = WiFi.status();
    if (s != last_status) {
        Serial.printf("[WIFI] state: %s\n", _wifi_status_str(s));
        if (s == WL_CONNECTED) {
            Serial.print("[WIFI] IP: ");
            Serial.println(WiFi.localIP());
            Serial.printf("[WIFI] RSSI: %d dBm\n", WiFi.RSSI());
        }
        last_status = s;
    }
    if (s == WL_CONNECTED) return;

    unsigned long now = millis();
    if (now - _wx_last_wifi_try_ms < WIFI_RETRY_MS) return;
    _wx_last_wifi_try_ms = now;
    Serial.println("[WIFI] retrying...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

// Tiny extractor: finds `"key":` in the JSON body and parses the number after it.
// Avoids pulling in ArduinoJson for two scalar fields.
static bool _json_find_number(const String& body, const char* key, float& out) {
    int k = body.indexOf(key);
    if (k < 0) return false;
    int colon = body.indexOf(':', k);
    if (colon < 0) return false;
    int i = colon + 1;
    while (i < (int)body.length() && (body[i] == ' ' || body[i] == '"')) i++;
    int start = i;
    while (i < (int)body.length() &&
           (isdigit(body[i]) || body[i] == '.' || body[i] == '-' || body[i] == '+')) i++;
    if (i == start) return false;
    out = body.substring(start, i).toFloat();
    return true;
}

static bool _fetch_weather(WatchState& ws) {
    WiFiClientSecure client;
    client.setInsecure();  // skip cert chain — fine for public weather data
    HTTPClient http;

    char url[192];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code",
        WEATHER_LAT, WEATHER_LON);

    if (!http.begin(client, url)) {
        Serial.println("[WX] http.begin failed");
        return false;
    }
    http.setTimeout(6000);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[WX] HTTP %d\n", code);
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();

    float temp = 0, wcode = 0;
    if (!_json_find_number(body, "\"temperature_2m\"", temp) ||
        !_json_find_number(body, "\"weather_code\"", wcode)) {
        Serial.println("[WX] parse failed");
        return false;
    }
    ws.weatherTemp = temp;
    ws.weatherCode = (uint8_t)wcode;
    Serial.printf("[WX] %.1f C  code=%u\n", temp, (uint8_t)wcode);
    return true;
}

// Call from loop(). Non-blocking when WiFi is down or interval not elapsed.
static void wifi_weather_poll(WatchState& ws) {
    if (!_wx_wifi_started) return;
    _wifi_supervise();
    if (WiFi.status() != WL_CONNECTED) return;

    unsigned long now = millis();
    bool first = (_wx_last_fetch_ms == 0);
    if (!first && (now - _wx_last_fetch_ms < WEATHER_REFRESH_MS)) return;

    _wx_last_fetch_ms = now;
    _fetch_weather(ws);
}
