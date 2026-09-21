#include "web_ui.h"
#include <WebServer.h>
#include <WiFi.h>
#include "config.h"
#include "app.h"
#include "store.h"
#include "dns_proxy.h"
#include "page.h"

static WebServer server(80);
static IPAddress s_apIp;
static bool s_rebootPending = false;
static uint32_t s_rebootAt = 0;

// ---- JSON helpers ---------------------------------------------------------------
static void jsonStr(String& out, const String& s) {
  out += '"';
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) {
          char b[8];
          snprintf(b, sizeof(b), "\\u%04x", c);
          out += b;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

static void kv(String& out, const char* k, const String& v, bool last = false) {
  jsonStr(out, k); out += ':'; jsonStr(out, v); if (!last) out += ',';
}
static void kv(String& out, const char* k, long v, bool last = false) {
  jsonStr(out, k); out += ':'; out += String(v); if (!last) out += ',';
}
static void kv(String& out, const char* k, bool v, bool last = false) {
  jsonStr(out, k); out += ':'; out += v ? "true" : "false"; if (!last) out += ',';
}

static void sendJson(const String& body, int code = 200) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

static void sendResult(bool ok, const String& msg = "") {
  String out = "{";
  kv(out, "ok", ok);
  kv(out, "msg", msg, true);
  out += "}";
  sendJson(out, ok ? 200 : 400);
}

// ---- Handlers -------------------------------------------------------------------
static void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", PAGE_HTML);
}

static void handleStatus() {
  bool staUp = WiFi.STA.connected() && (uint32_t)WiFi.STA.localIP() != 0;
  const auto& scan = app::scanResults();

  String out;
  out.reserve(1024 + scan.size() * 96 + store.count() * 48 + store.overrideCount() * 48);
  out += "{";
  kv(out, "mode", String(modeName(app::mode())));
  kv(out, "countdown", (long)app::countdownRemainingMs());
  kv(out, "target", app::targetSsid());
  kv(out, "error", app::lastError());
  kv(out, "override", app::visibleOverride());
  kv(out, "nat", dnsProxy.forwarding());
  kv(out, "scanning", app::scanInProgress());
  kv(out, "scanAge", (long)(app::scanAgeMs() == UINT32_MAX ? -1 : (long)app::scanAgeMs()));
  kv(out, "uptime", (long)(millis() / 1000));
  kv(out, "heap", (long)ESP.getFreeHeap());
  kv(out, "rebooting", s_rebootPending);

  out += "\"ap\":{";
  kv(out, "ssid", String(AP_SSID));
  kv(out, "ip", s_apIp.toString());
  kv(out, "net", store.apNetwork().toString());
  kv(out, "clients", (long)WiFi.AP.stationCount(), true);
  out += "},";

  out += "\"sta\":{";
  kv(out, "connected", staUp);
  kv(out, "ssid", staUp ? WiFi.STA.SSID() : String(""));
  kv(out, "ip", staUp ? WiFi.STA.localIP().toString() : String(""));
  kv(out, "gw", staUp ? WiFi.STA.gatewayIP().toString() : String(""));
  kv(out, "dns", staUp ? WiFi.STA.dnsIP().toString() : String(""));
  kv(out, "rssi", (long)(staUp ? WiFi.STA.RSSI() : 0), true);
  out += "},";

  out += "\"scan\":[";
  for (size_t i = 0; i < scan.size(); i++) {
    if (i) out += ',';
    out += '{';
    kv(out, "ssid", scan[i].ssid);
    kv(out, "rssi", (long)scan[i].rssi);
    kv(out, "ch", (long)scan[i].channel);
    kv(out, "secure", scan[i].auth != WIFI_AUTH_OPEN);
    kv(out, "saved", store.contains(scan[i].ssid));
    kv(out, "override", store.overrideIndexOf(scan[i].ssid) >= 0, true);
    out += '}';
  }
  out += "],";

  out += "\"saved\":[";
  for (int i = 0; i < store.count(); i++) {
    if (i) out += ',';
    out += '{';
    kv(out, "ssid", store.at(i).ssid);
    kv(out, "hasPass", store.at(i).pass.length() > 0, true);
    out += '}';
  }
  out += "],";

  out += "\"overrides\":[";
  for (int i = 0; i < store.overrideCount(); i++) {
    if (i) out += ',';
    out += '{';
    kv(out, "ssid", store.overrideAt(i).ssid);
    kv(out, "enabled", store.overrideAt(i).enabled, true);
    out += '}';
  }
  out += "]}";
  sendJson(out);
}

static void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim();
  if (ssid.length() == 0) return sendResult(false, "SSID required");
  if (pass.length() > 0 && pass.length() < 8) return sendResult(false, "WPA passphrase must be 8-63 characters");
  if (pass.length() > 63) return sendResult(false, "Passphrase too long");
  if (!store.put(ssid, pass)) return sendResult(false, "List full (max " + String(MAX_SAVED_NETWORKS) + ")");
  app::onStoreChanged();
  sendResult(true, "Saved " + ssid);
}

static void handleRemove() {
  String ssid = server.arg("ssid");
  if (!store.remove(ssid)) return sendResult(false, "Not in list");
  app::onStoreChanged();
  sendResult(true, "Removed " + ssid);
}

static void handleConnect() {
  String ssid = server.arg("ssid");
  if (store.count() == 0) return sendResult(false, "No networks selected");
  if (ssid.length() && !store.contains(ssid)) return sendResult(false, "Not in list");
  app::connectNow(ssid);
  String err = app::lastError();
  if (err.length()) return sendResult(false, err);
  sendResult(true, ssid.length() ? "Connecting to " + ssid : "Connecting to strongest selected network");
}

static void handleDisconnect() {
  app::disconnect();
  sendResult(true, "Disconnected; holding in AP-only mode");
}

static void handleScan() {
  app::requestScan();
  sendResult(true, "Scanning");
}

static void handleOverride() {
  String ssid = server.arg("ssid");
  ssid.trim();
  bool enabled = server.arg("enabled") != "0";
  if (ssid.length() == 0) return sendResult(false, "SSID required");
  if (!store.putOverride(ssid, enabled)) return sendResult(false, "Over-ride list full (max " + String(MAX_OVERRIDES) + ")");
  app::onStoreChanged();
  sendResult(true, (enabled ? "Over-ride enabled: " : "Over-ride disabled: ") + ssid);
}

static void handleOverrideRemove() {
  String ssid = server.arg("ssid");
  if (!store.removeOverride(ssid)) return sendResult(false, "Not in list");
  app::onStoreChanged();
  sendResult(true, "Removed over-ride " + ssid);
}

static void handleApNet() {
  String net = server.arg("net");
  net.trim();
  IPAddress ip;
  if (!ip.fromString(net)) return sendResult(false, "Enter a network like 192.168.137.0");
  if (!store.setApNetwork(ip)) return sendResult(false, "Must be a private (10/8, 172.16/12, 192.168/16) /24 network");
  sendResult(true, "AP network set to " + store.apNetwork().toString() + "/24. Reboot to apply.");
}

static void handleReboot() {
  s_rebootPending = true;
  s_rebootAt = millis() + 800;
  sendResult(true, "Rebooting");
}

// Captive-portal catch-all: anything we don't serve bounces to the UI.
static void handleNotFound() {
  server.sendHeader("Location", "http://" + s_apIp.toString() + "/", true);
  server.sendHeader("Cache-Control", "no-store");
  server.send(302, "text/plain", "");
}

// ---- Public -----------------------------------------------------------------------
namespace webui {
void begin(IPAddress apIp) {
  s_apIp = apIp;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/save", HTTP_POST, handleSave);
  server.on("/api/remove", HTTP_POST, handleRemove);
  server.on("/api/connect", HTTP_POST, handleConnect);
  server.on("/api/disconnect", HTTP_POST, handleDisconnect);
  server.on("/api/scan", HTTP_POST, handleScan);
  server.on("/api/override", HTTP_POST, handleOverride);
  server.on("/api/override/remove", HTTP_POST, handleOverrideRemove);
  server.on("/api/apnet", HTTP_POST, handleApNet);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.printf("[web] http://%s/\n", apIp.toString().c_str());
}

void loop() {
  server.handleClient();
  if (s_rebootPending && (int32_t)(millis() - s_rebootAt) >= 0) {
    Serial.println("[web] reboot requested");
    ESP.restart();
  }
}
}  // namespace webui
