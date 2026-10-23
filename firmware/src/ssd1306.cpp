#include "ssd1306.h"

namespace {

// Сколько байт данных влезает в одну I2C-транзакцию (минус управляющий байт 0x40).
#if defined(I2C_BUFFER_LENGTH) && I2C_BUFFER_LENGTH >= 128
constexpr size_t kChunk = 64;
#else
constexpr size_t kChunk = 30;
#endif

constexpr uint8_t kInitSsd1306[] = {
    0xAE,        // экран выключен
    0xD5, 0x80,  // частота развёртки
    0xA8, 0x3F,  // мультиплекс 1/64
    0xD3, 0x00,  // без вертикального смещения
    0x40,        // стартовая строка 0
    0x8D, 0x14,  // встроенный charge pump включён
    0x20, 0x00,  // горизонтальная адресация: окно 0x21/0x22 заполняется подряд
    0xA1,        // сегменты слева направо
    0xC8,        // строки сверху вниз
    0xDA, 0x12,  // раскладка COM для 128×64
    0x81, 0x7F,  // контраст
    0xD9, 0xF1,  // precharge
    0xDB, 0x40,  // VCOMH
    0xA4,        // показывать содержимое памяти
    0xA6,        // без инверсии
    0x2E,        // без аппаратной прокрутки
    0xAF,        // экран включён
};

// У SH1106 нет окон 0x21/0x22 и горизонтальной адресации: страницы пишутся по одной.
constexpr uint8_t kInitSh1106[] = {
    0xAE,        // экран выключен
    0xD5, 0x80,  // частота развёртки
    0xA8, 0x3F,  // мультиплекс 1/64
    0xD3, 0x00,  // без вертикального смещения
    0x40,        // стартовая строка 0
    0xAD, 0x8B,  // встроенный DC-DC включён
    0xA1,        // сегменты слева направо
    0xC8,        // строки сверху вниз
    0xDA, 0x12,  // раскладка COM
    0x81, 0x7F,  // контраст
    0xD9, 0x22,  // precharge
    0xDB, 0x35,  // VCOMH
    0xA4,        // показывать содержимое памяти
    0xA6,        // без инверсии
    0xAF,        // экран включён
};

bool probe(TwoWire& w, uint8_t a) {
  w.beginTransmission(a);
  return w.endTransmission() == 0;
}

}  // namespace

bool Ssd1306::begin(TwoWire& wire, uint8_t addr, bool sh1106) {
  wire_ = &wire;
  sh1106_ = sh1106;
  if (addr == 0) {
    if (probe(wire, 0x3C))
      addr = 0x3C;
    else if (probe(wire, 0x3D))
      addr = 0x3D;
    else
      return false;
  } else if (!probe(wire, addr)) {
    return false;
  }
  addr_ = addr;
  if (sh1106_)
    cmds(kInitSh1106, sizeof(kInitSh1106));
  else
    cmds(kInitSsd1306, sizeof(kInitSsd1306));
  face::Framebuffer blank;
  blank.clear();
  send_full(blank);
  return true;
}

void Ssd1306::cmd(uint8_t c) { cmds(&c, 1); }

void Ssd1306::cmds(const uint8_t* c, size_t n) {
  while (n) {
    const size_t k = n < kChunk ? n : kChunk;
    wire_->beginTransmission(addr_);
    wire_->write((uint8_t)0x00);  // дальше идут команды
    wire_->write(c, k);
    wire_->endTransmission();
    c += k;
    n -= k;
  }
}

void Ssd1306::contrast(uint8_t v) {
  const uint8_t c[2] = {0x81, v};
  cmds(c, 2);
}

void Ssd1306::power(bool on) { cmd(on ? 0xAF : 0xAE); }

void Ssd1306::send_window(const face::Framebuffer& fb, uint8_t page, uint8_t x0, uint8_t x1) {
  if (sh1106_) {
    // У SH1106 память на 132 столбца, видимая часть начинается со 2-го.
    const uint8_t col = (uint8_t)(x0 + 2);
    const uint8_t c[3] = {(uint8_t)(0xB0 | page), (uint8_t)(0x00 | (col & 0x0F)), (uint8_t)(0x10 | (col >> 4))};
    cmds(c, 3);
  } else {
    const uint8_t c[6] = {0x21, x0, x1, 0x22, page, page};
    cmds(c, 6);
  }
  const uint8_t* data = fb.data + page * face::kWidth + x0;
  size_t n = (size_t)(x1 - x0 + 1);
  while (n) {
    const size_t k = n < kChunk ? n : kChunk;
    wire_->beginTransmission(addr_);
    wire_->write((uint8_t)0x40);  // дальше идут данные
    wire_->write(data, k);
    wire_->endTransmission();
    data += k;
    n -= k;
  }
}

void Ssd1306::send_full(const face::Framebuffer& fb) {
  for (uint8_t p = 0; p < face::kPages; ++p) send_window(fb, p, 0, face::kWidth - 1);
}
