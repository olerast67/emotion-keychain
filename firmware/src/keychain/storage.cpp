#include "storage.h"

#include <stdio.h>
#include <string.h>

#ifdef STORAGE_HOST_TEST
#include "storage_host.h"  // проверка журнала на ПК: флеш в памяти и обрывы питания
#else
#include <Arduino.h>
#include <InternalFileSystem.h>
#include <nrf_sdm.h>
#include <nrf_soc.h>
using namespace Adafruit_LittleFS_Namespace;
#endif

namespace storage {

namespace {

#ifdef STORAGE_HOST_TEST
#define kBase host_flash_base()
#else
constexpr uintptr_t kBase = 0xEA000;  // три страницы до LittleFS (0xED000)
#endif
constexpr uint32_t kPageSize = 4096;
constexpr int kPages = 3;
constexpr uint32_t kMagic = 0x4B434C47u;  // «KCLG»

struct Header {
  uint32_t magic;
  uint32_t seq;
  uint16_t len;
  uint16_t reserved;
  uint32_t crc;
};
static_assert(sizeof(Header) == 16, "заголовок записи — 4 слова");

int cur_page = -1;      // страница, куда дописываем; −1 — журнал пуст
uint32_t cur_off = 0;   // свободное место в ней
uint32_t last_seq = 0;
uintptr_t last_addr = 0;  // адрес последней целой записи, 0 — нет
uint32_t erase_count = 0;

uintptr_t page_addr(int p) { return kBase + (uint32_t)p * kPageSize; }
uint32_t align4(uint32_t n) { return (n + 3u) & ~3u; }

uint32_t crc32(const uint8_t* p, uint32_t n, uint32_t c = 0xFFFFFFFFu) {
  for (uint32_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return c;
}

uint32_t record_crc(const Header& h, const uint8_t* payload) {
  const uint32_t c = crc32((const uint8_t*)&h.seq, 8);  // номер и длина тоже под суммой
  return ~crc32(payload, h.len, c);
}

bool erased(uintptr_t addr, uint32_t n) {
  const uint32_t* w = (const uint32_t*)addr;
  for (uint32_t i = 0; i < n / 4; ++i)
    if (w[i] != 0xFFFFFFFFu) return false;
  return true;
}

bool sd_on() {
  uint8_t en = 0;
  sd_softdevice_is_enabled(&en);
  return en != 0;
}

// Операции флеша при работающем SoftDevice идут в его окнах между радиособытиями, и о
// конце он сообщает событием. Его ловит обработчик LittleFS, поэтому здесь конец
// узнаётся по самой памяти: записанное читается, стёртое — одни единицы. Не дождались —
// операция сорвалась, запись считается несостоявшейся. Так узнать конец можно, только
// если память после операции другая, чем до неё: чистую страницу поэтому не стираем, а
// записи всегда начинаются с метки. И на всякий случай «занят» ждём долго — пока
// SoftDevice не закончит прошлую операцию.
void feed_watchdog() {
#ifndef STORAGE_HOST_TEST
  NRF_WDT->RR[0] = WDT_RR_RR_Reload;  // запись может идти секунды: сторожевой таймер — 8 с
#endif
}

bool wait_for(bool (*done)(uintptr_t, const void*, uint32_t), uintptr_t addr, const void* src, uint32_t n,
              uint32_t timeout_ms) {
  const uint32_t t0 = millis();
  while (!done(addr, src, n)) {
    feed_watchdog();
    if (millis() - t0 > timeout_ms) return false;
    delay(1);  // уступить задачам SoftDevice
  }
  return true;
}

bool same(uintptr_t addr, const void* src, uint32_t n) { return memcmp((const void*)addr, src, n) == 0; }
bool blank(uintptr_t addr, const void*, uint32_t n) { return erased(addr, n); }

void nvmc_wait() {
  while (!NRF_NVMC->READY) {
  }
}

// Подать операцию: SoftDevice отвечает «занят», пока не закончил прошлую.
template <typename F>
bool submit(F op) {
  for (int i = 0; i < 100; ++i) {
    const uint32_t err = op();
    if (err == NRF_SUCCESS) return true;
    if (err != NRF_ERROR_BUSY) return false;
    feed_watchdog();
    delay(5);
  }
  return false;
}

bool erase_page(int p) {
  const uintptr_t a = page_addr(p);
  if (erased(a, kPageSize)) return true;  // уже чистая: стирать нечего (и конец не узнать)
  ++erase_count;
  if (!sd_on()) {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
    nvmc_wait();
    NRF_NVMC->ERASEPAGE = a;
    nvmc_wait();
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    nvmc_wait();
    return erased(a, kPageSize);
  }
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (submit([&] { return sd_flash_page_erase((uint32_t)(a / kPageSize)); }) &&
        wait_for(blank, a, nullptr, kPageSize, 800))
      return true;
  }
  return false;
}

bool program(uintptr_t dst, const void* src, uint32_t n) {
  if (!sd_on()) {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    nvmc_wait();
    const uint32_t* s = (const uint32_t*)src;
    volatile uint32_t* d = (volatile uint32_t*)dst;
    for (uint32_t i = 0; i < n / 4; ++i) {
      d[i] = s[i];
      nvmc_wait();
    }
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    nvmc_wait();
    return same(dst, src, n);
  }
  // Повтор только в чистое место: сорвавшаяся запись могла записать часть слов.
  for (int attempt = 0; attempt < 3 && erased(dst, n); ++attempt) {
    if (submit([&] { return sd_flash_write((uint32_t*)dst, (const uint32_t*)src, n / 4); }) &&
        wait_for(same, dst, src, n, 400))
      return true;
  }
  return false;
}

}  // namespace

void begin() {
  cur_page = -1, cur_off = 0, last_seq = 0, last_addr = 0;
  for (int p = 0; p < kPages; ++p) {
    uint32_t off = 0;
    while (off + sizeof(Header) <= kPageSize) {
      const Header* h = (const Header*)(page_addr(p) + off);
      if (h->magic != kMagic || !h->len || h->len > kMaxRecord) break;
      const uint32_t total = sizeof(Header) + align4(h->len);
      if (off + total > kPageSize) break;
      if (h->crc != record_crc(*h, (const uint8_t*)(h + 1))) break;  // оборвана: дальше в странице — не наше
      if (!last_addr || h->seq >= last_seq) {
        last_seq = h->seq;
        last_addr = page_addr(p) + off;
        cur_page = p;
        cur_off = off + total;
      }
      off += total;
    }
  }
}

bool load(void* buf, size_t n) {
  if (!last_addr) return false;
  const Header* h = (const Header*)last_addr;
  if (h->len != n) return false;
  memcpy(buf, h + 1, n);
  return true;
}

int load_any(void* buf, size_t cap) {
  if (!last_addr) return -1;
  const Header* h = (const Header*)last_addr;
  memcpy(buf, h + 1, h->len < cap ? h->len : cap);
  return (int)h->len;
}

bool save(const void* data, size_t n) {
  if (!n || n > kMaxRecord) return false;
  static uint8_t rec[sizeof(Header) + kMaxRecord] __attribute__((aligned(4)));
  const uint32_t total = sizeof(Header) + align4((uint32_t)n);
  memset(rec, 0xFF, total);
  Header h{kMagic, last_seq + 1, (uint16_t)n, 0xFFFF, 0};
  memcpy(rec + sizeof h, data, n);
  h.crc = record_crc(h, rec + sizeof h);
  memcpy(rec, &h, sizeof h);
  // Сначала — в текущую страницу; не влезло или сорвалось — в следующую, стерев её.
  // Больше двух стираний за раз нет: страница с последней целой записью не стирается.
  for (int attempt = 0; attempt < kPages; ++attempt) {
    if (cur_page < 0 || cur_off + total > kPageSize || !erased(page_addr(cur_page) + cur_off, total)) {
      const int next = cur_page < 0 ? 0 : (cur_page + 1) % kPages;
      if (last_addr && last_addr >= page_addr(next) && last_addr < page_addr(next) + kPageSize) return false;
      cur_page = next;
      cur_off = kPageSize;  // пока не стёрта — места нет
      if (!erase_page(next)) continue;
      cur_off = 0;
    }
    const uintptr_t a = page_addr(cur_page) + cur_off;
    if (program(a, rec, total)) {
      cur_off += total;
      last_seq = h.seq;
      last_addr = a;
      return true;
    }
    cur_off = kPageSize;  // место испорчено недописанной записью — дальше в следующую
  }
  return false;
}

#ifdef STORAGE_HOST_TEST
int legacy_load(const char*, void*, size_t) { return -1; }
#else
int legacy_load(const char* name, void* buf, size_t cap) {
  if (!InternalFS.begin()) return -1;
  char path[32];
  snprintf(path, sizeof path, "/%s", name);
  File f(InternalFS);
  if (!f.open(path, FILE_O_READ)) {
    char tmp[36];
    snprintf(tmp, sizeof tmp, "/%s.new", name);
    if (!f.open(tmp, FILE_O_READ)) return -1;
  }
  const int n = f.read(buf, (uint16_t)cap);
  f.close();
  return n;
}
#endif

uint32_t erases() { return erase_count; }

}  // namespace storage
