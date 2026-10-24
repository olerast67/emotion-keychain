// Хранение во флеше nRF52840 (LittleFS ядра Adafruit): рост, знакомые, настройки.
// Каждая запись — отдельный файл; пишется целиком через временный, поэтому пропажа
// питания посреди записи не портит прошлую копию.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace storage {

bool begin();
// Прочитать файл целиком: число байт или −1, если файла нет.
int load(const char* name, void* buf, size_t cap);
bool save(const char* name, const void* data, size_t n);
// Стереть всё (команда reset).
void wipe();

}  // namespace storage
