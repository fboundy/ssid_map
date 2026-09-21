#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// HTTP UI + JSON API on port 80 of the AP interface.
namespace webui {
void begin(IPAddress apIp);
void loop();
}  // namespace webui
