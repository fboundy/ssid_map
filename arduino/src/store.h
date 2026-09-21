#pragma once
#include <Arduino.h>
#include "config.h"

// A user-selected network the device is allowed to join in STA mode.
struct SavedNetwork {
  String ssid;
  String pass;
};

// An SSID whose presence means "do not route": if any enabled over-ride is
// visible in a scan, NAT is disabled and the STA link dropped.
struct Override {
  String ssid;
  bool enabled;
};

// Persistent lists of selected SSIDs and over-ride SSIDs, backed by NVS.
class NetworkStore {
 public:
  void load();
  void save();

  // --- AP subnet (/24 network address, e.g. 192.168.137.0) ---
  IPAddress apNetwork() const { return _apNet; }
  IPAddress apIp() const { return IPAddress(_apNet[0], _apNet[1], _apNet[2], AP_HOST_OCTET); }
  IPAddress apLeaseStart() const { return IPAddress(_apNet[0], _apNet[1], _apNet[2], AP_LEASE_OCTET); }
  bool setApNetwork(IPAddress net);  // takes effect after reboot

  // --- over-rides ---
  int overrideCount() const { return _ovCount; }
  const Override& overrideAt(int i) const { return _ov[i]; }
  int overrideIndexOf(const String& ssid) const;
  bool overrideEnabled(const String& ssid) const;
  bool anyOverrideEnabled() const;
  bool putOverride(const String& ssid, bool enabled);  // add or update
  bool removeOverride(const String& ssid);

  // --- selected networks ---

  int count() const { return _count; }
  const SavedNetwork& at(int i) const { return _items[i]; }
  int indexOf(const String& ssid) const;
  bool contains(const String& ssid) const { return indexOf(ssid) >= 0; }

  // Adds or updates the password for an SSID. Returns false when full.
  bool put(const String& ssid, const String& pass);
  bool remove(const String& ssid);
  void clear();

 private:
  SavedNetwork _items[MAX_SAVED_NETWORKS];
  int _count = 0;
  Override _ov[MAX_OVERRIDES];
  int _ovCount = 0;
  IPAddress _apNet = AP_DEFAULT_NETWORK;
};

extern NetworkStore store;
