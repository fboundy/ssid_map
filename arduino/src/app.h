#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <vector>

// Top-level behaviour of the device.
enum class Mode {
  WAITING,     // no SSIDs saved: sit in AP mode until the user selects some
  COUNTDOWN,   // SSIDs saved: boot grace period before auto-connect
  CONNECTING,  // STA join in progress
  ROUTING,     // STA up, NAT enabled, AP clients have internet
  RETRY,       // STA dropped or failed; rescan and pick the best again
  HOLD,        // user pressed Disconnect; stay AP-only until told otherwise
  OVERRIDE,    // an enabled over-ride SSID is visible: NAT off until it goes
};
const char* modeName(Mode m);

struct ScanEntry {
  String ssid;
  int rssi;
  int channel;
  wifi_auth_mode_t auth;
};

namespace app {
Mode mode();
uint32_t countdownRemainingMs();
const std::vector<ScanEntry>& scanResults();
uint32_t scanAgeMs();  // ms since the last completed scan, UINT32_MAX if none
bool scanInProgress();
String targetSsid();   // SSID we are joining / joined
String lastError();
String visibleOverride();  // enabled over-ride SSID seen in last scan, or ""

void requestScan();
void connectNow(const String& ssid);  // "" = strongest saved network in view
void disconnect();
void onStoreChanged();
}  // namespace app
