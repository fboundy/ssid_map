#include "speedtest.h"

#ifndef XEBRO_SPEEDTEST
namespace speedtest {
void start() {}
void trigger() {}
Result last() { return Result{}; }
}  // namespace speedtest
#else

#include <cstdio>
#include <cstring>
#include <string>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "app.h"
#include "config.h"

static const char* TAG = "speedtest";

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_kick;
static speedtest::Result s_result;

static inline int64_t nowMs() { return esp_timer_get_time() / 1000; }

static void setResult(const speedtest::Result& r) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_result = r;
  xSemaphoreGive(s_lock);
}

// ---- Server selection ------------------------------------------------------------
struct Server {
  std::string host;  // hostname only
  int port = 8080;
  std::string name;  // "sponsor, city"
};

// The speedtest.net JSON API lists nearby servers as host:port on 8080.
static bool fetchServerList(Server& out, std::string& err) {
  if (strlen(SPEEDTEST_SERVER)) {
    std::string h = SPEEDTEST_SERVER;
    size_t c = h.find(':');
    out.host = c == std::string::npos ? h : h.substr(0, c);
    out.port = c == std::string::npos ? 8080 : atoi(h.c_str() + c + 1);
    out.name = out.host;
    return true;
  }

  esp_http_client_config_t cfg = {};
  cfg.url = "https://www.speedtest.net/api/js/servers?engine=js&limit=3";
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 10000;
  cfg.user_agent = "Mozilla/5.0 (ESP32; " AP_SSID ")";
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) { err = "http init"; return false; }

  std::string body;
  bool ok = false;
  do {
    if (esp_http_client_open(client, 0) != ESP_OK) { err = "server list: connect failed"; break; }
    int64_t len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) { err = "server list: HTTP " + std::to_string(status); break; }
    if (len < 0) len = 8192;
    if (len > 16384) len = 16384;
    body.resize(len);
    int got = 0;
    while (got < (int)len) {
      int r = esp_http_client_read(client, &body[got], len - got);
      if (r <= 0) break;
      got += r;
    }
    body.resize(got);
    ok = got > 0;
    if (!ok) err = "server list: empty";
  } while (false);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (!ok) return false;

  cJSON* root = cJSON_Parse(body.c_str());
  if (!root || !cJSON_IsArray(root) || cJSON_GetArraySize(root) == 0) {
    cJSON_Delete(root);
    err = "server list: bad JSON";
    return false;
  }
  cJSON* first = cJSON_GetArrayItem(root, 0);
  cJSON* host = cJSON_GetObjectItem(first, "host");
  cJSON* sponsor = cJSON_GetObjectItem(first, "sponsor");
  cJSON* city = cJSON_GetObjectItem(first, "name");
  if (!cJSON_IsString(host)) {
    cJSON_Delete(root);
    err = "server list: no host";
    return false;
  }
  std::string h = host->valuestring;
  size_t c = h.find(':');
  out.host = c == std::string::npos ? h : h.substr(0, c);
  out.port = c == std::string::npos ? 8080 : atoi(h.c_str() + c + 1);
  out.name = (cJSON_IsString(sponsor) ? std::string(sponsor->valuestring) : out.host);
  if (cJSON_IsString(city)) out.name += std::string(", ") + city->valuestring;
  cJSON_Delete(root);
  return true;
}

// ---- Plain-HTTP transfer helpers (raw sockets, port 8080) -------------------------------
static int connectTo(const Server& s, int& connectMs) {
  addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(s.host.c_str(), std::to_string(s.port).c_str(), &hints, &res) != 0 || !res) return -1;
  int sock = socket(res->ai_family, res->ai_socktype, 0);
  if (sock < 0) { freeaddrinfo(res); return -1; }
  timeval tv = {.tv_sec = 8, .tv_usec = 0};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  int64_t t0 = nowMs();
  int rc = connect(sock, res->ai_addr, res->ai_addrlen);
  connectMs = (int)(nowMs() - t0);
  freeaddrinfo(res);
  if (rc != 0) { close(sock); return -1; }
  return sock;
}

static bool sendAll(int sock, const char* p, size_t n) {
  while (n) {
    int w = send(sock, p, n, 0);
    if (w <= 0) return false;
    p += w;
    n -= w;
  }
  return true;
}

// Reads the response head; returns the HTTP status (0 on failure) and the
// number of body bytes already consumed into buf past the header terminator.
static int readHead(int sock, char* buf, int cap, int& bodyBytes) {
  int got = 0;
  bodyBytes = 0;
  while (got < cap - 1) {
    int r = recv(sock, buf + got, cap - 1 - got, 0);
    if (r <= 0) return 0;
    got += r;
    buf[got] = 0;
    char* end = strstr(buf, "\r\n\r\n");
    if (end) {
      bodyBytes = got - (int)((end + 4) - buf);
      int status = 0;
      sscanf(buf, "HTTP/%*d.%*d %d", &status);
      return status;
    }
  }
  return 0;
}

static const int kBuf = 4096;

// Streams a GET body for `seconds`; returns Mbit/s, or <0 with err set.
static float download(const Server& s, const std::string& path, int seconds, std::string& err) {
  int ms;
  int sock = connectTo(s, ms);
  if (sock < 0) { err = "download: connect"; return -1; }
  std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + s.host + "\r\nUser-Agent: Xebro\r\nConnection: close\r\n\r\n";
  char* buf = new char[kBuf];
  float mbps = -1;
  do {
    if (!sendAll(sock, req.c_str(), req.size())) { err = "download: send"; break; }
    int body = 0;
    int status = readHead(sock, buf, kBuf, body);
    if (status != 200) { err = "download: HTTP " + std::to_string(status); break; }
    int64_t t0 = nowMs();
    int64_t bytes = body;
    for (;;) {
      int r = recv(sock, buf, kBuf, 0);
      if (r <= 0) break;
      bytes += r;
      if (nowMs() - t0 >= seconds * 1000) break;
    }
    int64_t dt = nowMs() - t0;
    if (dt < 500 || bytes < 65536) { err = "download: too little data"; break; }
    mbps = (float)bytes * 8.0f / (float)dt / 1000.0f;
  } while (false);
  delete[] buf;
  close(sock);
  return mbps;
}

// POSTs `bytes` of filler and times it; returns Mbit/s, or <0 with err set.
// Stops early (rate from bytes sent so far) if it runs past maxSeconds.
static float upload(const Server& s, const std::string& path, int64_t bytes, int maxSeconds, std::string& err) {
  int ms;
  int sock = connectTo(s, ms);
  if (sock < 0) { err = "upload: connect"; return -1; }
  std::string req = "POST " + path + " HTTP/1.1\r\nHost: " + s.host + "\r\nUser-Agent: Xebro\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
                    std::to_string(bytes) + "\r\nConnection: close\r\n\r\n";
  char* buf = new char[kBuf];
  for (int i = 0; i < kBuf; i++) buf[i] = 'A' + (i % 26);
  float mbps = -1;
  do {
    if (!sendAll(sock, req.c_str(), req.size())) { err = "upload: send"; break; }
    int64_t t0 = nowMs();
    int64_t sent = 0;
    bool timedOut = false;
    while (sent < bytes) {
      int chunk = (int)((bytes - sent) < kBuf ? (bytes - sent) : kBuf);
      if (!sendAll(sock, buf, chunk)) { err = "upload: send"; break; }
      sent += chunk;
      if (nowMs() - t0 >= maxSeconds * 1000) { timedOut = true; break; }
    }
    if (sent == 0) break;
    int64_t dt;
    if (!timedOut && sent == bytes) {
      // Wait for the server's reply so the clock covers the data actually landing.
      int body = 0;
      int status = readHead(sock, buf, kBuf, body);
      dt = nowMs() - t0;
      if (status != 200) { err = "upload: HTTP " + std::to_string(status); break; }
    } else {
      dt = nowMs() - t0;
    }
    if (dt < 500) { err = "upload: too short"; break; }
    mbps = (float)sent * 8.0f / (float)dt / 1000.0f;
    err.clear();
  } while (false);
  delete[] buf;
  close(sock);
  return mbps;
}

// ---- One full test ----------------------------------------------------------------------
static void runTest() {
  speedtest::Result r;
  r.running = true;
  {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    r.valid = s_result.valid;  // keep the previous numbers visible while running
    r.downMbps = s_result.downMbps;
    r.upMbps = s_result.upMbps;
    r.server = s_result.server;
    r.atMs = s_result.atMs;
    xSemaphoreGive(s_lock);
  }
  setResult(r);
  r.running = false;

  Server srv;
  std::string err;
  if (!fetchServerList(srv, err)) {
    ESP_LOGW(TAG, "%s", err.c_str());
    r.error = err;
    setResult(r);
    return;
  }
  ESP_LOGI(TAG, "server %s (%s:%d)", srv.name.c_str(), srv.host.c_str(), srv.port);

  int ping = 0;
  int probe = connectTo(srv, ping);
  if (probe < 0) {
    r.error = "connect to " + srv.host + " failed";
    ESP_LOGW(TAG, "%s", r.error.c_str());
    setResult(r);
    return;
  }
  close(probe);

  // Newer OoklaServer builds serve /download?size=; older ones the classic
  // random JPEG. Try both.
  std::string nonce = std::to_string(nowMs());
  float down = download(srv, "/download?nocache=" + nonce + "&size=100000000", SPEEDTEST_SECONDS, err);
  if (down < 0) down = download(srv, "/speedtest/random4000x4000.jpg?x=" + nonce, SPEEDTEST_SECONDS, err);
  if (down < 0) {
    r.error = err;
    ESP_LOGW(TAG, "%s", err.c_str());
    setResult(r);
    return;
  }

  // Size the upload from the download rate so it lasts about SPEEDTEST_SECONDS
  // on a symmetric link; the hard cap keeps a slow uplink from running away.
  int64_t upBytes = (int64_t)(down * 1000.0f * 1000.0f / 8.0f * SPEEDTEST_SECONDS);
  if (upBytes < 1000000) upBytes = 1000000;
  if (upBytes > 24000000) upBytes = 24000000;
  float up = upload(srv, "/upload?nocache=" + nonce, upBytes, SPEEDTEST_SECONDS * 3, err);
  if (up < 0) up = upload(srv, "/speedtest/upload.php?x=" + nonce, upBytes, SPEEDTEST_SECONDS * 3, err);

  r.valid = true;
  r.downMbps = down;
  r.upMbps = up < 0 ? 0 : up;
  r.pingMs = ping;
  r.server = srv.name;
  r.atMs = nowMs();
  r.error = up < 0 ? err : "";
  setResult(r);
  ESP_LOGI(TAG, "down %.1f Mbit/s, up %.1f Mbit/s, connect %d ms via %s%s%s", down, r.upMbps, ping, srv.name.c_str(),
           r.error.empty() ? "" : " | ", r.error.c_str());
}

static void speedtestTask(void*) {
  for (;;) {
    // Wait for a kick (uplink up / UI), or the hourly interval.
    bool kicked = xSemaphoreTake(s_kick, pdMS_TO_TICKS(SPEEDTEST_INTERVAL_MS)) == pdTRUE;
    if (kicked) vTaskDelay(pdMS_TO_TICKS(SPEEDTEST_DELAY_MS));  // let DHCP/DNS settle
    if (app::snapshot().mode != Mode::ROUTING) continue;
    runTest();
  }
}

namespace speedtest {
void start() {
  s_lock = xSemaphoreCreateMutex();
  s_kick = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(speedtestTask, "speedtest", 8192, nullptr, 2, nullptr, 0);
}
void trigger() {
  if (s_kick) xSemaphoreGive(s_kick);
}
Result last() {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  Result r = s_result;
  xSemaphoreGive(s_lock);
  return r;
}
}  // namespace speedtest

#endif  // XEBRO_SPEEDTEST
