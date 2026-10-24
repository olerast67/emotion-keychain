// Радио: реклама своего пакета и прослушивание эфира по расписанию (docs/encounters.md).
// Соединений нет: только рекламные пакеты без подключения.
#pragma once

#include <stdint.h>

#include "face/encounter.h"

namespace radio {

void begin();
// Пакет для рассылки; меняется — уходит в эфир при следующем update.
void set_advert(const face::Advert& a);
// Режимы: обычный, «рядом кто-то есть или ждём общую сцену» и ночной.
enum class Pace : uint8_t { Normal, Busy, Night, Off };
void set_pace(Pace p);
// Разослать изменения и включить прослушивание, когда подошло его время.
void update(uint32_t now_ms);
// Пакет из эфира, услышанный с прошлого вызова.
bool take(face::Advert* a, int* rssi);
// Невидимка: только слушать, ничего не рассылать.
void set_ghost(bool on);
bool ghost();

}  // namespace radio
