// Надписи на экране: латиница, цифры и знаки шрифтом 3×5.
// Каждый пиксель шрифта рисуется квадратом scale×scale; scale < 2 не бывает,
// потому что однопиксельные буквы на 0.96″ с расстояния брелока не читаются.
#pragma once

#include "face/framebuffer.h"

namespace face {

// Шаг между буквами — 4 клетки шрифта (3 буквы + 1 пробел).
int text_width(const char* s, int scale = 2);
inline int text_height(int scale = 2) { return 5 * (scale < 2 ? 2 : scale); }

// Строчные буквы рисуются как прописные; неизвестные символы — пробелом.
// visible — сколько символов показать (для эффекта «печатает»), -1 — все.
void draw_text(Framebuffer& fb, int x, int y, const char* s, int scale = 2, Ink ink = Ink::Set, int visible = -1);
void draw_text_centered(Framebuffer& fb, int cx, int y, const char* s, int scale = 2, Ink ink = Ink::Set,
                        int visible = -1);

}  // namespace face
