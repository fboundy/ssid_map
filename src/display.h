#pragma once

// Status readout on the Waveshare ESP32-S3-Touch-LCD-1.9 (ST7789, 170x320)
// used in landscape. Shows mode, uplink SSID/IP/RSSI and the clients on the
// AP. No touch handling. Compiled in only with -DXEBRO_DISPLAY_WS19; otherwise
// start() is a no-op.
namespace display {
void start();
}  // namespace display
