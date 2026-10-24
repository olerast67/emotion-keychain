#include "storage.h"

#include <InternalFileSystem.h>
#include <stdio.h>

using namespace Adafruit_LittleFS_Namespace;

namespace storage {

bool begin() { return InternalFS.begin(); }

int load(const char* name, void* buf, size_t cap) {
  char path[32];
  snprintf(path, sizeof path, "/%s", name);
  File f(InternalFS);
  if (!f.open(path, FILE_O_READ)) {
    // Питание пропало между удалением старой копии и переименованием новой: берём новую.
    char tmp[36];
    snprintf(tmp, sizeof tmp, "/%s.new", name);
    if (!f.open(tmp, FILE_O_READ)) return -1;
  }
  const int n = f.read(buf, (uint16_t)cap);
  f.close();
  return n;
}

bool save(const char* name, const void* data, size_t n) {
  char path[32], tmp[36];
  snprintf(path, sizeof path, "/%s", name);
  snprintf(tmp, sizeof tmp, "/%s.new", name);
  InternalFS.remove(tmp);
  File f(InternalFS);
  if (!f.open(tmp, FILE_O_WRITE)) return false;
  const size_t w = f.write((const uint8_t*)data, n);
  f.close();
  if (w != n) {
    InternalFS.remove(tmp);
    return false;
  }
  InternalFS.remove(path);
  return InternalFS.rename(tmp, path);
}

void wipe() { InternalFS.format(); }

}  // namespace storage
