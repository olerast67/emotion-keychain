#include "radio.h"

#include <bluefruit.h>

#include "face/clock.h"

namespace radio {

namespace {

face::Advert adv;
bool adv_dirty = true;
bool adv_on = false;
bool ghost_on = false;
Pace pace = Pace::Normal;
uint32_t next_scan = 0;
uint32_t next_adv_refresh = 0;

// Очередь услышанных пакетов: обработчик Bluefruit пишет, основной цикл читает.
struct Heard {
  face::Advert a;
  int8_t rssi;
};
constexpr int kQueue = 8;
Heard queue[kQueue];
volatile uint8_t q_head = 0, q_tail = 0;

constexpr int8_t kRssiFloor = -90;

// Пакет этого брелока уже ждёт в очереди: поток повторов одного передатчика не вытеснит
// пакеты остальных. Читается из обработчика, единственного писателя очереди.
bool queued(uint32_t hash) {
  for (uint8_t i = q_tail; i != q_head; i = (uint8_t)((i + 1) % kQueue))
    if (queue[i].a.hash == hash) return true;
  return false;
}

void on_report(ble_gap_evt_adv_report_t* r) {
  uint8_t buf[32];
  const uint8_t n = Bluefruit.Scanner.parseReportByType(r, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, buf, sizeof buf);
  face::Advert a;
  // Совсем дальние пакеты не нужны ни для встречи, ни для отсчёта: не тратим на них очередь.
  if (r->rssi >= kRssiFloor && n && face::adv_decode(buf, n, &a) && !queued(a.hash)) {
    const uint8_t next = (uint8_t)((q_head + 1) % kQueue);
    if (next != q_tail) {  // полна — пакет теряется, придёт следующий
      queue[q_head].a = a;
      queue[q_head].rssi = r->rssi;
      __DMB();  // пакет записан раньше, чем его увидит основной цикл
      q_head = next;
    }
  }
  Bluefruit.Scanner.resume();  // без этого прослушивание останавливается после первого пакета
}

// Интервал рекламы в единицах 0.625 мс.
uint16_t adv_interval() {
  switch (pace) {
    case Pace::Busy: return 160;     // 100 мс: отсчёт до общей сцены доходит быстро
    case Pace::Night: return 16000;  // 10 с
    default: return 1600;            // 1 с: передавать дешевле, чем слушать
  }
}

void push_advert() {
  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  adv_on = false;
  if (ghost_on || pace == Pace::Off) return;
  uint8_t data[face::kAdvLen];
  face::adv_encode(adv, data);
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED);
  Bluefruit.Advertising.addManufacturerData(data, sizeof data);
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);
  const uint16_t iv = adv_interval();
  Bluefruit.Advertising.setInterval(iv, iv);
  Bluefruit.Advertising.restartOnDisconnect(false);
  adv_on = Bluefruit.Advertising.start(0);
}

}  // namespace

void begin() {
  Bluefruit.autoConnLed(false);
  Bluefruit.configPrphBandwidth(BANDWIDTH_LOW);
  Bluefruit.begin(1, 1);
  Bluefruit.setTxPower(0);
  Bluefruit.setName("Keychain");
  // Без «быстрого» режима: иначе через 30 с задача BLE сама перезапускала бы рекламу
  // одновременно с основным циклом.
  Bluefruit.Advertising.setFastTimeout(0);
  Bluefruit.Scanner.setRxCallback(on_report);
  Bluefruit.Scanner.restartOnDisconnect(false);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.setInterval(240, 240);  // окно = интервал: слушаем без пропусков, пока включено
}

void set_advert(const face::Advert& a) {
  if (a.hash == adv.hash && a.level == adv.level && a.flags == adv.flags && a.mood == adv.mood &&
      a.countdown == adv.countdown && a.to == adv.to)
    return;
  // Настроение меняется часто: без отсчёта такие пакеты обновляются не чаще раза в 30 с.
  const bool only_mood = a.hash == adv.hash && a.level == adv.level && a.flags == adv.flags &&
                         a.countdown == adv.countdown && a.to == adv.to;
  adv = a;
  if (!only_mood) adv_dirty = true;
}

void set_pace(Pace p) {
  if (p == pace) return;
  pace = p;
  adv_dirty = true;
  next_scan = 0;
}

void set_ghost(bool on) {
  ghost_on = on;
  adv_dirty = true;
}

bool ghost() { return ghost_on; }

void update(uint32_t now) {
  if (adv_dirty || face::time_reached(now, next_adv_refresh)) {
    adv_dirty = false;
    next_adv_refresh = now + 30000;
    push_advert();
  }
  if (pace == Pace::Off || pace == Pace::Night) {
    if (Bluefruit.Scanner.isRunning()) Bluefruit.Scanner.stop();
    return;
  }
  // Слушаем окнами: 150 мс раз в 30 с; когда рядом кто-то есть — 300 мс раз в 1.5 с.
  // Реклама раз в 1 с и окно раз в 30 с находят соседа так же быстро, как 2 с и 15 с,
  // а тока берут меньше: приём дороже передачи.
  if (face::time_reached(now, next_scan) && !Bluefruit.Scanner.isRunning()) {
    const bool busy = pace == Pace::Busy;
    Bluefruit.Scanner.start(busy ? 30 : 15);  // длительность в 10 мс
    next_scan = now + (busy ? 1500 : 30000);
  }
}

bool take(face::Advert* a, int* rssi) {
  if (q_tail == q_head) return false;
  __DMB();
  *a = queue[q_tail].a;
  *rssi = queue[q_tail].rssi;
  q_tail = (uint8_t)((q_tail + 1) % kQueue);
  return true;
}

}  // namespace radio
