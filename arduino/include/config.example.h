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
#define AP_DEFAULT_NETWORK IPAddress(192, 168, 137, 0)
#define AP_HOST_OCTET      1
#define AP_LEASE_OCTET     10
#define AP_MASK            IPAddress(255, 255, 255, 0)

// ---- Behaviour --------------------------------------------------------------
// After boot, if any SSIDs are saved, wait this long (still scanning) before
// connecting to the strongest saved one. Gives the user a window to change
// the selection first.
#define BOOT_GRACE_MS          60000UL
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

// ---- Storage ----------------------------------------------------------------
#define MAX_SAVED_NETWORKS 16
#define MAX_OVERRIDES      16
#define NVS_NAMESPACE      "ssidmap"

// ---- Upstream DNS used when the STA link gives us none ------------------------
#define FALLBACK_DNS     IPAddress(1, 1, 1, 1)
