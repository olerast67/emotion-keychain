// Микрофон PDM: блоки по 512 выборок 16 кГц (32 мс), обработчик зовётся «из прерывания».
#pragma once

#include <stdint.h>

class PDMClass {
 public:
  void onReceive(void (*cb)()) { cb_ = cb; }
  void setBufferSize(int bytes) { buf_bytes_ = bytes; }
  int begin(int channels, long rate);
  void end();
  int available() const { return have_ ? buf_bytes_ : 0; }
  int read(void* dst, int bytes);

  // Для модели: когда следующий блок (0 — микрофон выключен) и выдать его.
  uint64_t next_tick() const;
  void tick(uint64_t now_us);

 private:
  void (*cb_)() = nullptr;
  int buf_bytes_ = 1024;
  bool running_ = false, have_ = false;
  uint64_t next_us_ = 0;
  int16_t block_[512];
  uint32_t phase_ = 0;
};
extern PDMClass PDM;
