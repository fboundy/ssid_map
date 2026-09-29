#pragma once

// ---- Access point the device always advertises ------------------------------
#define AP_SSID          "Penrith MRT Xebro"
// WPA2 passphrase for the AP. Leave empty ("") for an open network.
#define AP_PASS          "change-me"
#define AP_CHANNEL       1
#define AP_MAX_CLIENTS   8

// Default AP subnet (/24). Can be changed from the web UI and is stored in
// NVS; the device takes .1, DHCP leases start at .10 and clients use the
// ESP32 as gateway + DNS.
#define AP_DEFAULT_NETWORK "192.168.137.0"
#define AP_HOST_OCTET      1
#define AP_LEASE_OCTET     10
#define AP_LEASE_COUNT     20

// ---- Behaviour --------------------------------------------------------------
// How often to rescan while not connected. Every scan takes the radio off the
// AP channel (~1.5 s), during which the AP stops beaconing, so keep this low.
#define IDLE_SCAN_INTERVAL_MS  30000UL
// Delay before trying the next candidate after a failed or dropped connection.
#define RECONNECT_DELAY_MS     5000UL
// Give up on a single connection attempt after this long.
#define CONNECT_TIMEOUT_MS     20000UL
// While routing, rescan this often to check whether an over-ride SSID has
// appeared (only when at least one over-ride is enabled). Each scan pauses
// routed traffic for roughly a second.
#define OVERRIDE_CHECK_INTERVAL_MS 30000UL
// Active-scan dwell per channel (ms). Short keeps AP beacon gaps small.
#define SCAN_DWELL_MS          120

// ---- Storage ----------------------------------------------------------------
#define MAX_SAVED_NETWORKS 16
#define MAX_OVERRIDES      16
#define NVS_NAMESPACE      "ssidmap"

// ---- Upstream DNS used when the STA link gives us none ------------------------
#define FALLBACK_DNS     "1.1.1.1"

// ---- Display orientation (Waveshare build only) ------------------------------
// 0 = USB-C on the right as you look at the screen, 1 = rotated 180 so the
// USB-C is on the left. The panel's 170 px axis sits centrally in the ST7789's
// 240 columns (35 px each side), so the gap is the same either way.
#define LCD_ROTATE_180 1

// ---- Radio regulatory domain ------------------------------------------------
// UK/EU: channels 1-13. (The IDF default "01" world domain only uses 1-11.)
#define WIFI_COUNTRY       "GB"
#define WIFI_COUNTRY_NCHAN 13

// ---- Periodic Ookla speed test (Waveshare build only: -DXEBRO_SPEEDTEST) --------
// Runs SPEEDTEST_DELAY_MS after the uplink comes up, then every
// SPEEDTEST_INTERVAL_MS while routing. Each direction streams for
// SPEEDTEST_SECONDS, so an hourly test costs roughly 2 x SECONDS x link rate
// of upstream traffic (~35 MB/h at 20 Mbit/s).
#define SPEEDTEST_DELAY_MS     5000UL
#define SPEEDTEST_INTERVAL_MS  3600000UL
#define SPEEDTEST_SECONDS      6
// Leave empty to pick the nearest server from speedtest.net; or force one,
// e.g. "speedtest.example.net:8080".
#define SPEEDTEST_SERVER       ""
