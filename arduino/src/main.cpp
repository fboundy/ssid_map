// Penrith MRT Xebro - Wi-Fi SSID selector and NAT router for ESP32-S3.
//
// Boot:  AP "Penrith MRT Xebro" comes up (default 192.168.137.1) with a web UI.
//        If SSIDs are saved in NVS, wait BOOT_GRACE_MS (scanning), then join
//        the strongest visible saved network and NAT AP clients through it.
//        If nothing is saved, wait for the user to select networks.

#include <Arduino.h>
#include <WiFi.h>
#include <algorithm>
#include "config.h"
#include "app.h"
#include "store.h"
#include "dns_proxy.h"
#include "web_ui.h"

// ---- State ------------------------------------------------------------------
static Mode s_mode = Mode::WAITING;
static uint32_t s_modeSince = 0;
static uint32_t s_countdownEnd = 0;
static uint32_t s_retryAt = 0;
static uint32_t s_connectStarted = 0;
static String s_target;
static String s_lastError;

static std::vector<ScanEntry> s_scan;
static uint32_t s_scanCompletedAt = 0;
static bool s_scanEver = false;
static bool s_scanRunning = false;
static bool s_scanWanted = false;
static uint32_t s_lastScanKick = 0;

// Flags set from the Wi-Fi event task, consumed in loop().
static volatile bool ev_gotIp = false;
static volatile bool ev_lost = false;

const char* modeName(Mode m) {
  switch (m) {
    case Mode::WAITING:    return "waiting";
    case Mode::COUNTDOWN:  return "countdown";
    case Mode::CONNECTING: return "connecting";
    case Mode::ROUTING:    return "routing";
    case Mode::RETRY:      return "retry";
    case Mode::HOLD:       return "hold";
    case Mode::OVERRIDE:   return "override";
  }
  return "?";
}

static void setMode(Mode m) {
  if (m == s_mode) return;
  Serial.printf("[mode] %s -> %s\n", modeName(s_mode), modeName(m));
  s_mode = m;
  s_modeSince = millis();
}

static void applyOverrideCheck();

// ---- Scanning ---------------------------------------------------------------
static void kickScan() {
  if (s_scanRunning) return;
  s_lastScanKick = millis();
  WiFi.scanNetworks(true /*async*/, false /*hidden*/, false, 120 /*ms per channel: keeps AP beacon gaps short*/);
  s_scanRunning = true;
  s_scanWanted = false;
  Serial.println("[scan] started");
}

static void pollScan() {
  if (!s_scanRunning) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  s_scanRunning = false;
  if (n < 0) {
    Serial.printf("[scan] failed (%d)\n", n);
    return;
  }
  s_scan.clear();
  s_scan.reserve(n);
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    // Keep only the strongest BSSID for each SSID.
    bool dup = false;
    for (auto& e : s_scan) {
      if (e.ssid == ssid) {
        dup = true;
        if (WiFi.RSSI(i) > e.rssi) {
          e.rssi = WiFi.RSSI(i);
          e.channel = WiFi.channel(i);
          e.auth = WiFi.encryptionType(i);
        }
        break;
      }
    }
    if (dup) continue;
    s_scan.push_back({ssid, (int)WiFi.RSSI(i), (int)WiFi.channel(i), WiFi.encryptionType(i)});
  }
  WiFi.scanDelete();
  std::sort(s_scan.begin(), s_scan.end(), [](const ScanEntry& a, const ScanEntry& b) { return a.rssi > b.rssi; });
  s_scanCompletedAt = millis();
  s_scanEver = true;
  Serial.printf("[scan] %d unique SSID(s)\n", (int)s_scan.size());
  applyOverrideCheck();
}

// Strongest saved network visible in the latest scan, or -1.
static int bestSavedIndex() {
  int best = -1;
  for (size_t i = 0; i < s_scan.size(); i++) {
    if (!store.contains(s_scan[i].ssid)) continue;
    if (best < 0 || s_scan[i].rssi > s_scan[best].rssi) best = i;
  }
  return best;
}

// Strongest enabled over-ride SSID visible in the latest scan, or "".
static String visibleOverrideSsid() {
  for (auto& e : s_scan) {
    if (store.overrideEnabled(e.ssid)) return e.ssid;
  }
  return "";
}

// ---- STA / NAT ----------------------------------------------------------------
static void startConnect(const String& ssid) {
  int idx = store.indexOf(ssid);
  if (idx < 0) {
    s_lastError = "SSID not in saved list";
    return;
  }
  s_target = ssid;
  s_lastError = "";
  s_connectStarted = millis();
  Serial.printf("[sta] connecting to \"%s\"\n", ssid.c_str());
  WiFi.disconnect(false, false);
  WiFi.begin(ssid.c_str(), store.at(idx).pass.c_str());
  setMode(Mode::CONNECTING);
}

static void enableRouting() {
  IPAddress dns = WiFi.STA.dnsIP();
  if ((uint32_t)dns == 0) dns = FALLBACK_DNS;
  dnsProxy.setUpstream(dns);
  bool ok = WiFi.AP.enableNAPT(true);
  Serial.printf("[nat] %s | sta ip %s gw %s dns %s\n", ok ? "enabled" : "FAILED", WiFi.STA.localIP().toString().c_str(),
                WiFi.STA.gatewayIP().toString().c_str(), dns.toString().c_str());
  if (!ok) s_lastError = "NAPT enable failed";
  setMode(Mode::ROUTING);
}

static void disableRouting(const char* why) {
  dnsProxy.setUpstream(IPAddress());
  WiFi.AP.enableNAPT(false);
  Serial.printf("[nat] disabled (%s)\n", why);
}

static void scheduleRetry(const char* why) {
  s_lastError = why;
  disableRouting(why);
  WiFi.disconnect(false, false);
  s_retryAt = millis() + RECONNECT_DELAY_MS;
  setMode(Mode::RETRY);
}

// Enter or leave OVERRIDE based on the latest scan. Called after every scan.
static void applyOverrideCheck() {
  String ov = visibleOverrideSsid();
  bool visible = ov.length() > 0;
  if (visible) {
    switch (s_mode) {
      case Mode::COUNTDOWN:
      case Mode::RETRY:
        Serial.printf("[override] \"%s\" visible; routing suppressed\n", ov.c_str());
        setMode(Mode::OVERRIDE);
        break;
      case Mode::CONNECTING:
      case Mode::ROUTING:
        Serial.printf("[override] \"%s\" visible; dropping uplink\n", ov.c_str());
        disableRouting("over-ride SSID visible");
        WiFi.disconnect(false, false);
        s_target = "";
        setMode(Mode::OVERRIDE);
        break;
      default:  // WAITING / HOLD / OVERRIDE already have NAT off
        break;
    }
  } else if (s_mode == Mode::OVERRIDE) {
    Serial.println("[override] clear; resuming");
    if (store.count() > 0) {
      s_retryAt = millis();
      setMode(Mode::RETRY);
    } else {
      setMode(Mode::WAITING);
    }
  }
}

// Try to join the strongest visible saved network; otherwise keep scanning.
static void tryBest() {
  if (s_scanRunning) return;
  int b = bestSavedIndex();
  if (b >= 0) {
    startConnect(s_scan[b].ssid);
  } else if (millis() - s_lastScanKick >= IDLE_SCAN_INTERVAL_MS) {
    kickScan();
  }
}

// ---- Events -------------------------------------------------------------------
static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:         ev_gotIp = true; break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:   ev_lost = true; break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:    Serial.println("[ap] client joined"); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: Serial.println("[ap] client left"); break;
    default: break;
  }
}

// ---- Public API (used by the web UI) ----------------------------------------------
namespace app {
Mode mode() { return s_mode; }
uint32_t countdownRemainingMs() {
  if (s_mode != Mode::COUNTDOWN) return 0;
  int32_t d = (int32_t)(s_countdownEnd - millis());
  return d > 0 ? d : 0;
}
const std::vector<ScanEntry>& scanResults() { return s_scan; }
uint32_t scanAgeMs() { return s_scanEver ? millis() - s_scanCompletedAt : UINT32_MAX; }
bool scanInProgress() { return s_scanRunning; }
String targetSsid() { return s_target; }
String lastError() { return s_lastError; }
String visibleOverride() { return visibleOverrideSsid(); }

void requestScan() { s_scanWanted = true; }

void connectNow(const String& ssid) {
  if (visibleOverrideSsid().length()) {
    s_lastError = "over-ride SSID visible; routing suppressed";
    return;
  }
  if (s_mode == Mode::ROUTING || s_mode == Mode::CONNECTING) disableRouting("user reconnect");
  if (ssid.length()) {
    startConnect(ssid);
    return;
  }
  // "" = pick the strongest saved network now. Refresh the scan first if the
  // last one is stale, then let tryBest() do the join.
  WiFi.disconnect(false, false);
  s_retryAt = millis();
  if (scanAgeMs() > IDLE_SCAN_INTERVAL_MS) s_scanWanted = true;
  setMode(Mode::RETRY);
}

void disconnect() {
  disableRouting("user disconnect");
  WiFi.disconnect(false, false);
  s_target = "";
  setMode(Mode::HOLD);
}

void onStoreChanged() {
  // Over-ride list may have changed: re-evaluate against the latest scan.
  applyOverrideCheck();
  if (s_mode == Mode::OVERRIDE) return;
  if (store.count() == 0) {
    if (s_mode != Mode::WAITING) {
      disableRouting("list emptied");
      WiFi.disconnect(false, false);
      s_target = "";
    }
    setMode(Mode::WAITING);
    return;
  }
  // First selection while idle: start the grace countdown so the user can
  // keep editing, then auto-connect (same as boot behaviour).
  if (s_mode == Mode::WAITING) {
    s_countdownEnd = millis() + BOOT_GRACE_MS;
    setMode(Mode::COUNTDOWN);
    return;
  }
  // If the network we're on was just removed, drop it.
  if ((s_mode == Mode::ROUTING || s_mode == Mode::CONNECTING) && !store.contains(s_target)) {
    scheduleRetry("current network removed");
  }
}
}  // namespace app

// ---- Arduino entry points ----------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Penrith MRT Xebro ===");
  Serial.printf("chip: %s rev%d, %d cores, flash %u MB, psram %u KB\n", ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)), (unsigned)(ESP.getPsramSize() / 1024));

  store.load();

  Network.onEvent(onWifiEvent);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);  // we choose which saved SSID to (re)join
  WiFi.setSleep(false);          // lower latency for routed traffic

  // AP first so the config page is reachable immediately.
  const IPAddress apIp = store.apIp();
  WiFi.AP.begin();
  WiFi.AP.config(apIp, apIp, AP_MASK, store.apLeaseStart(), apIp /*DHCP-offered DNS = our proxy*/);
  bool apOk = WiFi.AP.create(AP_SSID, strlen(AP_PASS) ? AP_PASS : nullptr, AP_CHANNEL, 0, AP_MAX_CLIENTS);
  if (!apOk || !WiFi.AP.waitStatusBits(ESP_NETIF_STARTED_BIT, 2000)) {
    Serial.println("[ap] FAILED to start");
  } else {
    Serial.printf("[ap] \"%s\" up at %s\n", AP_SSID, WiFi.AP.localIP().toString().c_str());
  }

  // STA interface must exist for scanning even before we join anything.
  WiFi.STA.begin();

  dnsProxy.begin(apIp);
  webui::begin(apIp);

  if (store.count() > 0) {
    s_countdownEnd = millis() + BOOT_GRACE_MS;
    setMode(Mode::COUNTDOWN);
    Serial.printf("[boot] %d saved network(s); auto-connect in %lus\n", store.count(), (unsigned long)(BOOT_GRACE_MS / 1000));
  } else {
    setMode(Mode::WAITING);
    Serial.println("[boot] no saved networks; waiting for selection");
  }
  kickScan();
}

void loop() {
  webui::loop();
  dnsProxy.loop();
  pollScan();

  // Wi-Fi events
  if (ev_gotIp) {
    ev_gotIp = false;
    if (s_mode == Mode::CONNECTING) enableRouting();
  }
  if (ev_lost) {
    ev_lost = false;
    if (s_mode == Mode::ROUTING || s_mode == Mode::CONNECTING) scheduleRetry("link lost");
  }

  // User asked for a scan (from the UI). Avoid scanning mid-join.
  if (s_scanWanted && !s_scanRunning && s_mode != Mode::CONNECTING) kickScan();

  switch (s_mode) {
    case Mode::WAITING:
    case Mode::HOLD:
    case Mode::OVERRIDE:
      if (!s_scanRunning && millis() - s_lastScanKick >= IDLE_SCAN_INTERVAL_MS) kickScan();
      break;

    case Mode::COUNTDOWN:
      if ((int32_t)(millis() - s_countdownEnd) >= 0) {
        s_retryAt = millis();
        setMode(Mode::RETRY);
      } else if (!s_scanRunning && millis() - s_lastScanKick >= IDLE_SCAN_INTERVAL_MS) {
        kickScan();
      }
      break;

    case Mode::RETRY:
      if ((int32_t)(millis() - s_retryAt) >= 0) tryBest();
      break;

    case Mode::CONNECTING:
      if (millis() - s_connectStarted > CONNECT_TIMEOUT_MS) scheduleRetry("connect timeout");
      break;

    case Mode::ROUTING:
      // Periodic look for over-ride SSIDs appearing while we route.
      if (store.anyOverrideEnabled() && !s_scanRunning && millis() - s_lastScanKick >= OVERRIDE_CHECK_INTERVAL_MS) kickScan();
      break;
  }

  delay(2);
}
