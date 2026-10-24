// LittleFS ядра: в симуляции пуст — записей старой прошивки нет.
#pragma once

#include <stdint.h>

#define FILE_O_READ 0

namespace Adafruit_LittleFS_Namespace {
class InternalFileSystemHost {
 public:
  bool begin() { return true; }
};
class File {
 public:
  explicit File(InternalFileSystemHost&) {}
  bool open(const char*, int) { return false; }
  int read(void*, uint16_t) { return -1; }
  void close() {}
};
}  // namespace Adafruit_LittleFS_Namespace

extern Adafruit_LittleFS_Namespace::InternalFileSystemHost InternalFS;
