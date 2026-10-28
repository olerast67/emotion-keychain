// Радио стенда: реклама своего пакета и прослушивание эфира по расписанию (docs/encounters.md).
// То же, что firmware/src/keychain/radio.h у платы rev B, на Bluetooth ядра Arduino-ESP32 3.3:
// NimBLE на ESP32-C3, C5 и S3, Bluedroid на обычной ESP32. Соединений нет: только рекламные
// пакеты без подключения, пакет — тот же, что у брелока (face/encounter.h).
#pragma once

#include <stdint.h>

#include "face/encounter.h"

namespace radio {

// Включить Bluetooth. false — не вышло (нет памяти, стек выключен в сборке): стенд работает
// дальше без встреч, остальные функции тогда ничего не делают.
bool begin();
bool ok();
// Какой стек: «NimBLE», «Bluedroid» или «нет».
const char* stack();
// Пакет для рассылки; меняется — уходит в эфир при следующем update.
void set_advert(const face::Advert& a);
// Режимы: обычный, «рядом кто-то есть или ждём общую сцену», ночной и выключенный.
enum class Pace : uint8_t { Normal, Busy, Night, Off };
void set_pace(Pace p);
Pace pace();
// Разослать изменения и включить или кончить окно прослушивания, когда подошло время.
void update(uint32_t now_ms);
// Пакет из эфира, услышанный с прошлого вызова.
bool take(face::Advert* a, int* rssi);
// Невидимка: только слушать, ничего не рассылать.
void set_ghost(bool on);
bool ghost();
// Сколько пакетов брелоков услышано и сколько потеряно на полной очереди — для команды radio.
uint32_t heard();
uint32_t dropped();
// Свободная куча, байт: сколько осталось после стека Bluetooth (у ESP32-C3 памяти меньше всех).
uint32_t heap_free();

}  // namespace radio
