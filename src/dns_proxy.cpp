#include "dns_proxy.h"
#include <atomic>
#include <cstring>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char* TAG = "dns";

static std::atomic<uint32_t> s_upstream{0};
static uint32_t s_captiveIp = 0;

static const int kMaxPending = 16;
static const int64_t kPendingTtlMs = 3000;

struct Pending {
  uint16_t ourId;
  uint16_t clientId;
  sockaddr_in client;
  int64_t sentAt;
  bool used;
};

static inline int64_t nowMs() { return esp_timer_get_time() / 1000; }

// Reply to the first question. A/IN gets the captive IP; anything else gets
// NOERROR with no answers so the client falls back to A.
static void answerCaptive(int sock, uint8_t* buf, int len, int cap, const sockaddr_in& to) {
  uint16_t qdcount = (buf[4] << 8) | buf[5];
  if (qdcount < 1) return;

  // Walk the QNAME to find QTYPE/QCLASS.
  int i = 12;
  while (i < len && buf[i] != 0) {
    if (buf[i] & 0xC0) { i += 1; break; }
    i += buf[i] + 1;
  }
  i += 1;
  if (i + 4 > len) return;
  uint16_t qtype = (buf[i] << 8) | buf[i + 1];
  uint16_t qclass = (buf[i + 2] << 8) | buf[i + 3];
  int questionEnd = i + 4;
  bool giveAnswer = (qtype == 1 && qclass == 1);

  buf[2] = 0x84;  // QR=1, opcode 0, AA=1
  buf[3] = 0x00;  // RA=0, RCODE=0
  buf[4] = 0; buf[5] = 1;                    // QDCOUNT=1
  buf[6] = 0; buf[7] = giveAnswer ? 1 : 0;   // ANCOUNT
  buf[8] = 0; buf[9] = 0;                    // NSCOUNT
  buf[10] = 0; buf[11] = 0;                  // ARCOUNT

  int out = questionEnd;
  if (giveAnswer && out + 16 <= cap) {
    const uint8_t* ip = (const uint8_t*)&s_captiveIp;
    buf[out++] = 0xC0; buf[out++] = 0x0C;  // name: pointer to question
    buf[out++] = 0x00; buf[out++] = 0x01;  // type A
    buf[out++] = 0x00; buf[out++] = 0x01;  // class IN
    buf[out++] = 0x00; buf[out++] = 0x00; buf[out++] = 0x00; buf[out++] = 0x0A;  // TTL 10s
    buf[out++] = 0x00; buf[out++] = 0x04;  // RDLENGTH
    buf[out++] = ip[0]; buf[out++] = ip[1]; buf[out++] = ip[2]; buf[out++] = ip[3];
  }
  sendto(sock, buf, out, 0, (const sockaddr*)&to, sizeof(to));
}

static void dnsTask(void*) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(TAG, "socket failed: %d", errno);
    vTaskDelete(nullptr);
    return;
  }
  sockaddr_in bindAddr = {};
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_port = htons(53);
  bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(sock, (sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
    ESP_LOGE(TAG, "bind failed: %d", errno);
    close(sock);
    vTaskDelete(nullptr);
    return;
  }
  timeval tv = {.tv_sec = 1, .tv_usec = 0};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ESP_LOGI(TAG, "listening on udp/53");

  static Pending pending[kMaxPending] = {};
  static uint8_t buf[512];
  uint16_t nextId = 1;

  for (;;) {
    sockaddr_in from = {};
    socklen_t fromLen = sizeof(from);
    int len = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&from, &fromLen);
    if (len < 12) continue;

    bool isResponse = buf[2] & 0x80;  // QR bit
    if (isResponse) {
      uint16_t id = (buf[0] << 8) | buf[1];
      for (auto& p : pending) {
        if (!p.used || p.ourId != id) continue;
        p.used = false;
        buf[0] = p.clientId >> 8;
        buf[1] = p.clientId & 0xFF;
        sendto(sock, buf, len, 0, (const sockaddr*)&p.client, sizeof(p.client));
        break;
      }
      continue;
    }

    uint32_t upstream = s_upstream.load();
    if (upstream == 0) {
      answerCaptive(sock, buf, len, sizeof(buf), from);
      continue;
    }

    // Find a free slot, expiring stale entries as we go.
    int64_t now = nowMs();
    Pending* slot = nullptr;
    for (auto& p : pending) {
      if (p.used && now - p.sentAt > kPendingTtlMs) p.used = false;
      if (!p.used && !slot) slot = &p;
    }
    if (!slot) {
      // Table full: answer captive-style rather than drop; client will retry.
      answerCaptive(sock, buf, len, sizeof(buf), from);
      continue;
    }
    slot->used = true;
    slot->clientId = (buf[0] << 8) | buf[1];
    slot->client = from;
    slot->sentAt = now;
    slot->ourId = nextId++;
    if (nextId == 0) nextId = 1;

    buf[0] = slot->ourId >> 8;
    buf[1] = slot->ourId & 0xFF;
    sockaddr_in up = {};
    up.sin_family = AF_INET;
    up.sin_port = htons(53);
    up.sin_addr.s_addr = upstream;
    sendto(sock, buf, len, 0, (const sockaddr*)&up, sizeof(up));
  }
}

namespace dnsproxy {
void start(uint32_t captiveIp) {
  s_captiveIp = captiveIp;
  xTaskCreatePinnedToCore(dnsTask, "dns", 4096, nullptr, 5, nullptr, 1);
}
void setUpstream(uint32_t upstream) { s_upstream.store(upstream); }
bool forwarding() { return s_upstream.load() != 0; }
}  // namespace dnsproxy
