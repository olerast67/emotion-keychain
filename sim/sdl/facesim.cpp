// facesim — оконный симулятор на SDL2.
//
// Клавиши:
//   ← →        предыдущая / следующая эмоция
//   ↑ ↓        предыдущая / следующая категория
//   Пробел     случайная эмоция
//   1..9, 0    клипы: моргнуть, подмигнуть, кивнуть, мотнуть головой, испуг,
//              зевок, двойной взгляд, чих, осмотреться, прыжок
//   D          дрейф против выгорания вкл/выкл
//   L          ретро-режим через строку
//   U          перевернуть экран
//   G          подсветить изменённые окна (что уходит по шине)
//   R          чистые пиксели без сетки
//   P          сохранить кадр в facesim.gif
//   Esc        выход
//
// --frames N  выйти через N кадров (для проверки без экрана)
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "face/engine.h"
#include "gif_writer.h"
#include "oled_look.h"

using namespace face;

namespace {

constexpr int kScale = 6;
constexpr int kPad = 16;
constexpr int kHud = 40;

Expr step_category(Expr cur, int dir) {
  int c = (int)expression(cur).cat;
  c = (c + dir + (int)Category::Count) % (int)Category::Count;
  for (int i = 0; i < kExprCount; ++i)
    if ((int)expression((Expr)i).cat == c) return (Expr)i;
  return cur;
}

}  // namespace

int main(int argc, char** argv) {
  int max_frames = -1;
  for (int i = 1; i < argc; ++i)
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atoi(argv[++i]);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  const int W = kWidth * kScale + kPad * 2, H = kHeight * kScale + kPad * 2 + kHud;
  SDL_Window* win = SDL_CreateWindow("facesim", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
  SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, 0) : nullptr;
  SDL_Texture* tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, W, H) : nullptr;
  if (!tex) {
    fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }

  FaceEngine eng;
  const uint32_t start = SDL_GetTicks();
  eng.init(start, 0);
  Expr cur = Expr::neutral;
  bool show_dirty = false, raw = false;
  const auto pal = sim::oled_palette();
  sim::Canvas cv;
  cv.init(W, H);
  std::vector<uint8_t> rgb((size_t)W * H * 3);
  DirtyWindow last_dirty[kPages];
  int last_dirty_n = 0;
  uint32_t last_flash = 0;
  int frames = 0;
  bool running = true;

  const Clip number_clips[10] = {Clip::blink, Clip::wink,        Clip::nod,    Clip::headshake,   Clip::startle,
                                 Clip::yawn,  Clip::double_take, Clip::sneeze, Clip::look_around, Clip::hop};

  while (running) {
    const uint32_t now = SDL_GetTicks() - start;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) running = false;
      if (ev.type != SDL_KEYDOWN) continue;
      SDL_Keycode k = ev.key.keysym.sym;
      if (k == SDLK_ESCAPE)
        running = false;
      else if (k == SDLK_RIGHT)
        cur = (Expr)(((int)cur + 1) % kExprCount), eng.set_expression(cur, now);
      else if (k == SDLK_LEFT)
        cur = (Expr)(((int)cur + kExprCount - 1) % kExprCount), eng.set_expression(cur, now);
      else if (k == SDLK_DOWN)
        cur = step_category(cur, 1), eng.set_expression(cur, now);
      else if (k == SDLK_UP)
        cur = step_category(cur, -1), eng.set_expression(cur, now);
      else if (k == SDLK_SPACE)
        cur = (Expr)(rand() % kExprCount), eng.set_expression(cur, now);
      else if (k >= SDLK_0 && k <= SDLK_9)
        eng.play_clip(number_clips[k == SDLK_0 ? 9 : k - SDLK_1], now);
      else if (k == SDLK_d)
        eng.options().drift = !eng.options().drift;
      else if (k == SDLK_l)
        eng.options().scanlines = !eng.options().scanlines;
      else if (k == SDLK_u)
        eng.options().upside_down = !eng.options().upside_down;
      else if (k == SDLK_g)
        show_dirty = !show_dirty;
      else if (k == SDLK_r)
        raw = !raw;
      else if (k == SDLK_p) {
        sim::GifWriter g;
        if (g.open("facesim.gif", cv.w, cv.h, pal, false)) {
          g.frame(cv.px, 100);
          g.close();
          printf("Сохранено: facesim.gif\n");
        }
      }
    }
    // Если эмоция сама сменилась (hold/next), следуем за ней.
    cur = eng.expression();

    if (now >= eng.next_update_ms() && eng.update(now)) {
      last_dirty_n = eng.dirty(last_dirty);
      last_flash = now;
    }

    cv.rect(0, 0, W, H, sim::kBg);
    cv.rect(kPad - 6, kPad - 6, kWidth * kScale + 12, kHeight * kScale + 12, sim::kBezel);
    sim::draw_oled(cv, eng.frame(), kPad, kPad, kScale, raw);
    if (show_dirty && now - last_flash < 120) {
      for (int i = 0; i < last_dirty_n; ++i) {
        const DirtyWindow& d = last_dirty[i];
        const int x0 = kPad + d.x0 * kScale, x1 = kPad + (d.x1 + 1) * kScale - 1;
        const int y0 = kPad + d.page * 8 * kScale, y1 = y0 + 8 * kScale - 1;
        for (int x = x0; x <= x1; ++x) cv.set(x, y0, sim::kDirty), cv.set(x, y1, sim::kDirty);
        for (int y = y0; y <= y1; ++y) cv.set(x0, y, sim::kDirty), cv.set(x1, y, sim::kDirty);
      }
    }
    const EngineStats& st = eng.stats();
    char hud[128];
    snprintf(hud, sizeof hud, "%s  frames %u  bytes %u", expression(cur).id, st.frames, st.bytes);
    sim::draw_text(cv, kPad, kHeight * kScale + kPad * 2 + 8, hud, 3, sim::kText);

    for (size_t i = 0; i < cv.px.size(); ++i) {
      const sim::Rgb c = pal[cv.px[i]];
      rgb[i * 3] = c.r, rgb[i * 3 + 1] = c.g, rgb[i * 3 + 2] = c.b;
    }
    SDL_UpdateTexture(tex, nullptr, rgb.data(), W * 3);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);

    std::string title = std::string("facesim — ") + expression(cur).name + " (" + expression(cur).id + ")";
    SDL_SetWindowTitle(win, title.c_str());

    if (max_frames >= 0 && ++frames >= max_frames) running = false;
    SDL_Delay(8);
  }
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
