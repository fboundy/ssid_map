#pragma once
#include <string>
#include <cstdint>
#include "config.h"

// A user-selected network the device is allowed to join in STA mode.
struct SavedNetwork {
  std::string ssid;
  std::string pass;
};

// An SSID whose presence means "do not route": if any enabled over-ride is
// visible in a scan, NAT is disabled and the STA link dropped.
struct Override {
  std::string ssid;
  bool enabled = true;
};

// Persistent lists of selected SSIDs and over-ride SSIDs plus the AP subnet,
// backed by NVS. Not thread-safe by itself: callers hold the app lock.
class NetworkStore {
 public:
  void load();
  void save();

  // --- AP subnet (/24). Addresses are IPv4 in network byte order. ---
  uint32_t apNetwork() const { return _apNet; }
  uint32_t apIp() const { return hostInNet(AP_HOST_OCTET); }
  uint32_t apLeaseStart() const { return hostInNet(AP_LEASE_OCTET); }
  uint32_t apLeaseEnd() const { return hostInNet(AP_LEASE_OCTET + AP_LEASE_COUNT - 1); }
  bool setApNetwork(uint32_t net);  // takes effect after reboot

  // --- over-rides ---
  int overrideCount() const { return _ovCount; }
  const Override& overrideAt(int i) const { return _ov[i]; }
  int overrideIndexOf(const std::string& ssid) const;
  bool overrideEnabled(const std::string& ssid) const;
  bool anyOverrideEnabled() const;
  bool putOverride(const std::string& ssid, bool enabled);  // add or update
  bool removeOverride(const std::string& ssid);

  // --- selected networks ---
  int count() const { return _count; }
  const SavedNetwork& at(int i) const { return _items[i]; }
  int indexOf(const std::string& ssid) const;
  bool contains(const std::string& ssid) const { return indexOf(ssid) >= 0; }
  bool put(const std::string& ssid, const std::string& pass);  // add or update; false when full
  bool remove(const std::string& ssid);

 private:
  uint32_t hostInNet(int octet) const;

  SavedNetwork _items[MAX_SAVED_NETWORKS];
  int _count = 0;
  Override _ov[MAX_OVERRIDES];
  int _ovCount = 0;
  uint32_t _apNet = 0;
};

extern NetworkStore store;

// Helpers for IPv4 addresses in network byte order.
std::string ip4ToString(uint32_t ip);
bool ip4FromString(const std::string& s, uint32_t& out);
