#pragma once
#include <cstdint>

// Tiny DNS server on UDP/53 for AP clients, running in its own task.
//
//  * Captive mode (no upstream): every A query is answered with the AP's own
//    IP so phones/laptops pop the "sign in to network" page, which lands on
//    the web UI.
//  * Forward mode (routing): queries are relayed to the upstream resolver
//    learned from the STA link and replies are relayed back to the client.
//
// Addresses are IPv4 in network byte order.
namespace dnsproxy {
void start(uint32_t captiveIp);
void setUpstream(uint32_t upstream);  // 0 = captive mode
bool forwarding();
}  // namespace dnsproxy
