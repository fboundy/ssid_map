#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>

// Tiny DNS server on UDP/53 for AP clients.
//
//  * Captive mode (no upstream): every A query is answered with the AP's own
//    IP so phones/laptops pop the "sign in to network" page, which lands on
//    the web UI.
//  * Forward mode (routing): queries are relayed to the upstream resolver
//    learned from the STA link and replies are relayed back to the client.
class DnsProxy {
 public:
  void begin(IPAddress captiveIp);
  void loop();

  // Set to an unset/zero address to fall back to captive mode.
  void setUpstream(IPAddress upstream) { _upstream = upstream; }
  bool forwarding() const { return (uint32_t)_upstream != 0; }

 private:
  struct Pending {
    uint16_t ourId;
    uint16_t clientId;
    IPAddress client;
    uint16_t clientPort;
    uint32_t sentAt;
    bool used;
  };

  void handleQuery(uint8_t* buf, int len, IPAddress from, uint16_t port);
  void handleResponse(uint8_t* buf, int len);
  void answerCaptive(uint8_t* buf, int len, IPAddress to, uint16_t port);

  WiFiUDP _udp;
  IPAddress _captiveIp;
  IPAddress _upstream;
  uint16_t _nextId = 1;
  static const int kMaxPending = 16;
  Pending _pending[kMaxPending] = {};
  uint8_t _buf[512];
};

extern DnsProxy dnsProxy;
