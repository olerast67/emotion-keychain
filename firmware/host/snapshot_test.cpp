// Запись журнала брелока (firmware/src/keychain/snapshot.cpp) на ПК: рост, знакомые и
// настройки переживают смену формата записи — запись прошивки без заголовка, настройки
// другой длины, Progress версии 1 через заголовок; запись более новой прошивки не читается,
// но и не ломает разбор.
#include <stdio.h>
#include <string.h>

#include <vector>

#include "snapshot.h"

using namespace face;

namespace {
int failures = 0, checks = 0;

#define CHECK(c, ...)                                  \
  do {                                                 \
    ++checks;                                          \
    if (!(c)) {                                        \
      ++failures;                                      \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);      \
      printf(__VA_ARGS__);                             \
      printf("\n");                                    \
    }                                                  \
  } while (0)

// Рост и знакомые «старого брелока» с настройками.
struct Sample {
  Progress p;
  Friends f;
  Settings s;
  Sample() {
    p.reset();
    p.xp = 4321, p.days = 12, p.steps = 98765;
    p.seal();
    f.reset();
    f.meet(0x12345678u, 3);
    f.seal();
    s.theme = 2, s.sound = 0, s.contrast = 0x40, s.clock_min = 600, s.day_min = 77;
  }
};

void put(std::vector<uint8_t>& v, const void* d, size_t n) {
  const uint8_t* p = (const uint8_t*)d;
  v.insert(v.end(), p, p + n);
}

std::vector<uint8_t> with_head(const SnapHead& h, const void* p, const void* f, const void* s) {
  std::vector<uint8_t> v;
  put(v, &h, sizeof h);
  put(v, p, h.p_len);
  put(v, f, h.f_len);
  put(v, s, h.s_len);
  return v;
}

struct Got {
  Progress p;
  Friends f;
  Settings s;
  SnapLoaded r;
};

Got load(const std::vector<uint8_t>& v) {
  Got g;
  g.p.reset();
  g.f.reset();
  g.r = snapshot_load(v.data(), (int)v.size(), &g.p, &g.f, &g.s);
  return g;
}

uint32_t crc32_ref(const uint8_t* p, uint32_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

}  // namespace

int main() {
  const Sample a;

  // Нынешняя запись: заголовок и три части.
  {
    Snapshot snap;
    snap.progress = a.p, snap.friends = a.f, snap.settings = a.s;
    std::vector<uint8_t> v;
    put(v, &snap, sizeof snap);
    const Got g = load(v);
    CHECK(g.r.found && g.r.progress && g.r.friends && g.r.settings, "нынешняя запись читается целиком");
    CHECK(g.p.xp == 4321 && g.f.level(0x12345678u) >= 0 && g.s.theme == 2 && g.s.day_min == 77,
          "рост, знакомый и настройки на месте");
  }

  // Запись прошивки до заголовка: Progress, Friends и Settings подряд, 292 байта.
  {
    std::vector<uint8_t> v;
    put(v, &a.p, sizeof a.p);
    put(v, &a.f, sizeof a.f);
    put(v, &a.s, sizeof a.s);
    CHECK(v.size() == 292, "старая запись — %zu байт", v.size());
    const Got g = load(v);
    CHECK(g.r.found && g.r.progress && g.r.friends && g.r.settings, "запись без заголовка читается");
    CHECK(g.p.xp == 4321 && g.p.steps == 98765 && g.f.level(0x12345678u) >= 0 && g.s.theme == 2 && g.s.sound == 0,
          "из записи без заголовка — опыт %u, знакомый и тема %u", (unsigned)g.p.xp, g.s.theme);
  }

  // Настройки короче нынешних (16 байт: до даты) — дальше поля заводские; длиннее (32) —
  // лишнее отбрасывается.
  {
    SnapHead h;
    h.s_len = 16;
    const Got g = load(with_head(h, &a.p, &a.f, &a.s));
    CHECK(g.r.settings && g.s.theme == 2 && g.s.contrast == 0x40, "короткие настройки: тема и яркость на месте");
    CHECK(g.s.clock_min == Settings{}.clock_min && g.s.day_min == 0, "короткие настройки: новые поля заводские");
    CHECK(g.r.progress && g.r.friends && g.p.xp == 4321, "рост за короткими настройками на месте");
    uint8_t longer[32];
    memset(longer, 0xEE, sizeof longer);
    memcpy(longer, &a.s, sizeof a.s);
    SnapHead h2;
    h2.s_len = 32;
    const Got g2 = load(with_head(h2, &a.p, &a.f, longer));
    CHECK(g2.r.settings && g2.s.theme == 2 && g2.s.day_min == 77 && g2.s.reserved[0] == 0,
          "длинные настройки: свои поля на месте, лишнее отброшено");
  }

  // Progress версии 1 (56 байт) за заголовком: миграция по версии.
  {
    uint8_t v1[56] = {};
    auto put16 = [&](int o, uint32_t v) { v1[o] = (uint8_t)v, v1[o + 1] = (uint8_t)(v >> 8); };
    auto put32 = [&](int o, uint32_t v) { put16(o, v & 0xFFFF), put16(o + 2, v >> 16); };
    put32(0, Progress::kMagic), put16(4, 1), put16(6, 9), put32(8, 555), put32(12, 10), put32(16, 2000);
    put32(52, crc32_ref(v1, 52));
    SnapHead h;
    h.p_len = sizeof v1;
    const Got g = load(with_head(h, v1, &a.f, &a.s));
    CHECK(g.r.progress && g.p.xp == 555 && g.p.days == 9 && g.p.version == Progress::kVersion,
          "Progress версии 1 через заголовок переносится (опыт %u)", (unsigned)g.p.xp);
    CHECK(g.r.friends && g.r.settings && g.s.theme == 2, "знакомые и настройки за ним на месте");
  }

  // Запись более новой прошивки: Progress и Friends длиннее. Их не читаем (рост — заново),
  // настройки за ними — читаем; разбор не выходит за запись.
  {
    std::vector<uint8_t> p(sizeof(Progress) + 8, 0xAB), f(sizeof(Friends) + 12, 0xCD);
    memcpy(p.data(), &a.p, sizeof a.p);
    memcpy(f.data(), &a.f, sizeof a.f);
    SnapHead h;
    h.p_len = (uint16_t)p.size(), h.f_len = (uint16_t)f.size();
    const Got g = load(with_head(h, p.data(), f.data(), &a.s));
    CHECK(g.r.found && !g.r.progress && !g.r.friends && g.p.xp == 0 && g.f.count() == 0,
          "части новой прошивки не читаются, рост и знакомые не тронуты");
    CHECK(g.r.settings && g.s.theme == 2, "настройки за ними читаются");
  }

  // Испорченный заголовок: длины больше самой записи — не разбирается вовсе.
  {
    SnapHead h;
    std::vector<uint8_t> v = with_head(h, &a.p, &a.f, &a.s);
    SnapHead bad = h;
    bad.f_len = 400;
    memcpy(v.data(), &bad, sizeof bad);
    const Got g = load(v);
    CHECK(!g.r.found && !g.r.progress && !g.r.settings, "длины частей больше записи — запись не разбирается");
    std::vector<uint8_t> tiny(5, 0x4E);
    CHECK(!load(tiny).r.found, "обрывок записи не разбирается");
    CHECK(!snapshot_load(nullptr, -1, nullptr, nullptr, nullptr).found, "записи нет — разбирать нечего");
  }

  // Чужая версия настроек — заводские, рост при этом читается.
  {
    Settings other = a.s;
    other.version = 7;
    SnapHead h;
    const Got g = load(with_head(h, &a.p, &a.f, &other));
    CHECK(g.r.progress && !g.r.settings && g.s.theme == Settings{}.theme, "настройки чужой версии — заводские");
  }

  printf("%d проверок, %d ошибок\n", checks, failures);
  return failures ? 1 : 0;
}
