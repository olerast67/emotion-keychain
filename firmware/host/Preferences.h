// Заглушка Preferences (NVS ESP32): ключи живут в памяти, переживают повторный setup().
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

class Preferences {
 public:
  bool begin(const char* ns, bool read_only = false) {
    (void)read_only;
    ns_ = ns;
    return true;
  }
  size_t putBytes(const char* key, const void* data, size_t n) {
    auto& v = store()[ns_ + "/" + key];
    v.assign((const uint8_t*)data, (const uint8_t*)data + n);
    ++writes;
    return n;
  }
  // Как на ESP32: не влезает в буфер — 0 и ничего не копируется.
  size_t getBytes(const char* key, void* out, size_t n) {
    auto it = store().find(ns_ + "/" + key);
    if (it == store().end() || it->second.size() > n) return 0;
    memcpy(out, it->second.data(), it->second.size());
    return it->second.size();
  }
  size_t getBytesLength(const char* key) {
    auto it = store().find(ns_ + "/" + key);
    return it == store().end() ? 0 : it->second.size();
  }
  size_t putUChar(const char* key, uint8_t v) { return putBytes(key, &v, 1); }
  uint8_t getUChar(const char* key, uint8_t def = 0) {
    uint8_t v = def;
    return getBytes(key, &v, 1) == 1 ? v : def;
  }

  static std::map<std::string, std::vector<uint8_t>>& store() {
    static std::map<std::string, std::vector<uint8_t>> s;
    return s;
  }
  static inline int writes = 0;

 private:
  std::string ns_;
};
