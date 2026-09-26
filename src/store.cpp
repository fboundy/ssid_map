#include "store.h"
#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "nvs.h"
#include "lwip/inet.h"

static const char* TAG = "store";

NetworkStore store;

std::string ip4ToString(uint32_t ip) {
  char buf[16];
  const uint8_t* b = (const uint8_t*)&ip;
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  return buf;
}

bool ip4FromString(const std::string& s, uint32_t& out) {
  ip4_addr_t a;
  if (!ip4addr_aton(s.c_str(), &a)) return false;
  out = a.addr;
  return true;
}

uint32_t NetworkStore::hostInNet(int octet) const {
  uint32_t ip = _apNet;
  ((uint8_t*)&ip)[3] = (uint8_t)octet;
  return ip;
}

// Layout in NVS: "n" = count, then "s<i>" / "p<i>" per selected network;
// "on" = over-ride count, then "o<i>" / "e<i>" (ssid / enabled) per over-ride;
// "apnet" = AP /24 network address (network byte order, 0 = default);
// "hmac"/"hname" = the single named host.
static std::string keyFor(char prefix, int i) {
  char k[8];
  snprintf(k, sizeof(k), "%c%d", prefix, i);
  return k;
}

static std::string getStr(nvs_handle_t h, const std::string& key) {
  size_t len = 0;
  if (nvs_get_str(h, key.c_str(), nullptr, &len) != ESP_OK || len == 0) return "";
  std::string v(len, '\0');
  if (nvs_get_str(h, key.c_str(), &v[0], &len) != ESP_OK) return "";
  v.resize(strlen(v.c_str()));
  return v;
}

void NetworkStore::load() {
  _count = 0;
  _ovCount = 0;
  _hostMac.clear();
  _hostName.clear();
  ip4FromString(AP_DEFAULT_NETWORK, _apNet);

  nvs_handle_t h;
  // Open read-write so a fresh chip gets the namespace created silently.
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
    ESP_LOGE(TAG, "NVS open failed; running with an empty list");
    return;
  }
  uint32_t net = 0;
  if (nvs_get_u32(h, "apnet", &net) == ESP_OK && net != 0) _apNet = net;

  int32_t n = 0;
  nvs_get_i32(h, "n", &n);
  if (n < 0) n = 0;
  if (n > MAX_SAVED_NETWORKS) n = MAX_SAVED_NETWORKS;
  for (int i = 0; i < n; i++) {
    std::string s = getStr(h, keyFor('s', i));
    if (s.empty()) continue;
    _items[_count].ssid = s;
    _items[_count].pass = getStr(h, keyFor('p', i));
    _count++;
  }

  int32_t on = 0;
  nvs_get_i32(h, "on", &on);
  if (on < 0) on = 0;
  if (on > MAX_OVERRIDES) on = MAX_OVERRIDES;
  for (int i = 0; i < on; i++) {
    std::string s = getStr(h, keyFor('o', i));
    if (s.empty()) continue;
    uint8_t en = 1;
    nvs_get_u8(h, keyFor('e', i).c_str(), &en);
    _ov[_ovCount].ssid = s;
    _ov[_ovCount].enabled = en != 0;
    _ovCount++;
  }
  _hostMac = getStr(h, "hmac");
  _hostName = getStr(h, "hname");

  nvs_close(h);
  ESP_LOGI(TAG, "loaded %d saved network(s), %d over-ride(s), AP net %s", _count, _ovCount, ip4ToString(_apNet).c_str());
}

void NetworkStore::save() {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
    ESP_LOGE(TAG, "NVS open for write failed");
    return;
  }
  nvs_erase_all(h);
  nvs_set_u32(h, "apnet", _apNet);
  nvs_set_i32(h, "n", _count);
  for (int i = 0; i < _count; i++) {
    nvs_set_str(h, keyFor('s', i).c_str(), _items[i].ssid.c_str());
    nvs_set_str(h, keyFor('p', i).c_str(), _items[i].pass.c_str());
  }
  nvs_set_i32(h, "on", _ovCount);
  for (int i = 0; i < _ovCount; i++) {
    nvs_set_str(h, keyFor('o', i).c_str(), _ov[i].ssid.c_str());
    nvs_set_u8(h, keyFor('e', i).c_str(), _ov[i].enabled ? 1 : 0);
  }
  nvs_set_str(h, "hmac", _hostMac.c_str());
  nvs_set_str(h, "hname", _hostName.c_str());
  esp_err_t err = nvs_commit(h);
  nvs_close(h);
  ESP_LOGI(TAG, "saved %d network(s), %d over-ride(s) (%s)", _count, _ovCount, esp_err_to_name(err));
}

bool NetworkStore::setApNetwork(uint32_t net) {
  const uint8_t* b = (const uint8_t*)&net;
  // Private (RFC1918) /24 only.
  bool priv = (b[0] == 10) || (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 192 && b[1] == 168);
  if (!priv) return false;
  ((uint8_t*)&net)[3] = 0;
  _apNet = net;
  save();
  return true;
}

int NetworkStore::indexOf(const std::string& ssid) const {
  for (int i = 0; i < _count; i++) {
    if (_items[i].ssid == ssid) return i;
  }
  return -1;
}

bool NetworkStore::put(const std::string& ssid, const std::string& pass) {
  if (ssid.empty() || ssid.size() > 32) return false;
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

bool NetworkStore::remove(const std::string& ssid) {
  int i = indexOf(ssid);
  if (i < 0) return false;
  for (int j = i; j < _count - 1; j++) _items[j] = _items[j + 1];
  _count--;
  _items[_count] = SavedNetwork{};
  save();
  return true;
}

int NetworkStore::overrideIndexOf(const std::string& ssid) const {
  for (int i = 0; i < _ovCount; i++) {
    if (_ov[i].ssid == ssid) return i;
  }
  return -1;
}

bool NetworkStore::overrideEnabled(const std::string& ssid) const {
  int i = overrideIndexOf(ssid);
  return i >= 0 && _ov[i].enabled;
}

bool NetworkStore::anyOverrideEnabled() const {
  for (int i = 0; i < _ovCount; i++) {
    if (_ov[i].enabled) return true;
  }
  return false;
}

bool NetworkStore::putOverride(const std::string& ssid, bool enabled) {
  if (ssid.empty() || ssid.size() > 32) return false;
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

bool NetworkStore::removeOverride(const std::string& ssid) {
  int i = overrideIndexOf(ssid);
  if (i < 0) return false;
  for (int j = i; j < _ovCount - 1; j++) _ov[j] = _ov[j + 1];
  _ovCount--;
  _ov[_ovCount] = Override{};
  save();
  return true;
}

// Accepts aa:bb:cc:dd:ee:ff or aabbccddeeff, any case; stores the colon form.
bool NetworkStore::setHost(const std::string& mac, const std::string& name) {
  std::string hex;
  for (char c : mac) {
    if (isxdigit((unsigned char)c)) hex += tolower(c);
    else if (c != ':' && c != '-' && c != '.' && c != ' ') return false;
  }
  if (hex.size() != 12) return false;
  std::string norm;
  for (size_t i = 0; i < 12; i += 2) {
    if (i) norm += ':';
    norm += hex.substr(i, 2);
  }
  _hostMac = norm;
  _hostName = name.empty() ? norm : name.substr(0, 24);
  save();
  return true;
}

void NetworkStore::clearHost() {
  _hostMac.clear();
  _hostName.clear();
  save();
}
