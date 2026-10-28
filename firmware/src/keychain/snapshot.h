// Всё, что переживает выключение, — одной записью журнала (storage.h): рост, знакомые и
// настройки. Заголовок хранит длины частей: прошивка, у которой Progress, Friends или
// Settings выросли, прочтёт старую запись по частям (у Progress — своя миграция по версии),
// и рост не пропадёт. Разбор — здесь, а не в main.cpp: его проверяет тест на ПК
// (firmware/host/snapshot_test.cpp).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "face/friends.h"
#include "face/progress.h"

// Настройки, которые переживают перезагрузку.
struct Settings {
  uint32_t magic = 0x4B435354u;  // «KCST»
  uint8_t version = 1;
  uint8_t theme = 0;
  uint8_t sound = 1;
  uint8_t duel_ready = 1;
  uint8_t ghost = 0;
  uint8_t contrast = 0x7F;
  uint8_t bday_m = 0, bday_d = 0;
  uint16_t year = 0;  // 0 — дата не задана
  uint8_t month = 0, day = 0;
  uint16_t clock_min = 12 * 60;
  uint8_t clock_known = 0;
  uint8_t empty_off = 0;  // выключились на пустой ячейке: кнопка без USB не будит целиком
  uint16_t day_min = 0;   // без часов: минут работы с начала текущих суток
  uint8_t reserved[2] = {};  // до 24 байт без выравнивания: memcmp не видит мусора
};
static_assert(sizeof(Settings) == 24, "настройки без скрытых байтов выравнивания");

// Своя ли это запись настроек: чужая версия — заводские.
bool settings_ok(const Settings& s);

struct SnapHead {
  uint32_t magic = 0x4B43534Eu;  // «KCSN»
  uint16_t p_len = sizeof(face::Progress), f_len = sizeof(face::Friends), s_len = sizeof(Settings), reserved = 0;
};
struct Snapshot {
  SnapHead head;
  face::Progress progress;
  face::Friends friends;
  Settings settings;
};

// Что удалось взять из записи. found — запись разобрана на части (с заголовком или старая без
// него); части, которые не прочитались, не тронуты.
struct SnapLoaded {
  bool found = false, progress = false, friends = false, settings = false;
};

// Разобрать запись журнала длины n (storage::load_any). Настройки короче нынешних — новые
// поля остаются заводскими, длиннее — лишнее отбрасывается. Progress и Friends другой длины
// (записанные новой прошивкой) не читаются: их load проверяет размер.
SnapLoaded snapshot_load(const uint8_t* raw, int n, face::Progress* progress, face::Friends* friends, Settings* cfg);
