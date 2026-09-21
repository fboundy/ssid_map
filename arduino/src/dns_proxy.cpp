#include "dns_proxy.h"

DnsProxy dnsProxy;

static const uint32_t kPendingTtlMs = 3000;

void DnsProxy::begin(IPAddress captiveIp) {
  _captiveIp = captiveIp;
  _udp.begin(53);
}

void DnsProxy::loop() {
  int len = _udp.parsePacket();
  if (len <= 0) return;
  if (len > (int)sizeof(_buf)) len = sizeof(_buf);
  len = _udp.read(_buf, len);
  if (len < 12) return;  // shorter than a DNS header

  bool isResponse = _buf[2] & 0x80;  // QR bit
  if (isResponse) {
    handleResponse(_buf, len);
  } else {
    handleQuery(_buf, len, _udp.remoteIP(), _udp.remotePort());
  }
}

void DnsProxy::handleQuery(uint8_t* buf, int len, IPAddress from, uint16_t port) {
  if (!forwarding()) {
    answerCaptive(buf, len, from, port);
    return;
  }

  // Find a free slot, expiring stale entries as we go.
  uint32_t now = millis();
  Pending* slot = nullptr;
  for (auto& p : _pending) {
    if (p.used && now - p.sentAt > kPendingTtlMs) p.used = false;
    if (!p.used && !slot) slot = &p;
  }
  if (!slot) {
    // Table full: answer captive-style rather than drop, client will retry.
    answerCaptive(buf, len, from, port);
    return;
  }

  slot->used = true;
  slot->clientId = (buf[0] << 8) | buf[1];
  slot->client = from;
  slot->clientPort = port;
  slot->sentAt = now;
  slot->ourId = _nextId++;
  if (_nextId == 0) _nextId = 1;

  buf[0] = slot->ourId >> 8;
  buf[1] = slot->ourId & 0xFF;
  _udp.beginPacket(_upstream, 53);
  _udp.write(buf, len);
  _udp.endPacket();
}

void DnsProxy::handleResponse(uint8_t* buf, int len) {
  uint16_t id = (buf[0] << 8) | buf[1];
  for (auto& p : _pending) {
    if (!p.used || p.ourId != id) continue;
    p.used = false;
    buf[0] = p.clientId >> 8;
    buf[1] = p.clientId & 0xFF;
    _udp.beginPacket(p.client, p.clientPort);
    _udp.write(buf, len);
    _udp.endPacket();
    return;
  }
}

// Reply to the first question. A/IN gets the captive IP; anything else gets
// NOERROR with no answers so the client falls back to A.
void DnsProxy::answerCaptive(uint8_t* buf, int len, IPAddress to, uint16_t port) {
  uint16_t qdcount = (buf[4] << 8) | buf[5];
  if (qdcount < 1) return;

  // Walk the QNAME to find QTYPE/QCLASS.
  int i = 12;
  while (i < len && buf[i] != 0) {
    if (buf[i] & 0xC0) { i += 1; break; }  // compression pointer (unusual in queries)
    i += buf[i] + 1;
  }
  i += 1;  // terminating zero
  if (i + 4 > len) return;
  uint16_t qtype = (buf[i] << 8) | buf[i + 1];
  uint16_t qclass = (buf[i + 2] << 8) | buf[i + 3];
  int questionEnd = i + 4;

  bool giveAnswer = (qtype == 1 && qclass == 1);

  buf[2] = 0x84;  // QR=1, opcode 0, AA=1
  buf[3] = 0x00;  // RA=0, RCODE=0
  buf[4] = 0; buf[5] = 1;  // QDCOUNT=1
  buf[6] = 0; buf[7] = giveAnswer ? 1 : 0;  // ANCOUNT
  buf[8] = 0; buf[9] = 0;  // NSCOUNT
  buf[10] = 0; buf[11] = 0;  // ARCOUNT

  int out = questionEnd;
  if (giveAnswer && out + 16 <= (int)sizeof(_buf)) {
    buf[out++] = 0xC0; buf[out++] = 0x0C;  // name: pointer to question
    buf[out++] = 0x00; buf[out++] = 0x01;  // type A
    buf[out++] = 0x00; buf[out++] = 0x01;  // class IN
    buf[out++] = 0x00; buf[out++] = 0x00; buf[out++] = 0x00; buf[out++] = 0x0A;  // TTL 10s
    buf[out++] = 0x00; buf[out++] = 0x04;  // RDLENGTH
    buf[out++] = _captiveIp[0]; buf[out++] = _captiveIp[1];
    buf[out++] = _captiveIp[2]; buf[out++] = _captiveIp[3];
  }

  _udp.beginPacket(to, port);
  _udp.write(buf, out);
  _udp.endPacket();
}
