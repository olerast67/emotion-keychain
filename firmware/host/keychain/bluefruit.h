// Bluefruit: только то, что нужно встречам без соединения, — реклама и прослушивание.
#pragma once

#include <stdint.h>
#include <string.h>

#include <vector>

#include "Arduino.h"

#define BLE_GAP_AD_TYPE_FLAGS 0x01
#define BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA 0xFF
#define BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED 0x04
#define BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED 0x03
#define BANDWIDTH_LOW 1

struct ble_data_t {
  uint8_t* p_data;
  uint16_t len;
};
struct ble_gap_evt_adv_report_t {
  ble_data_t data;
  int8_t rssi;
};

class BLEAdvertising {
 public:
  void stop() { running = false; }
  void clearData() { data.clear(); }
  bool addFlags(uint8_t f) { return add(BLE_GAP_AD_TYPE_FLAGS, &f, 1); }
  bool addManufacturerData(const void* d, uint8_t n) { return add(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, d, n); }
  void setType(uint8_t t) { type = t; }
  void setInterval(uint16_t a, uint16_t) { interval = a; }
  void setFastTimeout(uint16_t s) { fast_timeout = s; }
  void restartOnDisconnect(bool) {}
  bool start(uint16_t timeout_s) {
    if (data.size() > 31) return false;
    running = true;
    timeout = timeout_s;
    ++starts;
    return true;
  }

  std::vector<uint8_t> data;
  uint16_t interval = 0, fast_timeout = 30, timeout = 0;
  uint8_t type = 0;
  bool running = false;
  uint32_t starts = 0;

 private:
  bool add(uint8_t t, const void* d, uint8_t n) {
    if (data.size() + 2 + n > 31) return false;
    data.push_back((uint8_t)(n + 1));
    data.push_back(t);
    data.insert(data.end(), (const uint8_t*)d, (const uint8_t*)d + n);
    return true;
  }
};

class BLEScanner {
 public:
  void setRxCallback(void (*cb)(ble_gap_evt_adv_report_t*)) { cb_ = cb; }
  void restartOnDisconnect(bool) {}
  void useActiveScan(bool a) { active = a; }
  void setInterval(uint16_t i, uint16_t w) { interval = i, window = w; }
  bool start(uint16_t timeout_10ms);
  void stop() { running_ = false, paused_ = false; }
  void resume() { paused_ = false; }
  bool isRunning();
  // Для модели эфира: доставить пакет, если слушаем.
  bool deliver(const uint8_t* d, uint16_t n, int8_t rssi);

  uint16_t interval = 0, window = 0;
  bool active = true;

 private:
  void (*cb_)(ble_gap_evt_adv_report_t*) = nullptr;
  bool running_ = false, paused_ = false;
  uint64_t until_us_ = 0;
};

class AdafruitBluefruit {
 public:
  void autoConnLed(bool) {}
  void configPrphBandwidth(int) {}
  bool begin(int prph, int central);
  void setTxPower(int8_t p) { tx_power = p; }
  void setName(const char*) {}

  BLEAdvertising Advertising;
  BLEScanner Scanner;
  int8_t tx_power = 0;
  bool started = false;
};
extern AdafruitBluefruit Bluefruit;
