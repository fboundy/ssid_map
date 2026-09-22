#include "web_ui.h"
#include <cstring>
#include <map>
#include <string>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"
#include "app.h"
#include "store.h"
#include "dns_proxy.h"
#include "speedtest.h"
#include "page.h"

static const char* TAG = "web";
static std::string s_apIpStr;
static bool s_rebootPending = false;

// ---- Request helpers -----------------------------------------------------------
static std::string urlDecode(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); i++) {
    char c = in[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < in.size()) {
      char hex[3] = {in[i + 1], in[i + 2], 0};
      out += (char)strtol(hex, nullptr, 16);
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

// Reads an application/x-www-form-urlencoded body into a key/value map.
static bool readForm(httpd_req_t* req, std::map<std::string, std::string>& form) {
  int len = req->content_len;
  if (len < 0 || len > 1024) return false;
  std::string body(len, '\0');
  int got = 0;
  while (got < len) {
    int r = httpd_req_recv(req, &body[got], len - got);
    if (r <= 0) return false;
    got += r;
  }
  size_t pos = 0;
  while (pos <= body.size()) {
    size_t amp = body.find('&', pos);
    if (amp == std::string::npos) amp = body.size();
    std::string pair = body.substr(pos, amp - pos);
    size_t eq = pair.find('=');
    if (eq != std::string::npos) {
      form[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
    } else if (!pair.empty()) {
      form[urlDecode(pair)] = "";
    }
    pos = amp + 1;
  }
  return true;
}

static std::string trim(std::string s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  size_t b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static esp_err_t sendJson(httpd_req_t* req, cJSON* root, const char* status = "200 OK") {
  char* text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t r = httpd_resp_sendstr(req, text ? text : "{}");
  cJSON_free(text);
  return r;
}

static esp_err_t sendResult(httpd_req_t* req, bool ok, const std::string& msg) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", ok);
  cJSON_AddStringToObject(root, "msg", msg.c_str());
  return sendJson(req, root, ok ? "200 OK" : "400 Bad Request");
}

// ---- Handlers ----------------------------------------------------------------------
static esp_err_t handleRoot(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handleStatus(httpd_req_t* req) {
  StatusSnapshot s = app::snapshot();

  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "mode", modeName(s.mode));
  cJSON_AddNumberToObject(root, "countdown", s.countdownMs);
  cJSON_AddStringToObject(root, "target", s.target.c_str());
  cJSON_AddStringToObject(root, "error", s.error.c_str());
  cJSON_AddStringToObject(root, "override", s.visibleOverride.c_str());
  cJSON_AddBoolToObject(root, "nat", s.nat);
  cJSON_AddBoolToObject(root, "scanning", s.scanning);
  cJSON_AddNumberToObject(root, "scanAge", (double)s.scanAgeMs);
  cJSON_AddNumberToObject(root, "uptime", (double)(esp_timer_get_time() / 1000000));
  cJSON_AddNumberToObject(root, "heap", (double)esp_get_free_heap_size());
  cJSON_AddBoolToObject(root, "rebooting", s_rebootPending);

  appLock();
  std::string apNet = ip4ToString(store.apNetwork());
  cJSON* saved = cJSON_AddArrayToObject(root, "saved");
  for (int i = 0; i < store.count(); i++) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "ssid", store.at(i).ssid.c_str());
    cJSON_AddBoolToObject(o, "hasPass", !store.at(i).pass.empty());
    cJSON_AddItemToArray(saved, o);
  }
  cJSON* ovs = cJSON_AddArrayToObject(root, "overrides");
  for (int i = 0; i < store.overrideCount(); i++) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "ssid", store.overrideAt(i).ssid.c_str());
    cJSON_AddBoolToObject(o, "enabled", store.overrideAt(i).enabled);
    cJSON_AddItemToArray(ovs, o);
  }
  cJSON* scan = cJSON_AddArrayToObject(root, "scan");
  for (const auto& n : s.scan) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "ssid", n.ssid.c_str());
    cJSON_AddNumberToObject(o, "rssi", n.rssi);
    cJSON_AddNumberToObject(o, "ch", n.channel);
    cJSON_AddBoolToObject(o, "secure", n.auth != WIFI_AUTH_OPEN);
    cJSON_AddBoolToObject(o, "saved", store.contains(n.ssid));
    cJSON_AddBoolToObject(o, "override", store.overrideIndexOf(n.ssid) >= 0);
    cJSON_AddItemToArray(scan, o);
  }
  appUnlock();

  cJSON* ap = cJSON_AddObjectToObject(root, "ap");
  cJSON_AddStringToObject(ap, "ssid", AP_SSID);
  cJSON_AddStringToObject(ap, "ip", s_apIpStr.c_str());
  cJSON_AddStringToObject(ap, "net", apNet.c_str());
  cJSON_AddNumberToObject(ap, "clients", s.apClients);

  cJSON* sta = cJSON_AddObjectToObject(root, "sta");
  cJSON_AddBoolToObject(sta, "connected", s.staConnected);
  cJSON_AddStringToObject(sta, "ssid", s.staSsid.c_str());
  cJSON_AddStringToObject(sta, "ip", s.staIp.c_str());
  cJSON_AddStringToObject(sta, "gw", s.staGw.c_str());
  cJSON_AddStringToObject(sta, "dns", s.staDns.c_str());
  cJSON_AddNumberToObject(sta, "rssi", s.staRssi);

  speedtest::Result st = speedtest::last();
  cJSON* sp = cJSON_AddObjectToObject(root, "speed");
#ifdef XEBRO_SPEEDTEST
  cJSON_AddBoolToObject(sp, "enabled", true);
#else
  cJSON_AddBoolToObject(sp, "enabled", false);
#endif
  cJSON_AddBoolToObject(sp, "valid", st.valid);
  cJSON_AddBoolToObject(sp, "running", st.running);
  cJSON_AddNumberToObject(sp, "down", st.downMbps);
  cJSON_AddNumberToObject(sp, "up", st.upMbps);
  cJSON_AddNumberToObject(sp, "ping", st.pingMs);
  cJSON_AddStringToObject(sp, "server", st.server.c_str());
  cJSON_AddStringToObject(sp, "error", st.error.c_str());
  cJSON_AddNumberToObject(sp, "ageSec", st.valid ? (double)((esp_timer_get_time() / 1000 - st.atMs) / 1000) : -1);

  return sendJson(req, root);
}

static esp_err_t handleSave(httpd_req_t* req) {
  std::map<std::string, std::string> f;
  if (!readForm(req, f)) return sendResult(req, false, "Bad request");
  std::string ssid = trim(f["ssid"]), pass = f["pass"];
  if (ssid.empty()) return sendResult(req, false, "SSID required");
  if (!pass.empty() && pass.size() < 8) return sendResult(req, false, "WPA passphrase must be 8-63 characters");
  if (pass.size() > 63) return sendResult(req, false, "Passphrase too long");
  appLock();
  bool ok = store.put(ssid, pass);
  if (ok) app::onStoreChanged();
  appUnlock();
  if (!ok) return sendResult(req, false, "List full (max " + std::to_string(MAX_SAVED_NETWORKS) + ")");
  return sendResult(req, true, "Saved " + ssid);
}

static esp_err_t handleRemove(httpd_req_t* req) {
  std::map<std::string, std::string> f;
  if (!readForm(req, f)) return sendResult(req, false, "Bad request");
  appLock();
  bool ok = store.remove(f["ssid"]);
  if (ok) app::onStoreChanged();
  appUnlock();
  if (!ok) return sendResult(req, false, "Not in list");
  return sendResult(req, true, "Removed " + f["ssid"]);
}

static esp_err_t handleConnect(httpd_req_t* req) {
  std::map<std::string, std::string> f;
  if (!readForm(req, f)) return sendResult(req, false, "Bad request");
  std::string err;
  if (!app::connectNow(f["ssid"], err)) return sendResult(req, false, err);
  return sendResult(req, true, f["ssid"].empty() ? "Connecting to strongest selected network" : "Connecting to " + f["ssid"]);
}

static esp_err_t handleDisconnect(httpd_req_t* req) {
  app::disconnect();
  return sendResult(req, true, "Disconnected; holding in AP-only mode");
}

static esp_err_t handleScan(httpd_req_t* req) {
  app::requestScan();
  return sendResult(req, true, "Scanning");
}

static esp_err_t handleOverride(httpd_req_t* req) {
  std::map<std::string, std::string> f;
  if (!readForm(req, f)) return sendResult(req, false, "Bad request");
  std::string ssid = trim(f["ssid"]);
  bool enabled = f["enabled"] != "0";
  if (ssid.empty()) return sendResult(req, false, "SSID required");
  appLock();
  bool ok = store.putOverride(ssid, enabled);
  if (ok) app::onStoreChanged();
  appUnlock();
  if (!ok) return sendResult(req, false, "Over-ride list full (max " + std::to_string(MAX_OVERRIDES) + ")");
  return sendResult(req, true, (enabled ? "Over-ride enabled: " : "Over-ride disabled: ") + ssid);
}

static esp_err_t handleOverrideRemove(httpd_req_t* req) {
  std::map<std::string, std::string> f;
  if (!readForm(req, f)) return sendResult(req, false, "Bad request");
  appLock();
  bool ok = store.removeOverride(f["ssid"]);
  if (ok) app::onStoreChanged();
  appUnlock();
  if (!ok) return sendResult(req, false, "Not in list");
  return sendResult(req, true, "Removed over-ride " + f["ssid"]);
}

static esp_err_t handleApNet(httpd_req_t* req) {
  std::map<std::string, std::string> f;
  if (!readForm(req, f)) return sendResult(req, false, "Bad request");
  uint32_t net = 0;
  if (!ip4FromString(trim(f["net"]), net)) return sendResult(req, false, "Enter a network like 192.168.137.0");
  appLock();
  bool ok = store.setApNetwork(net);
  std::string now = ip4ToString(store.apNetwork());
  appUnlock();
  if (!ok) return sendResult(req, false, "Must be a private (10/8, 172.16/12, 192.168/16) /24 network");
  return sendResult(req, true, "AP network set to " + now + "/24. Reboot to apply.");
}

static esp_err_t handleSpeedtest(httpd_req_t* req) {
#ifdef XEBRO_SPEEDTEST
  if (app::snapshot().mode != Mode::ROUTING) return sendResult(req, false, "Not routing; no uplink to test");
  speedtest::trigger();
  return sendResult(req, true, "Speed test starting");
#else
  return sendResult(req, false, "Speed test not built into this firmware");
#endif
}

static void rebootTask(void*) {
  vTaskDelay(pdMS_TO_TICKS(800));
  ESP_LOGI(TAG, "reboot requested");
  esp_restart();
}

static esp_err_t handleReboot(httpd_req_t* req) {
  s_rebootPending = true;
  xTaskCreate(rebootTask, "reboot", 2048, nullptr, 5, nullptr);
  return sendResult(req, true, "Rebooting");
}

// Captive-portal catch-all: anything we don't serve bounces to the UI.
static esp_err_t handleNotFound(httpd_req_t* req) {
  std::string loc = "http://" + s_apIpStr + "/";
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", loc.c_str());
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, nullptr, 0);
}

// ---- Public -------------------------------------------------------------------------
namespace webui {
void start(uint32_t apIp) {
  s_apIpStr = ip4ToString(apIp);

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 16;
  cfg.lru_purge_enable = true;
  cfg.stack_size = 8192;
  cfg.core_id = 1;
  cfg.uri_match_fn = httpd_uri_match_wildcard;

  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed");
    return;
  }

  const httpd_uri_t routes[] = {
      {.uri = "/", .method = HTTP_GET, .handler = handleRoot, .user_ctx = nullptr},
      {.uri = "/api/status", .method = HTTP_GET, .handler = handleStatus, .user_ctx = nullptr},
      {.uri = "/api/save", .method = HTTP_POST, .handler = handleSave, .user_ctx = nullptr},
      {.uri = "/api/remove", .method = HTTP_POST, .handler = handleRemove, .user_ctx = nullptr},
      {.uri = "/api/connect", .method = HTTP_POST, .handler = handleConnect, .user_ctx = nullptr},
      {.uri = "/api/disconnect", .method = HTTP_POST, .handler = handleDisconnect, .user_ctx = nullptr},
      {.uri = "/api/scan", .method = HTTP_POST, .handler = handleScan, .user_ctx = nullptr},
      {.uri = "/api/override", .method = HTTP_POST, .handler = handleOverride, .user_ctx = nullptr},
      {.uri = "/api/override/remove", .method = HTTP_POST, .handler = handleOverrideRemove, .user_ctx = nullptr},
      {.uri = "/api/apnet", .method = HTTP_POST, .handler = handleApNet, .user_ctx = nullptr},
      {.uri = "/api/reboot", .method = HTTP_POST, .handler = handleReboot, .user_ctx = nullptr},
      {.uri = "/api/speedtest", .method = HTTP_POST, .handler = handleSpeedtest, .user_ctx = nullptr},
      // Registered last: wildcard catch-all for captive-portal probes.
      {.uri = "/*", .method = HTTP_GET, .handler = handleNotFound, .user_ctx = nullptr},
  };
  for (const auto& r : routes) httpd_register_uri_handler(server, &r);
  ESP_LOGI(TAG, "http://%s/", s_apIpStr.c_str());
}
}  // namespace webui
