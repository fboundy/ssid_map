// Penrith MRT Xebro - Wi-Fi SSID selector and NAT router for ESP32-S3 (ESP-IDF).
//
// Boot:  AP "Penrith MRT Xebro" comes up (default 192.168.137.1) with a web UI.
//        If SSIDs are saved in NVS, wait BOOT_GRACE_MS (scanning), then join
//        the strongest visible saved network and NAT AP clients through it.
//        If nothing is saved, wait for the user to select networks.
//        If an enabled over-ride SSID is in range, NAT stays off.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "dhcpserver/dhcpserver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "config.h"
#include "app.h"
#include "store.h"
#include "dns_proxy.h"
#include "web_ui.h"
#include "display.h"
#include "speedtest.h"

static const char* TAG = "xebro";

// ---- State (guarded by s_lock) ----------------------------------------------
static SemaphoreHandle_t s_lock;
static esp_netif_t* s_apNetif = nullptr;
static esp_netif_t* s_staNetif = nullptr;

static Mode s_mode = Mode::WAITING;
static int64_t s_countdownEnd = 0;
static int64_t s_retryAt = 0;
static int64_t s_connectStarted = 0;
static std::string s_target;
static std::string s_lastError;
static bool s_natOn = false;

static std::vector<ScanEntry> s_scan;
static int64_t s_scanCompletedAt = 0;
static bool s_scanEver = false;
static bool s_scanRunning = false;
static bool s_scanWanted = false;
static int64_t s_lastScanKick = 0;

// Commands queued by the web UI.
static bool s_cmdConnect = false;
static std::string s_cmdSsid;
static bool s_cmdDisconnect = false;

// Flags set from the event task, consumed in the state-machine loop.
static std::atomic<bool> ev_gotIp{false};
static std::atomic<bool> ev_lost{false};
static std::atomic<int> ev_lostReason{0};
static std::atomic<bool> ev_scanDone{false};
static std::atomic<bool> s_staLinked{false};

static inline int64_t nowMs() { return esp_timer_get_time() / 1000; }

void appLock() { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
void appUnlock() { xSemaphoreGiveRecursive(s_lock); }

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
  ESP_LOGI(TAG, "mode %s -> %s", modeName(s_mode), modeName(m));
  s_mode = m;
}

// ---- Scanning ---------------------------------------------------------------
static void applyOverrideCheck();

static void kickScan() {
  if (s_scanRunning) return;
  s_lastScanKick = nowMs();
  wifi_scan_config_t cfg = {};
  cfg.show_hidden = false;
  cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  cfg.scan_time.active.min = 0;
  cfg.scan_time.active.max = SCAN_DWELL_MS;
  esp_err_t err = esp_wifi_scan_start(&cfg, false);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "scan start failed: %s", esp_err_to_name(err));
    return;
  }
  s_scanRunning = true;
  s_scanWanted = false;
}

static void collectScan() {
  s_scanRunning = false;
  uint16_t n = 0;
  esp_wifi_scan_get_ap_num(&n);
  std::vector<wifi_ap_record_t> recs(n);
  if (n > 0) {
    esp_wifi_scan_get_ap_records(&n, recs.data());
  } else {
    esp_wifi_clear_ap_list();
  }
  s_scan.clear();
  s_scan.reserve(n);
  for (uint16_t i = 0; i < n; i++) {
    std::string ssid((const char*)recs[i].ssid);
    if (ssid.empty()) continue;
    // Keep only the strongest BSSID for each SSID.
    bool dup = false;
    for (auto& e : s_scan) {
      if (e.ssid == ssid) {
        dup = true;
        if (recs[i].rssi > e.rssi) {
          e.rssi = recs[i].rssi;
          e.channel = recs[i].primary;
          e.auth = recs[i].authmode;
        }
        break;
      }
    }
    if (dup) continue;
    s_scan.push_back({ssid, recs[i].rssi, recs[i].primary, recs[i].authmode});
  }
  std::sort(s_scan.begin(), s_scan.end(), [](const ScanEntry& a, const ScanEntry& b) { return a.rssi > b.rssi; });
  s_scanCompletedAt = nowMs();
  s_scanEver = true;
  ESP_LOGI(TAG, "scan: %d unique SSID(s)", (int)s_scan.size());
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
static std::string visibleOverrideSsid() {
  for (auto& e : s_scan) {
    if (store.overrideEnabled(e.ssid)) return e.ssid;
  }
  return "";
}

// ---- STA / NAT ----------------------------------------------------------------
static void staDisconnect() {
  if (s_staLinked.load() || s_mode == Mode::CONNECTING) esp_wifi_disconnect();
}

static void startConnect(const std::string& ssid) {
  int idx = store.indexOf(ssid);
  if (idx < 0) {
    s_lastError = "SSID not in saved list";
    return;
  }
  const std::string& pass = store.at(idx).pass;
  s_target = ssid;
  s_lastError.clear();
  s_connectStarted = nowMs();
  ESP_LOGI(TAG, "sta: connecting to \"%s\"", ssid.c_str());

  wifi_config_t cfg = {};
  strncpy((char*)cfg.sta.ssid, ssid.c_str(), sizeof(cfg.sta.ssid));
  strncpy((char*)cfg.sta.password, pass.c_str(), sizeof(cfg.sta.password));
  cfg.sta.threshold.authmode = pass.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
  cfg.sta.pmf_cfg.capable = true;
  cfg.sta.pmf_cfg.required = false;
  cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

  staDisconnect();  // any resulting ASSOC_LEAVE event is ignored while CONNECTING
  esp_wifi_set_config(WIFI_IF_STA, &cfg);
  esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK) ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
  setMode(Mode::CONNECTING);
}

static void enableRouting() {
  esp_netif_dns_info_t d = {};
  uint32_t dns = 0;
  if (esp_netif_get_dns_info(s_staNetif, ESP_NETIF_DNS_MAIN, &d) == ESP_OK) dns = d.ip.u_addr.ip4.addr;
  if (dns == 0) ip4FromString(FALLBACK_DNS, dns);
  dnsproxy::setUpstream(dns);

  esp_err_t err = esp_netif_napt_enable(s_apNetif);
  s_natOn = (err == ESP_OK);
  esp_netif_ip_info_t ip = {};
  esp_netif_get_ip_info(s_staNetif, &ip);
  ESP_LOGI(TAG, "nat %s | sta ip " IPSTR " gw " IPSTR " dns %s", s_natOn ? "enabled" : "FAILED", IP2STR(&ip.ip), IP2STR(&ip.gw),
           ip4ToString(dns).c_str());
  if (!s_natOn) s_lastError = std::string("NAPT enable failed: ") + esp_err_to_name(err);
  setMode(Mode::ROUTING);
  speedtest::trigger();
}

static void disableRouting(const char* why) {
  dnsproxy::setUpstream(0);
  if (s_natOn) {
    esp_netif_napt_disable(s_apNetif);
    s_natOn = false;
    ESP_LOGI(TAG, "nat disabled (%s)", why);
  }
}

static void scheduleRetry(const char* why) {
  s_lastError = why;
  disableRouting(why);
  staDisconnect();
  s_retryAt = nowMs() + RECONNECT_DELAY_MS;
  setMode(Mode::RETRY);
}

// Enter or leave OVERRIDE based on the latest scan. Called after every scan.
static void applyOverrideCheck() {
  std::string ov = visibleOverrideSsid();
  if (!ov.empty()) {
    switch (s_mode) {
      case Mode::COUNTDOWN:
      case Mode::RETRY:
        ESP_LOGI(TAG, "override: \"%s\" visible; routing suppressed", ov.c_str());
        setMode(Mode::OVERRIDE);
        break;
      case Mode::CONNECTING:
      case Mode::ROUTING:
        ESP_LOGI(TAG, "override: \"%s\" visible; dropping uplink", ov.c_str());
        disableRouting("over-ride SSID visible");
        staDisconnect();
        s_target.clear();
        setMode(Mode::OVERRIDE);
        break;
      default:  // WAITING / HOLD / OVERRIDE already have NAT off
        break;
    }
  } else if (s_mode == Mode::OVERRIDE) {
    ESP_LOGI(TAG, "override: clear; resuming");
    if (store.count() > 0) {
      s_retryAt = nowMs();
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
  } else if (nowMs() - s_lastScanKick >= (int64_t)IDLE_SCAN_INTERVAL_MS) {
    kickScan();
  }
}

// ---- Events (run in the default event task) -------------------------------------------
static void onEvent(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT) {
    switch (id) {
      case WIFI_EVENT_SCAN_DONE: ev_scanDone = true; break;
      case WIFI_EVENT_STA_CONNECTED: s_staLinked = true; break;
      case WIFI_EVENT_STA_DISCONNECTED: {
        s_staLinked = false;
        auto* e = (wifi_event_sta_disconnected_t*)data;
        ev_lostReason = e ? e->reason : 0;
        ev_lost = true;
        break;
      }
      case WIFI_EVENT_AP_STACONNECTED: ESP_LOGI(TAG, "ap: client joined"); break;
      case WIFI_EVENT_AP_STADISCONNECTED: ESP_LOGI(TAG, "ap: client left"); break;
      default: break;
    }
  } else if (base == IP_EVENT) {
    if (id == IP_EVENT_STA_GOT_IP) ev_gotIp = true;
    else if (id == IP_EVENT_STA_LOST_IP) { ev_lostReason = 0; ev_lost = true; }
  }
}

// ---- Public API (used by the web UI) ----------------------------------------------
namespace app {
StatusSnapshot snapshot() {
  StatusSnapshot s;
  appLock();
  s.mode = s_mode;
  s.countdownMs = 0;
  if (s_mode == Mode::COUNTDOWN) {
    int64_t d = s_countdownEnd - nowMs();
    s.countdownMs = d > 0 ? (uint32_t)d : 0;
  }
  s.target = s_target;
  s.error = s_lastError;
  s.visibleOverride = visibleOverrideSsid();
  s.nat = s_natOn;
  s.scanning = s_scanRunning;
  s.scanAgeMs = s_scanEver ? nowMs() - s_scanCompletedAt : -1;
  s.scan = s_scan;
  appUnlock();

  s.staConnected = false;
  s.staRssi = 0;
  esp_netif_ip_info_t ip = {};
  if (s_staLinked.load() && esp_netif_get_ip_info(s_staNetif, &ip) == ESP_OK && ip.ip.addr != 0) {
    s.staConnected = true;
    s.staIp = ip4ToString(ip.ip.addr);
    s.staGw = ip4ToString(ip.gw.addr);
    esp_netif_dns_info_t d = {};
    if (esp_netif_get_dns_info(s_staNetif, ESP_NETIF_DNS_MAIN, &d) == ESP_OK) s.staDns = ip4ToString(d.ip.u_addr.ip4.addr);
    wifi_ap_record_t rec = {};
    if (esp_wifi_sta_get_ap_info(&rec) == ESP_OK) {
      s.staSsid = (const char*)rec.ssid;
      s.staRssi = rec.rssi;
    }
  }
  wifi_sta_list_t stas = {};
  if (esp_wifi_ap_get_sta_list(&stas) == ESP_OK && stas.num > 0) {
    wifi_sta_mac_ip_list_t ips = {};
    esp_wifi_ap_get_sta_list_with_ip(&stas, &ips);
    for (int i = 0; i < stas.num; i++) {
      char mac[18];
      const uint8_t* m = stas.sta[i].mac;
      snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
      ApClient c;
      c.mac = mac;
      if (i < ips.num && ips.sta[i].ip.addr != 0) c.ip = ip4ToString(ips.sta[i].ip.addr);
      s.clients.push_back(c);
    }
  }
  return s;
}

void requestScan() {
  appLock();
  s_scanWanted = true;
  appUnlock();
}

bool connectNow(const std::string& ssid, std::string& err) {
  appLock();
  bool ok = true;
  if (!visibleOverrideSsid().empty()) {
    err = "over-ride SSID visible; routing suppressed";
    ok = false;
  } else if (!ssid.empty() && !store.contains(ssid)) {
    err = "Not in list";
    ok = false;
  } else if (store.count() == 0) {
    err = "No networks selected";
    ok = false;
  } else {
    s_cmdConnect = true;
    s_cmdSsid = ssid;
  }
  appUnlock();
  return ok;
}

void disconnect() {
  appLock();
  s_cmdDisconnect = true;
  appUnlock();
}

void onStoreChanged() {
  // Over-ride list may have changed: re-evaluate against the latest scan.
  applyOverrideCheck();
  if (s_mode == Mode::OVERRIDE) return;
  if (store.count() == 0) {
    if (s_mode != Mode::WAITING) {
      disableRouting("list emptied");
      staDisconnect();
      s_target.clear();
    }
    setMode(Mode::WAITING);
    return;
  }
  // First selection while idle: start the grace countdown so the user can
  // keep editing, then auto-connect (same as boot behaviour).
  if (s_mode == Mode::WAITING) {
    s_countdownEnd = nowMs() + BOOT_GRACE_MS;
    setMode(Mode::COUNTDOWN);
    return;
  }
  // If the network we're on was just removed, drop it.
  if ((s_mode == Mode::ROUTING || s_mode == Mode::CONNECTING) && !store.contains(s_target)) {
    scheduleRetry("current network removed");
  }
}
}  // namespace app

// ---- Commands from the UI, applied in the state-machine loop -------------------------------
static void applyCommands() {
  if (s_cmdDisconnect) {
    s_cmdDisconnect = false;
    s_cmdConnect = false;
    disableRouting("user disconnect");
    staDisconnect();
    s_target.clear();
    setMode(Mode::HOLD);
  }
  if (s_cmdConnect) {
    s_cmdConnect = false;
    if (s_mode == Mode::ROUTING || s_mode == Mode::CONNECTING) disableRouting("user reconnect");
    if (!s_cmdSsid.empty()) {
      startConnect(s_cmdSsid);
    } else {
      // "" = pick the strongest saved network now. Refresh the scan first if
      // the last one is stale, then let tryBest() do the join.
      staDisconnect();
      s_retryAt = nowMs();
      if (!s_scanEver || nowMs() - s_scanCompletedAt > (int64_t)IDLE_SCAN_INTERVAL_MS) s_scanWanted = true;
      setMode(Mode::RETRY);
    }
  }
}

// ---- One pass of the state machine ---------------------------------------------------------
static void tick() {
  applyCommands();

  if (ev_scanDone.exchange(false)) collectScan();

  if (ev_gotIp.exchange(false)) {
    if (s_mode == Mode::CONNECTING) enableRouting();
  }
  if (ev_lost.exchange(false)) {
    int reason = ev_lostReason.load();
    // Our own esp_wifi_disconnect() before a fresh connect shows up as ASSOC_LEAVE.
    bool selfInflicted = (s_mode == Mode::CONNECTING && reason == WIFI_REASON_ASSOC_LEAVE);
    if (!selfInflicted && (s_mode == Mode::ROUTING || s_mode == Mode::CONNECTING)) {
      ESP_LOGW(TAG, "sta: link lost (reason %d)", reason);
      scheduleRetry("link lost");
    }
  }

  // User asked for a scan (from the UI). Avoid scanning mid-join.
  if (s_scanWanted && !s_scanRunning && s_mode != Mode::CONNECTING) kickScan();

  int64_t now = nowMs();
  switch (s_mode) {
    case Mode::WAITING:
    case Mode::HOLD:
    case Mode::OVERRIDE:
      if (!s_scanRunning && now - s_lastScanKick >= (int64_t)IDLE_SCAN_INTERVAL_MS) kickScan();
      break;

    case Mode::COUNTDOWN:
      if (now >= s_countdownEnd) {
        s_retryAt = now;
        setMode(Mode::RETRY);
      } else if (!s_scanRunning && now - s_lastScanKick >= (int64_t)IDLE_SCAN_INTERVAL_MS) {
        kickScan();
      }
      break;

    case Mode::RETRY:
      if (now >= s_retryAt) tryBest();
      break;

    case Mode::CONNECTING:
      if (now - s_connectStarted > (int64_t)CONNECT_TIMEOUT_MS) scheduleRetry("connect timeout");
      break;

    case Mode::ROUTING:
      // Periodic look for over-ride SSIDs appearing while we route.
      if (store.anyOverrideEnabled() && !s_scanRunning && now - s_lastScanKick >= (int64_t)OVERRIDE_CHECK_INTERVAL_MS) kickScan();
      break;
  }
}

// ---- Bring-up ---------------------------------------------------------------------
static void configureAp() {
  uint32_t apIp = store.apIp();

  // Static IP for the AP interface and DHCP server settings.
  esp_netif_dhcps_stop(s_apNetif);
  esp_netif_ip_info_t info = {};
  info.ip.addr = apIp;
  info.gw.addr = apIp;
  ip4FromString("255.255.255.0", info.netmask.addr);
  ESP_ERROR_CHECK(esp_netif_set_ip_info(s_apNetif, &info));

  // Offer ourselves as DNS (the captive/forwarding proxy).
  esp_netif_dns_info_t dns = {};
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = apIp;
  ESP_ERROR_CHECK(esp_netif_set_dns_info(s_apNetif, ESP_NETIF_DNS_MAIN, &dns));
  dhcps_offer_t offer = OFFER_DNS;
  ESP_ERROR_CHECK(esp_netif_dhcps_option(s_apNetif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer)));

  dhcps_lease_t lease = {};
  lease.enable = true;
  lease.start_ip.addr = store.apLeaseStart();
  lease.end_ip.addr = store.apLeaseEnd();
  ESP_ERROR_CHECK(esp_netif_dhcps_option(s_apNetif, ESP_NETIF_OP_SET, ESP_NETIF_REQUESTED_IP_ADDRESS, &lease, sizeof(lease)));
  ESP_ERROR_CHECK(esp_netif_dhcps_start(s_apNetif));

  wifi_config_t ap = {};
  strncpy((char*)ap.ap.ssid, AP_SSID, sizeof(ap.ap.ssid));
  ap.ap.ssid_len = strlen(AP_SSID);
  strncpy((char*)ap.ap.password, AP_PASS, sizeof(ap.ap.password));
  ap.ap.channel = AP_CHANNEL;
  ap.ap.max_connection = AP_MAX_CLIENTS;
  ap.ap.authmode = strlen(AP_PASS) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  ap.ap.pmf_cfg.required = false;
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
}

extern "C" void app_main(void) {
  s_lock = xSemaphoreCreateRecursiveMutex();

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  esp_chip_info_t chip;
  esp_chip_info(&chip);
  uint32_t flashSize = 0;
  esp_flash_get_size(nullptr, &flashSize);
  ESP_LOGI(TAG, "=== Penrith MRT Xebro (ESP-IDF %s) ===", esp_get_idf_version());
  ESP_LOGI(TAG, "chip rev %d, %d cores, flash %lu MB, psram %u KB, free heap %lu", chip.revision, chip.cores,
           (unsigned long)(flashSize / (1024 * 1024)), (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned long)esp_get_free_heap_size());

  store.load();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  s_apNetif = esp_netif_create_default_wifi_ap();
  s_staNetif = esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onEvent, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &onEvent, nullptr));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

  wifi_country_t country = {};
  strncpy(country.cc, WIFI_COUNTRY, sizeof(country.cc));
  country.schan = 1;
  country.nchan = WIFI_COUNTRY_NCHAN;
  country.policy = WIFI_COUNTRY_POLICY_MANUAL;
  ESP_ERROR_CHECK(esp_wifi_set_country(&country));

  configureAp();
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));  // no modem sleep: latency + throughput
  ESP_LOGI(TAG, "ap: \"%s\" up at %s", AP_SSID, ip4ToString(store.apIp()).c_str());

  dnsproxy::start(store.apIp());
  webui::start(store.apIp());
  display::start();    // no-op unless built for a board with an LCD
  speedtest::start();  // no-op unless built with -DXEBRO_SPEEDTEST

  appLock();
  if (store.count() > 0) {
    s_countdownEnd = nowMs() + BOOT_GRACE_MS;
    setMode(Mode::COUNTDOWN);
    ESP_LOGI(TAG, "boot: %d saved network(s); auto-connect in %lus", store.count(), (unsigned long)(BOOT_GRACE_MS / 1000));
  } else {
    ESP_LOGI(TAG, "boot: no saved networks; waiting for selection");
  }
  kickScan();
  appUnlock();

  for (;;) {
    appLock();
    tick();
    appUnlock();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
