#include "store.h"
#include <Preferences.h>

NetworkStore store;

static Preferences prefs;

// Layout in NVS: "n" = count, then "s<i>" / "p<i>" per selected network;
// "on" = over-ride count, then "o<i>" / "e<i>" (ssid / enabled) per over-ride;
// "apnet" = AP /24 network address as a uint32 (0 = default).
static String keyFor(char prefix, int i) {
  char k[8];
  snprintf(k, sizeof(k), "%c%d", prefix, i);
  return String(k);
}

void NetworkStore::load() {
  _count = 0;
  _apNet = AP_DEFAULT_NETWORK;
  // Open read-write first so a fresh chip gets the namespace created silently.
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    log_e("NVS open failed; running with an empty list");
    return;
  }
  uint32_t net = prefs.getUInt("apnet", 0);
  if (net != 0) _apNet = IPAddress(net);
  int n = prefs.getInt("n", 0);
  if (n < 0) n = 0;
  if (n > MAX_SAVED_NETWORKS) n = MAX_SAVED_NETWORKS;
  for (int i = 0; i < n; i++) {
    String s = prefs.getString(keyFor('s', i).c_str(), "");
    if (s.length() == 0) continue;
    _items[_count].ssid = s;
    _items[_count].pass = prefs.getString(keyFor('p', i).c_str(), "");
    _count++;
  }
  _ovCount = 0;
  int on = prefs.getInt("on", 0);
  if (on < 0) on = 0;
  if (on > MAX_OVERRIDES) on = MAX_OVERRIDES;
  for (int i = 0; i < on; i++) {
    String s = prefs.getString(keyFor('o', i).c_str(), "");
    if (s.length() == 0) continue;
    _ov[_ovCount].ssid = s;
    _ov[_ovCount].enabled = prefs.getBool(keyFor('e', i).c_str(), true);
    _ovCount++;
  }
  prefs.end();
  log_i("Loaded %d saved network(s), %d over-ride(s) from NVS", _count, _ovCount);
}

void NetworkStore::save() {
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    log_e("Failed to open NVS for writing");
    return;
  }
  prefs.clear();
  prefs.putInt("n", _count);
  for (int i = 0; i < _count; i++) {
    prefs.putString(keyFor('s', i).c_str(), _items[i].ssid);
    prefs.putString(keyFor('p', i).c_str(), _items[i].pass);
  }
  prefs.putUInt("apnet", (uint32_t)_apNet);
  prefs.putInt("on", _ovCount);
  for (int i = 0; i < _ovCount; i++) {
    prefs.putString(keyFor('o', i).c_str(), _ov[i].ssid);
    prefs.putBool(keyFor('e', i).c_str(), _ov[i].enabled);
  }
  prefs.end();
  log_i("Saved %d network(s), %d over-ride(s) to NVS", _count, _ovCount);
}

int NetworkStore::overrideIndexOf(const String& ssid) const {
  for (int i = 0; i < _ovCount; i++) {
    if (_ov[i].ssid == ssid) return i;
  }
  return -1;
}

bool NetworkStore::overrideEnabled(const String& ssid) const {
  int i = overrideIndexOf(ssid);
  return i >= 0 && _ov[i].enabled;
}

bool NetworkStore::anyOverrideEnabled() const {
  for (int i = 0; i < _ovCount; i++) {
    if (_ov[i].enabled) return true;
  }
  return false;
}

bool NetworkStore::putOverride(const String& ssid, bool enabled) {
  if (ssid.length() == 0 || ssid.length() > 32) return false;
  int i = overrideIndexOf(ssid);
  if (i < 0) {
    if (_ovCount >= MAX_OVERRIDES) return false;
    i = _ovCount++;
    _ov[i].ssid = ssid;
  }
  _ov[i].enabled = enabled;
  save();
  return true;
}

bool NetworkStore::removeOverride(const String& ssid) {
  int i = overrideIndexOf(ssid);
  if (i < 0) return false;
  for (int j = i; j < _ovCount - 1; j++) _ov[j] = _ov[j + 1];
  _ovCount--;
  _ov[_ovCount] = Override{};
  save();
  return true;
}

int NetworkStore::indexOf(const String& ssid) const {
  for (int i = 0; i < _count; i++) {
    if (_items[i].ssid == ssid) return i;
  }
  return -1;
}

bool NetworkStore::put(const String& ssid, const String& pass) {
  if (ssid.length() == 0 || ssid.length() > 32) return false;
  int i = indexOf(ssid);
  if (i < 0) {
    if (_count >= MAX_SAVED_NETWORKS) return false;
    i = _count++;
    _items[i].ssid = ssid;
  }
  _items[i].pass = pass;
  save();
  return true;
}

bool NetworkStore::remove(const String& ssid) {
  int i = indexOf(ssid);
  if (i < 0) return false;
  for (int j = i; j < _count - 1; j++) _items[j] = _items[j + 1];
  _count--;
  _items[_count] = SavedNetwork{};
  save();
  return true;
}

void NetworkStore::clear() {
  for (int i = 0; i < _count; i++) _items[i] = SavedNetwork{};
  _count = 0;
  save();
}

bool NetworkStore::setApNetwork(IPAddress net) {
  // Private (RFC1918) /24 only, and never a network we could not host.
  bool priv = (net[0] == 10) || (net[0] == 172 && net[1] >= 16 && net[1] <= 31) || (net[0] == 192 && net[1] == 168);
  if (!priv) return false;
  _apNet = IPAddress(net[0], net[1], net[2], 0);
  save();
  return true;
}
