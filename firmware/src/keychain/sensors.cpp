#include "sensors.h"

namespace {

// Регистры LIS3DH.
constexpr uint8_t kWhoAmI = 0x0F, kCtrl1 = 0x20, kCtrl2 = 0x21, kCtrl3 = 0x22, kCtrl4 = 0x23, kCtrl5 = 0x24,
                  kOutX = 0x28, kFifoCtrl = 0x2E, kFifoSrc = 0x2F, kClickCfg = 0x38, kClickSrc = 0x39,
                  kClickThs = 0x3A, kTimeLimit = 0x3B, kTimeLatency = 0x3C, kTimeWindow = 0x3D;

// Регистры LTR-303ALS.
constexpr uint8_t kAlsContr = 0x80, kAlsMeasRate = 0x85, kPartId = 0x86, kAlsData = 0x88, kAlsStatus = 0x8C;

}  // namespace

// ---------------------------------------------------------------- LIS3DH

bool Lis3dh::write(uint8_t reg, uint8_t v) {
  w_->beginTransmission(addr_);
  w_->write(reg);
  w_->write(v);
  return w_->endTransmission() == 0;
}

bool Lis3dh::read(uint8_t reg, uint8_t* buf, size_t n) {
  w_->beginTransmission(addr_);
  w_->write(n > 1 ? (uint8_t)(reg | 0x80) : reg);  // старший бит — автоприращение адреса
  if (w_->endTransmission(false) != 0) return false;
  if (w_->requestFrom(addr_, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; ++i) buf[i] = (uint8_t)w_->read();
  return true;
}

bool Lis3dh::begin(TwoWire& wire, uint8_t addr) {
  w_ = &wire;
  addr_ = addr;
  uint8_t id = 0;
  ok_ = read(kWhoAmI, &id, 1) && id == 0x33;
  if (!ok_) return false;
  // 100 Гц, все оси; высокое разрешение, ±4 g, данные целиком (BDU).
  ok_ = write(kCtrl1, 0x57) && write(kCtrl4, 0x98) &&
        // Детектор касаний — по данным после фильтра верхних частот: наклон ему не мешает.
        write(kCtrl2, 0x04) && write(kCtrl3, 0x80) &&
        // FIFO в потоковом режиме.
        write(kCtrl5, 0x40) && write(kFifoCtrl, 0x80) &&
        // Одиночное и двойное касание по всем осям; порог 0.77 g (24 × 32 мг), флаг держится до чтения.
        write(kClickCfg, 0x3F) && write(kClickThs, 0x98) &&
        // Касание короче 80 мс, второе — через 160 мс, но в окне 480 мс (шаг 10 мс при 100 Гц).
        write(kTimeLimit, 0x08) && write(kTimeLatency, 0x10) && write(kTimeWindow, 0x30);
  return ok_;
}

int Lis3dh::read_fifo(float (*xyz)[3], int max) {
  if (!ok_) return 0;
  uint8_t src = 0;
  if (!read(kFifoSrc, &src, 1)) return 0;
  int n = src & 0x1F;
  if (src & 0x40) n = 32;  // FIFO переполнен: все 32 выборки
  if (n > max) n = max;
  // Пачками до 10 выборок за одну посылку (60 байт при буфере Wire 64): в режиме FIFO адрес
  // после OUT_Z_H (0x2D) возвращается на OUT_X_L (AN3308), и каждые 6 байт — следующая
  // выборка. Так вдесятеро меньше посылок и ожидания на шине.
  constexpr int kBurst = 10;
  int done = 0;
  while (done < n) {
    const int m = n - done < kBurst ? n - done : kBurst;
    uint8_t b[6 * kBurst];
    if (!read(kOutX, b, (size_t)(6 * m))) return done;
    for (int j = 0; j < m; ++j)
      for (int k = 0; k < 3; ++k) {
        const int16_t raw = (int16_t)(b[6 * j + 2 * k] | b[6 * j + 2 * k + 1] << 8);
        xyz[done + j][k] = (float)(raw >> 4) * 0.002f;  // 12 бит, ±4 g: 2 мг на единицу
      }
    done += m;
  }
  return n;
}

int Lis3dh::take_click() {
  if (!ok_) return 0;
  uint8_t src = 0;
  if (!read(kClickSrc, &src, 1) || !(src & 0x40)) return 0;
  return (src & 0x20) ? 2 : (src & 0x10) ? 1 : 0;
}

void Lis3dh::power(bool on) {
  if (ok_) write(kCtrl1, on ? 0x57 : 0x07);  // 0x07 — выключен, оси сохраняются
}

// ---------------------------------------------------------------- LTR-303ALS

bool Ltr303::write(uint8_t reg, uint8_t v) {
  w_->beginTransmission(addr_);
  w_->write(reg);
  w_->write(v);
  return w_->endTransmission() == 0;
}

bool Ltr303::begin(TwoWire& wire, uint8_t addr) {
  w_ = &wire;
  addr_ = addr;
  // После подачи питания датчик отвечает через 100 мс: при холодном старте ждём и пробуем ещё.
  while (millis() < 120) delay(5);
  int id = -1;
  for (int attempt = 0; attempt < 5 && id < 0; ++attempt) {
    w_->beginTransmission(addr_);
    w_->write(kPartId);
    if (w_->endTransmission(false) == 0 && w_->requestFrom(addr_, (uint8_t)1) == 1)
      id = w_->read();
    else
      delay(20);
  }
  if (id < 0) return ok_ = false;
  ok_ = (id >> 4) == 0x0A;
  // Замер 100 мс; между разовыми замерами (прошивка включает датчик на один) — сон.
  if (ok_) ok_ = write(kAlsMeasRate, 0x03) && write(kAlsContr, 0x00);
  return ok_;
}

float Ltr303::lux() {
  if (!ok_) return -1.0f;
  w_->beginTransmission(addr_);
  w_->write(kAlsStatus);
  if (w_->endTransmission(false) != 0 || w_->requestFrom(addr_, (uint8_t)1) != 1) return -1.0f;
  const uint8_t st = (uint8_t)w_->read();
  if (!(st & 0x04) || (st & 0x80)) return -1.0f;  // нет новых данных или замер недостоверен
  // Каналы читаются одной посылкой, сначала CH1: так они из одного замера.
  w_->beginTransmission(addr_);
  w_->write(kAlsData);
  if (w_->endTransmission(false) != 0 || w_->requestFrom(addr_, (uint8_t)4) != 4) return -1.0f;
  uint8_t b[4];
  for (uint8_t& x : b) x = (uint8_t)w_->read();
  const float ch1 = (float)(b[0] | b[1] << 8), ch0 = (float)(b[2] | b[3] << 8);
  if (ch0 + ch1 <= 0) return 0.0f;
  // Формула из документации Lite-On для усиления 1× и замера 100 мс.
  const float ratio = ch1 / (ch0 + ch1);
  if (ratio < 0.45f) return 1.7743f * ch0 + 1.1059f * ch1;
  if (ratio < 0.64f) return 4.2785f * ch0 - 1.9548f * ch1;
  if (ratio < 0.85f) return 0.5926f * ch0 + 0.1185f * ch1;
  return 0.0f;
}

void Ltr303::power(bool on) {
  if (ok_) write(kAlsContr, on ? 0x01 : 0x00);
}
