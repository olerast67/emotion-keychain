#include "stand_radio.h"

#include <Arduino.h>
#include <string.h>

#include <atomic>

#include "sdkconfig.h"

// -DSTAND_BLE=0 — стенд без радио: встречи только командами meet, duel и chat.
#ifndef STAND_BLE
#define STAND_BLE 1
#endif

#if STAND_BLE && defined(CONFIG_BT_NIMBLE_ENABLED)
#define RADIO_NIMBLE 1
#elif STAND_BLE && defined(CONFIG_BT_BLUEDROID_ENABLED)
#define RADIO_BLUEDROID 1
#endif

#if RADIO_NIMBLE || RADIO_BLUEDROID
// Без этого ядро при старте отдаёт память контроллера Bluetooth под кучу (initArduino), и
// включить его потом уже нельзя.
#include "esp32-hal-alloc-ble-mem.h"
#endif

#if RADIO_NIMBLE
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#elif RADIO_BLUEDROID
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#endif

namespace radio {

namespace {

face::Advert adv;
bool started = false, up = false;
bool adv_dirty = true;
bool ghost_on = false;
Pace pace_ = Pace::Normal;
// Сроки — как «прошло с прошлого раза»: такое сравнение верно при любом переполнении
// времени (как у брелока, keychain/radio.cpp).
bool scan_now = true;
bool scanning = false;
uint32_t scan_at = 0, scan_len = 0;
uint32_t adv_at = 0;

// Очередь услышанных пакетов: обработчик стека Bluetooth (своя задача FreeRTOS, на S3 и
// обычной ESP32 — часто на другом ядре) пишет, основной цикл читает. Писатель один.
struct Heard {
  face::Advert a;
  int8_t rssi;
};
constexpr int kQueue = 8;
Heard queue[kQueue];
std::atomic<uint8_t> q_head{0}, q_tail{0};
std::atomic<uint32_t> n_heard{0}, n_dropped{0};

constexpr int8_t kRssiFloor = -90;

// Пакет этого брелока уже ждёт в очереди: поток повторов одного передатчика не вытеснит
// пакеты остальных. Читается из обработчика, единственного писателя очереди.
bool queued(uint32_t hash) {
  const uint8_t head = q_head.load(std::memory_order_relaxed);
  for (uint8_t i = q_tail.load(std::memory_order_acquire); i != head; i = (uint8_t)((i + 1) % kQueue))
    if (queue[i].a.hash == hash) return true;
  return false;
}

// Данные сервиса со 128-битным UUID из рекламы (UUID сверяет adv_decode). Свой разбор с
// проверкой длин: пакет пишет кто угодно, длина поля больше остатка или нулевая — конец.
int service_data(const uint8_t* p, int len, uint8_t* out, int cap) {
  int i = 0;
  while (i < len) {
    const int field = p[i];
    if (!field || i + 1 + field > len) return 0;  // конец или испорченная длина
    if (p[i + 1] == face::kAdvType) {
      const int n = field - 1 < cap ? field - 1 : cap;
      memcpy(out, p + i + 2, (size_t)n);
      return n;
    }
    i += 1 + field;
  }
  return 0;
}

// Вызывается из задачи стека Bluetooth на каждый услышанный рекламный пакет.
void on_report(const uint8_t* data, int len, int rssi) {
  if (!data || len <= 0 || len > 31 || rssi < kRssiFloor) return;  // дальние не тратят очередь
  uint8_t buf[32];
  const int n = service_data(data, len, buf, sizeof buf);
  face::Advert a;
  if (!n || !face::adv_decode(buf, n, &a)) return;
  n_heard.fetch_add(1, std::memory_order_relaxed);
  if (queued(a.hash)) return;
  const uint8_t head = q_head.load(std::memory_order_relaxed);
  const uint8_t next = (uint8_t)((head + 1) % kQueue);
  if (next == q_tail.load(std::memory_order_acquire)) {  // полна — пакет теряется, придёт следующий
    n_dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  queue[head].a = a;
  queue[head].rssi = (int8_t)rssi;
  q_head.store(next, std::memory_order_release);  // пакет записан раньше, чем его увидит цикл
}

// Интервал рекламы в единицах 0.625 мс.
uint16_t adv_interval() {
  switch (pace_) {
    case Pace::Busy: return 160;     // 100 мс: отсчёт до общей сцены доходит быстро
    case Pace::Night: return 16000;  // 10 с
    default: return 1600;            // 1 с
  }
}

// ---------------------------------------------------------------- NimBLE (C3, C5, S3)
#if RADIO_NIMBLE

uint8_t own_addr = BLE_OWN_ADDR_PUBLIC;
std::atomic<bool> synced{false};

int gap_event(struct ble_gap_event* e, void*) {
  if (e->type == BLE_GAP_EVENT_DISC) on_report(e->disc.data, e->disc.length_data, e->disc.rssi);
  return 0;
}

void on_sync() {
  ble_hs_util_ensure_addr(0);
  ble_hs_id_infer_auto(0, &own_addr);
  synced = true;
}

void on_reset(int) { synced = false; }

void host_task(void*) {
  nimble_port_run();  // возвращается только после nimble_port_stop
  nimble_port_freertos_deinit();
}

bool stack_begin() {
  if (nimble_port_init() != ESP_OK) return false;
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.reset_cb = on_reset;
  nimble_port_freertos_init(host_task);
  for (int i = 0; i < 200 && !synced; ++i) delay(5);  // синхронизация хоста с контроллером
  return synced;
}

bool ready() { return synced; }

void adv_stop() {
  if (ble_gap_adv_active()) ble_gap_adv_stop();
}

bool adv_start(const uint8_t* data, int len, uint16_t iv) {
  if (ble_gap_adv_set_data(data, len) != 0) return false;
  ble_gap_adv_params p;
  memset(&p, 0, sizeof p);
  p.conn_mode = BLE_GAP_CONN_MODE_NON;  // без соединения
  p.disc_mode = BLE_GAP_DISC_MODE_NON;  // и без ответа на сканирование: ADV_NONCONN_IND
  p.itvl_min = p.itvl_max = iv;
  return ble_gap_adv_start(own_addr, nullptr, BLE_HS_FOREVER, &p, gap_event, nullptr) == 0;
}

bool scan_start() {
  ble_gap_disc_params p;
  memset(&p, 0, sizeof p);
  p.itvl = p.window = 240;  // окно = интервал: слушаем без пропусков, пока включено
  p.passive = 1;
  p.filter_duplicates = 0;  // повторы нужны: встреча — это тот же сосед дважды
  return ble_gap_disc(own_addr, BLE_HS_FOREVER, &p, gap_event, nullptr) == 0;
}

void scan_stop() {
  if (ble_gap_disc_active()) ble_gap_disc_cancel();
}

const char* kStack = "NimBLE";

// ---------------------------------------------------------------- Bluedroid (обычная ESP32)
#elif RADIO_BLUEDROID

esp_ble_adv_params_t adv_params;
std::atomic<bool> adv_want{false}, scan_ready{false};

void gap_event(esp_gap_ble_cb_event_t ev, esp_ble_gap_cb_param_t* p) {
  switch (ev) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
      // Данные легли — запускаем рекламу, если её не выключили, пока данные шли.
      if (adv_want) esp_ble_gap_start_advertising(&adv_params);
      break;
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: scan_ready = true; break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
      if (p->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT)
        on_report(p->scan_rst.ble_adv, p->scan_rst.adv_data_len, p->scan_rst.rssi);
      break;
    default: break;
  }
}

bool stack_begin() {
  if (!btStart()) return false;  // контроллер; память классического Bluetooth ядро уже отдало
  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED && esp_bluedroid_init() != ESP_OK)
    return false;
  if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED && esp_bluedroid_enable() != ESP_OK) return false;
  if (esp_ble_gap_register_callback(gap_event) != ESP_OK) return false;
  esp_ble_scan_params_t sp;
  memset(&sp, 0, sizeof sp);
  sp.scan_type = BLE_SCAN_TYPE_PASSIVE;
  sp.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
  sp.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
  sp.scan_interval = sp.scan_window = 240;  // окно = интервал: слушаем без пропусков, пока включено
  sp.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;  // повторы нужны: встреча — тот же сосед дважды
  if (esp_ble_gap_set_scan_params(&sp) != ESP_OK) return false;
  for (int i = 0; i < 200 && !scan_ready; ++i) delay(5);
  return scan_ready;
}

bool ready() { return scan_ready; }

void adv_stop() {
  adv_want = false;
  esp_ble_gap_stop_advertising();
}

bool adv_start(const uint8_t* data, int len, uint16_t iv) {
  memset(&adv_params, 0, sizeof adv_params);
  adv_params.adv_int_min = adv_params.adv_int_max = iv;
  adv_params.adv_type = ADV_TYPE_NONCONN_IND;  // без соединения и без ответа на сканирование
  adv_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
  adv_params.channel_map = ADV_CHNL_ALL;
  adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
  adv_want = true;
  // Реклама запустится в gap_event, когда стек подтвердит новые данные.
  return esp_ble_gap_config_adv_data_raw(const_cast<uint8_t*>(data), (uint32_t)len) == ESP_OK;
}

bool scan_start() { return esp_ble_gap_start_scanning(0) == ESP_OK; }  // 0 — пока не остановим
void scan_stop() { esp_ble_gap_stop_scanning(); }

const char* kStack = "Bluedroid";

// ---------------------------------------------------------------- без радио
#else

bool stack_begin() { return false; }
bool ready() { return false; }
void adv_stop() {}
bool adv_start(const uint8_t*, int, uint16_t) { return false; }
bool scan_start() { return false; }
void scan_stop() {}
const char* kStack = "нет";

#endif

void push_advert() {
  adv_stop();
  if (ghost_on || pace_ == Pace::Off) return;
  // Без флагов: реклама без соединения, а поле с UUID занимает 29 байт из 31.
  uint8_t data[2 + face::kAdvLen];
  data[0] = (uint8_t)(1 + face::kAdvLen);
  data[1] = face::kAdvType;
  face::adv_encode(adv, data + 2);
  adv_start(data, sizeof data, adv_interval());
}

}  // namespace

bool begin() {
  if (started) return up;
  started = true;
  up = stack_begin();
  return up;
}

bool ok() { return up; }

const char* stack() { return kStack; }

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
  if (p == pace_) return;
  pace_ = p;
  adv_dirty = true;
  scan_now = true;
}

Pace pace() { return pace_; }

void set_ghost(bool on) {
  ghost_on = on;
  adv_dirty = true;
}

bool ghost() { return ghost_on; }

uint32_t heard() { return n_heard.load(std::memory_order_relaxed); }
uint32_t dropped() { return n_dropped.load(std::memory_order_relaxed); }
uint32_t heap_free() { return (uint32_t)esp_get_free_heap_size(); }

void update(uint32_t now) {
  if (!up || !ready()) return;
  if (adv_dirty || now - adv_at >= 30000u) {
    adv_dirty = false;
    adv_at = now;
    push_advert();
  }
  // Окно прослушивания кончилось или эфир сейчас не слушаем.
  if (scanning && (now - scan_at >= scan_len || pace_ == Pace::Off || pace_ == Pace::Night)) {
    scan_stop();
    scanning = false;
  }
  if (pace_ == Pace::Off || pace_ == Pace::Night) return;
  // Слушаем окнами, как брелок: 150 мс раз в 30 с; когда рядом кто-то есть — 300 мс раз в 1.5 с.
  const bool busy = pace_ == Pace::Busy;
  if ((scan_now || now - scan_at >= (busy ? 1500u : 30000u)) && !scanning) {
    scan_now = false;
    scan_at = now;
    scan_len = busy ? 300u : 150u;
    scanning = scan_start();
  }
}

bool take(face::Advert* a, int* rssi) {
  const uint8_t tail = q_tail.load(std::memory_order_relaxed);
  if (tail == q_head.load(std::memory_order_acquire)) return false;
  *a = queue[tail].a;
  *rssi = queue[tail].rssi;
  q_tail.store((uint8_t)((tail + 1) % kQueue), std::memory_order_release);
  return true;
}

}  // namespace radio
