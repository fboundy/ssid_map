#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "esp_wifi_types.h"

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
  std::string ssid;
  int rssi;
  int channel;
  wifi_auth_mode_t auth;
};

// Point-in-time copy of everything the web UI shows. Built under the app lock.
struct StatusSnapshot {
  Mode mode;
  uint32_t countdownMs;
  std::string target;
  std::string error;
  std::string visibleOverride;
  bool nat;
  bool scanning;
  int64_t scanAgeMs;  // -1 if no scan has completed yet
  std::vector<ScanEntry> scan;
  bool staConnected;
  std::string staSsid, staIp, staGw, staDns;
  int staRssi;
  int apClients;
};

// The app lock guards the state machine, scan results and the NetworkStore.
// HTTP handlers must hold it while reading state or mutating the store.
void appLock();
void appUnlock();

namespace app {
StatusSnapshot snapshot();  // takes the lock itself

// Commands from the UI. Each takes the lock itself and is applied by the
// state-machine task on its next pass.
void requestScan();
bool connectNow(const std::string& ssid, std::string& err);  // "" = strongest saved
void disconnect();
void onStoreChanged();  // caller holds the lock
}  // namespace app
