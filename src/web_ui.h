#pragma once
#include <cstdint>

// HTTP UI + JSON API on port 80 (esp_http_server). apIp is used for the
// captive-portal redirect; network byte order.
namespace webui {
void start(uint32_t apIp);
}  // namespace webui
