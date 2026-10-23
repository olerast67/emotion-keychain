// Отрисовка лица и оверлеев в кадровый буфер.
#pragma once

#include "face/expressions.h"
#include "face/framebuffer.h"
#include "face/params.h"

namespace face {

// Где оказались части лица после всех смещений: к этим точкам привязаны оверлеи.
struct Anchors {
  float ex[2], ey[2];  // центры глаз (0 — левый, 1 — правый)
  float ew[2], eh[2];  // размеры глаз с учётом масштаба пресета
  float mx, my, mw;    // центр и ширина рта
  float ox, oy;        // смещение лица
};

// Рисует глаза, брови и рот. Кадр не очищает.
void render_face(Framebuffer& fb, const Face& f, Anchors* anchors);

// Рисует один оверлей. alpha 0..1 — появление/исчезание, t — секунды с момента появления.
void render_overlay(Framebuffer& fb, Overlay o, const Anchors& a, float alpha, float t);

// Как часто оверлею нужны новые кадры: 0 — статичен, иначе желаемый период в мс.
uint16_t overlay_frame_ms(Overlay o);
const char* overlay_id(Overlay o);
// Значок привязан к экрану, а не к глазам и рту (летающие сердечки, надписи):
// поле от края он держит сам, сдвиг лица на него не влияет.
bool overlay_screen_space(Overlay o);

// Слой темы поверх лица. Cyber — прицельные рамки вокруг глаз (lock 0..1 —
// «захват цели» после смены эмоции). cyber_eye — кибер-глаз редкого брелока,
// рисуется в любой теме; glint — момент, когда он вспыхивает.
void render_theme(Framebuffer& fb, const Anchors& a, bool cyber, bool cyber_eye, float lock, bool glint);

}  // namespace face
