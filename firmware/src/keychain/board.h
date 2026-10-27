// Плата брелока rev B: выводы и шины (hardware/rev-b/gen/netlist.py, GPIO).
// Номер вывода Arduino равен номеру ножки nRF52840: P0.xx — xx, P1.xx — 32 + xx.
#pragma once

#include <stdint.h>

namespace board {

// Экран: модуль SSD1306 на своей шине I2C (Wire1) за ключом питания.
constexpr uint8_t kDispEn = 19;    // P0.19, высокий — экран включён
// Датчики на шине Wire: LIS3DH и LTR-303ALS.
constexpr uint8_t kAccInt1 = 4;    // P0.04, касание (CLICK)
constexpr uint8_t kAccInt2 = 17;   // P0.17
constexpr uint8_t kAlsInt = 0;     // P0.00
// SA0 на VDD: у вывода внутренняя подтяжка, и на земле через неё всё время шёл бы ток ~0.15 мА.
constexpr uint8_t kLis3dhAddr = 0x19;
constexpr uint8_t kLtr303Addr = 0x29;
// Пищалка: два вывода, сигнал в противофазе — громче вдвое.
constexpr uint8_t kBuzA = 25;      // P0.25
constexpr uint8_t kBuzB = 34;      // P1.02
// Вибромотор через ключ.
constexpr uint8_t kVibEn = 21;     // P0.21
// Кнопка на правом торце, на землю.
constexpr uint8_t kButton = 22;    // P0.22
// Статус зарядника MCP73832 (открытый сток): 0 — заряжается.
constexpr uint8_t kChgIo = 20;     // P0.20
// Разрешение заряда: нижний конец R3 (PROG). Ноль — заряд 100 мА, отпущена — без заряда.
constexpr uint8_t kChgEn = 23;     // P0.23

// Оси акселерометра в осях брелока (x — вправо, y — вверх по экрану, z — из экрана).
// LIS3DH стоит на обороте платы, повёрнутый генератором платы вокруг её оси X (Flip сверху
// вниз): ось X датчика смотрит в сторону его выводов 1–5, то есть влево брелока, Y — вверх,
// а Z — внутрь платы. Отсюда x и z со знаком минус. Проверить на живой плате командой
// `sensors`: висит ровно — около (0, +1, 0); правый край вниз — x около −1; лицом вверх —
// z около +1.
constexpr int8_t kAccMap[3][2] = {{0, -1}, {1, +1}, {2, -1}};  // {номер оси датчика, знак}

}  // namespace board
