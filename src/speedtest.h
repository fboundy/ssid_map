#pragma once
#include <cstdint>
#include <string>

// Periodic Ookla (speedtest.net) throughput test over the STA uplink.
// Compiled in with -DXEBRO_SPEEDTEST (Waveshare build); otherwise no-ops.
namespace speedtest {

struct Result {
  bool valid = false;    // a completed test is available
  bool running = false;  // a test is in progress right now
  float downMbps = 0;
  float upMbps = 0;
  int pingMs = 0;        // TCP connect time to the test server
  std::string server;    // "sponsor, city"
  std::string error;     // last failure, if any
  int64_t atMs = 0;      // uptime when the result was taken
};

void start();     // create the worker task
void trigger();   // run a test soon (called when the uplink comes up, or from the UI)
Result last();

}  // namespace speedtest
