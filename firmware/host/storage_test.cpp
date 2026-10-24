// Журнал во флеше (firmware/src/keychain/storage.cpp) на ПК: запись, чтение, переход по
// страницам и обрыв питания в любой момент записи и стирания.
#include <stdio.h>

#include <random>

#include "storage.h"
#include "storage_host.h"

namespace {
alignas(4096) uint8_t flash[3 * 4096];
uint32_t fake_ms = 0;
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

struct Rec {
  uint32_t n;
  uint8_t body[288];
};

Rec make(uint32_t n) {
  Rec r;
  r.n = n;
  for (size_t i = 0; i < sizeof r.body; ++i) r.body[i] = (uint8_t)(n * 31 + i);
  return r;
}
}  // namespace

long host_cut_after_words = -1;
long host_cut_on_erase = -1;
HostNvmc host_nvmc;
uintptr_t host_flash_base() { return (uintptr_t)flash; }
uint32_t millis() { return fake_ms; }

namespace {
// Операция SoftDevice: подана — и идёт, конец — через время.
struct Op {
  int kind = 0;  // 1 — запись, 2 — стирание
  uint32_t* dst = nullptr;
  const uint32_t* src = nullptr;
  uint32_t words = 0;
  uint8_t* page = nullptr;
  uint32_t done_ms = 0;
} op;
uint32_t busy_answers = 0;

void finish() {
  const Op o = op;
  op = Op{};
  if (o.kind == 1) {
    for (uint32_t i = 0; i < o.words; ++i) {
      if (host_cut_after_words == 0) throw HostPowerCut{};
      if (host_cut_after_words > 0) --host_cut_after_words;
      o.dst[i] &= o.src[i];  // флеш умеет только сбрасывать биты
    }
  } else if (o.kind == 2) {
    if (host_cut_on_erase == 0) {
      memset(o.page, 0xFF, 1000);  // стирание оборвалось на середине: часть страницы — мусор
      o.page[2000] = 0x12;
      throw HostPowerCut{};
    }
    if (host_cut_on_erase > 0) --host_cut_on_erase;
    memset(o.page, 0xFF, 4096);
  }
}
}  // namespace

void delay(uint32_t ms) {
  for (uint32_t i = 0; i < ms; ++i) {
    ++fake_ms;
    if (op.kind && fake_ms >= op.done_ms) finish();
  }
}

uint32_t sd_flash_write(uint32_t* dst, const uint32_t* src, uint32_t words) {
  if (op.kind) return ++busy_answers, NRF_ERROR_BUSY;
  op = Op{1, dst, src, words, nullptr, fake_ms + 1 + words / 25};
  return 0;
}

uint32_t sd_flash_page_erase(uint32_t page) {
  if (op.kind) return ++busy_answers, NRF_ERROR_BUSY;
  // Номер страницы 32-битный, как у SoftDevice: старшие биты адреса на ПК — от нашего массива.
  const uint64_t full = (((uint64_t)host_flash_base() / 4096u) & ~(uint64_t)0xFFFFFFFFu) | page;
  op = Op{2, nullptr, nullptr, 0, (uint8_t*)(uintptr_t)(full * 4096u), fake_ms + 85};
  return 0;
}

int main() {
  memset(flash, 0xFF, sizeof flash);
  storage::begin();
  Rec r;
  CHECK(!storage::load(&r, sizeof r), "пустой журнал — записи нет");

  // Много сохранений: каждый раз читается последнее, стираний мало.
  for (uint32_t i = 1; i <= 100; ++i) {
    const Rec w = make(i);
    CHECK(storage::save(&w, sizeof w), "запись %u", i);
    storage::begin();  // как после перезагрузки
    Rec got;
    CHECK(storage::load(&got, sizeof got) && memcmp(&got, &w, sizeof w) == 0, "после перезагрузки — запись %u", i);
  }
  CHECK(storage::erases() <= 100 / 13 + 2, "стираний на 100 записей: %u", storage::erases());

  // Обрыв питания посреди записи: читается последняя целая.
  std::mt19937 rng(1);
  uint32_t last_ok = 100;
  for (int round = 0; round < 3000; ++round) {
    const uint32_t n = last_ok + 1;
    const Rec w = make(n);
    // Заполнить страницу, чтобы следующая запись стирала (иначе стирания редки).
    const bool cut_erase = rng() % 8 == 0;
    host_cut_after_words = cut_erase ? -1 : (long)(rng() % 80);
    host_cut_on_erase = cut_erase ? 0 : -1;
    bool saved = false;
    try {
      saved = storage::save(&w, sizeof w);
    } catch (HostPowerCut&) {
    }
    op = Op{};  // питание пропало: операция SoftDevice — тоже
    host_cut_after_words = host_cut_on_erase = -1;
    storage::begin();
    Rec got;
    const bool ok = storage::load(&got, sizeof got);
    CHECK(ok, "после обрыва (раунд %d) запись есть", round);
    if (!ok) break;
    CHECK(got.n == n || got.n == last_ok, "после обрыва — последняя целая: %u (ждали %u или %u)", got.n, n, last_ok);
    const Rec want = make(got.n);
    CHECK(memcmp(&got, &want, sizeof got) == 0, "запись %u целая", got.n);
    if (saved) CHECK(got.n == n, "удавшаяся запись читается");
    last_ok = got.n;
    // После обрыва следующая запись проходит.
    const Rec w2 = make(last_ok + 1);
    CHECK(storage::save(&w2, sizeof w2), "после обрыва запись снова проходит (раунд %d)", round);
    storage::begin();
    CHECK(storage::load(&got, sizeof got) && got.n == last_ok + 1, "и читается");
    last_ok = got.n;
  }
  printf("%s: %d проверок, %d ошибок; стираний %u\n", failures ? "FAIL" : "OK", checks, failures, storage::erases());
  return failures ? 1 : 0;
}
