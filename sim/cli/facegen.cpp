// facegen — консольный симулятор: рендерит эмоции в GIF, собирает
// контактные листы, проигрывает сценарии и меряет нагрузку на шину.
//
//   facegen list
//   facegen still <emotion> out.gif [--scale 6] [--raw]
//   facegen anim <emotion> out.gif [--seconds 4] [--scale 4] [--seed 1]
//   facegen sheet out.gif [--scale 2] [--cols 6] [--anim] [--seconds 4] [--cat N]
//   facegen trans <from> <to> out.gif [--scale 4]
//   facegen clip <emotion> <clip> out.gif [--scale 4]
//   facegen demo out.gif [--scale 3] [--per 2.5] [--ids a,b] [--module]
//   facegen bench [--seconds 60]
//   facegen scene <scene> out.gif [--text X] [--value 1] [--key N] [--scale 4]
//   facegen id <serial>                 — характер и внешность брелока по серийному номеру
//   facegen sound <cue|scene|all> out.wav [--serial N] [--value 1] [--key N]
//                                       — голос брелока в WAV: меандр, как у пьезопищалки;
//                                         cue:boot, cue:duel — реплика, а не музыка сцены
// Общие ключи: --theme cyber, --serial N (внешность конкретного брелока).
//   facegen catalog [docs/emotions.md]          — каталог по-русски
//   facegen catalog --lang en docs/emotions.en.md — каталог по-английски
// --lang en|ru — язык названий и описаний (list, catalog, id).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "face/engine.h"
#include "face/identity.h"
#include "face/sound.h"
#include "gif_writer.h"
#include "oled_look.h"

using namespace face;
using sim::Canvas;

namespace {

struct Args {
  std::vector<std::string> pos;
  int scale = -1;
  int cols = 6;
  int seed = 1;
  int cat = -1;
  float seconds = -1;
  float per = 2.5f;
  bool raw = false;
  bool anim = false;
  bool drift = false;
  std::string ids;        // --ids a,b,c — свой набор эмоций
  bool module = false;    // --module — кадр в виде модуля SSD1306 на синей плате
  bool cyber = false;     // --theme cyber
  long long serial = -1;  // --serial N
  std::string text;       // --text — подпись для сцены
  int value = 0;          // --value — исход сцены
  long long key = 0;      // --key — ключ секретного разговора (сцена chat)
  Lang lang = Lang::Ru;   // --lang en|ru
};

// Тема и внешность для всех движков, которые создаёт facegen.
Args g_style;

void style(FaceEngine& eng) {
  if (g_style.cyber) eng.options().theme = Theme::Cyber;
  if (g_style.serial >= 0) eng.set_quirks(make_identity((uint64_t)g_style.serial).quirks, 0);
}

Args parse(int argc, char** argv) {
  Args a;
  for (int i = 2; i < argc; ++i) {
    std::string s = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
    if (s == "--scale")
      a.scale = atoi(next());
    else if (s == "--cols")
      a.cols = atoi(next());
    else if (s == "--seed")
      a.seed = atoi(next());
    else if (s == "--cat")
      a.cat = atoi(next());
    else if (s == "--seconds")
      a.seconds = (float)atof(next());
    else if (s == "--per")
      a.per = (float)atof(next());
    else if (s == "--raw")
      a.raw = true;
    else if (s == "--anim")
      a.anim = true;
    else if (s == "--drift")
      a.drift = true;
    else if (s == "--ids")
      a.ids = next();
    else if (s == "--module")
      a.module = true;
    else if (s == "--theme")
      a.cyber = std::string(next()) == "cyber";
    else if (s == "--serial")
      a.serial = atoll(next());
    else if (s == "--text")
      a.text = next();
    else if (s == "--value")
      a.value = atoi(next());
    else if (s == "--key")
      a.key = atoll(next());
    else if (s == "--lang") {
      const char* l = next();
      if (!lang_from_id(l, &a.lang)) fprintf(stderr, "Неизвестный язык: %s (ru или en)\n", l);
    }
    else
      a.pos.push_back(s);
  }
  return a;
}

bool find_expr(const std::string& id, Expr* e) {
  if (expr_from_id(id.c_str(), e)) return true;
  fprintf(stderr, "Неизвестная эмоция: %s (см. facegen list)\n", id.c_str());
  return false;
}

bool find_clip(const std::string& id, Clip* c) {
  for (int i = 0; i < kClipCount; ++i) {
    if (id == clip_id((Clip)i)) {
      *c = (Clip)i;
      return true;
    }
  }
  fprintf(stderr, "Неизвестный клип: %s\n", id.c_str());
  return false;
}

// Кадр «как на устройстве» с рамкой.
struct Screen {
  int scale;
  int pad;
  int w() const { return face::kWidth * scale + pad * 2; }
  int h() const { return face::kHeight * scale + pad * 2; }
};

void draw_screen(Canvas& cv, const Framebuffer& fb, const Screen& s, int ox, int oy, bool raw) {
  cv.rect(ox, oy, s.w(), s.h(), sim::kBezel);
  sim::draw_oled(cv, fb, ox + s.pad, oy + s.pad, s.scale, raw);
}

// Разбирает --ids a,b,c; пустой список — все эмоции (или одна категория).
std::vector<Expr> expr_list(const Args& a) {
  std::vector<Expr> list;
  if (!a.ids.empty()) {
    size_t p = 0;
    while (p <= a.ids.size()) {
      size_t q = a.ids.find(',', p);
      if (q == std::string::npos) q = a.ids.size();
      Expr e;
      if (q > p && find_expr(a.ids.substr(p, q - p), &e)) list.push_back(e);
      p = q + 1;
    }
  } else {
    for (int i = 0; i < kExprCount; ++i)
      if (a.cat < 0 || (int)expression((Expr)i).cat == a.cat) list.push_back((Expr)i);
  }
  return list;
}

// Устоявшийся кадр эмоции: без моргания, взгляда и дрейфа.
void settled_frame(Expr e, Framebuffer& out) {
  FaceEngine eng;
  eng.options().drift = false;
  eng.options().idle_motion = false;
  eng.init(1, 0);
  style(eng);
  eng.set_expression(e, 0, 0);
  // Немного времени, чтобы оверлеи успели появиться.
  for (uint32_t t = 0; t <= 600; t += 33) eng.update(t);
  out = eng.frame();
}

int cmd_list() {
  for (int c = 0; c < (int)Category::Count; ++c) {
    printf("\n[%s]\n", category_name((Category)c));
    for (int i = 0; i < kExprCount; ++i) {
      const Expression& x = expression((Expr)i);
      if ((int)x.cat != c) continue;
      printf("  %-18s %s\n", x.id, expr_name((Expr)i));
    }
  }
  printf("\nКлипы:");
  for (int i = 0; i < kClipCount; ++i) printf(" %s", clip_id((Clip)i));
  printf("\n");
  return 0;
}

// Текст для Markdown: GitHub читает пару «$» как формулу, поэтому знак экранируется.
std::string md(const char* s) {
  std::string out;
  for (; *s; ++s) {
    if (*s == '$') out += '\\';
    out += *s;
  }
  return out;
}

// Каталог в Markdown: генерируется из пресетов, поэтому всегда совпадает с кодом.
int cmd_catalog(const Args& a) {
  FILE* f = a.pos.empty() ? stdout : fopen(a.pos[0].c_str(), "w");
  if (!f) return 1;
  const bool en = a.lang == Lang::En;
  if (en)
    fprintf(f,
            "# Emotion catalog\n\n"
            "Generated by `facegen catalog --lang en docs/emotions.en.md` from `engine/src/expressions.cpp`; "
            "do not edit by hand. Emotions: %d. Russian version: [emotions.md](emotions.md).\n\n"
            "![All emotions](img/emotions_sheet.gif)\n",
            kExprCount);
  else
    fprintf(f,
            "# Каталог эмоций\n\n"
            "Файл создан командой `facegen catalog docs/emotions.md` из `engine/src/expressions.cpp`, "
            "руками не править. Всего эмоций: %d.\n\n"
            "![Все эмоции](img/emotions_sheet.gif)\n",
            kExprCount);
  for (int c = 0; c < (int)Category::Count; ++c) {
    int n = 0;
    for (int i = 0; i < kExprCount; ++i) n += (int)expression((Expr)i).cat == c;
    if (!n) continue;
    fprintf(f, en ? "\n## %s (%d)\n\n| id | Emotion | When it appears | Then |\n|---|---|---|---|\n"
                  : "\n## %s (%d)\n\n| id | Эмоция | Когда появляется | Затем |\n|---|---|---|---|\n",
            category_name((Category)c, a.lang), n);
    for (int i = 0; i < kExprCount; ++i) {
      const Expression& x = expression((Expr)i);
      if ((int)x.cat != c) continue;
      const std::string name = md(expr_name((Expr)i, a.lang)), when = md(expr_when((Expr)i, a.lang));
      if (x.hold_ms)
        fprintf(f, en ? "| `%s` | %s | %s | → `%s` after %.1f s |\n" : "| `%s` | %s | %s | → `%s` через %.1f с |\n",
                x.id, name.c_str(), when.c_str(), expr_id(x.next), x.hold_ms / 1000.0);
      else
        fprintf(f, en ? "| `%s` | %s | %s | stays |\n" : "| `%s` | %s | %s | держится |\n", x.id, name.c_str(),
                when.c_str());
    }
  }
  if (f != stdout) fclose(f);
  return 0;
}

int cmd_still(const Args& a) {
  if (a.pos.size() < 2) return 2;
  Expr e;
  if (!find_expr(a.pos[0], &e)) return 1;
  Framebuffer fb;
  settled_frame(e, fb);
  Screen s{a.scale > 0 ? a.scale : 6, a.scale > 0 ? a.scale * 2 : 12};
  Canvas cv;
  cv.init(s.w(), s.h());
  draw_screen(cv, fb, s, 0, 0, a.raw);
  sim::GifWriter g;
  if (!g.open(a.pos[1], cv.w, cv.h, sim::oled_palette(), false)) return 1;
  g.frame(cv.px, 100);
  g.close();
  return 0;
}

// Проигрывает движок и пишет GIF. script вызывается перед каждым кадром.
template <typename Script>
int record(const std::string& path, float seconds, int scale, bool raw, uint32_t seed, bool drift, Script script,
           bool module = false) {
  FaceEngine eng;
  eng.options().drift = drift;
  eng.init(seed, 0);
  style(eng);
  Screen s{scale, scale * 2};
  Canvas cv;
  if (module)
    cv.init(sim::module_w(scale), sim::module_h(scale));
  else
    cv.init(s.w(), s.h());
  sim::GifWriter g;
  if (!g.open(path, cv.w, cv.h, sim::oled_palette(), true)) return 1;
  if (module) g.set_transparent(sim::kBg);  // фон вокруг платы прозрачный: смотрится в любой теме
  const uint32_t step = 20;  // GIF хранит задержки в сотых, шаг 20 мс
  const uint32_t total = (uint32_t)(seconds * 1000.0f);
  for (uint32_t t = 0; t < total; t += step) {
    script(eng, t);
    eng.update(t);
    if (module)
      sim::draw_module(cv, eng.frame(), 0, 0, scale, raw);
    else
      draw_screen(cv, eng.frame(), s, 0, 0, raw);
    g.frame(cv.px, (int)(step / 10));
  }
  g.close();
  return 0;
}

int cmd_anim(const Args& a) {
  if (a.pos.size() < 2) return 2;
  Expr e;
  if (!find_expr(a.pos[0], &e)) return 1;
  float sec = a.seconds > 0 ? a.seconds : 4.0f;
  return record(
      a.pos[1], sec, a.scale > 0 ? a.scale : 4, a.raw, (uint32_t)a.seed, a.drift,
      [&](FaceEngine& eng, uint32_t t) {
        if (t == 0) eng.set_expression(e, 0, 0);
      },
      a.module);
}

int cmd_trans(const Args& a) {
  if (a.pos.size() < 3) return 2;
  Expr from, to;
  if (!find_expr(a.pos[0], &from) || !find_expr(a.pos[1], &to)) return 1;
  float sec = a.seconds > 0 ? a.seconds : 3.0f;
  return record(a.pos[2], sec, a.scale > 0 ? a.scale : 4, a.raw, (uint32_t)a.seed, false,
                [&](FaceEngine& eng, uint32_t t) {
                  if (t == 0) eng.set_expression(from, 0, 0);
                  if (t == 1000) eng.set_expression(to, t);
                });
}

int cmd_clip(const Args& a) {
  if (a.pos.size() < 3) return 2;
  Expr e;
  Clip c;
  if (!find_expr(a.pos[0], &e) || !find_clip(a.pos[1], &c)) return 1;
  float sec = a.seconds > 0 ? a.seconds : (clip_duration_ms(c) + 1400) / 1000.0f;
  return record(a.pos[2], sec, a.scale > 0 ? a.scale : 4, a.raw, (uint32_t)a.seed, false,
                [&](FaceEngine& eng, uint32_t t) {
                  if (t == 0) eng.set_expression(e, 0, 0);
                  if (t == 600) eng.play_clip(c, t);
                });
}

int cmd_demo(const Args& a) {
  if (a.pos.empty()) return 2;
  // Сценарий: обход эмоций по очереди (весь каталог, категория или --ids).
  const std::vector<Expr> order = expr_list(a);
  const uint32_t per = (uint32_t)(a.per * 1000.0f);
  const float sec = (float)(order.size() * per) / 1000.0f;
  return record(
      a.pos[0], sec, a.scale > 0 ? a.scale : 3, a.raw, (uint32_t)a.seed, false,
      [&](FaceEngine& eng, uint32_t t) {
        if (t % per == 0) {
          size_t i = t / per;
          if (i < order.size()) eng.set_expression(order[i], t);
        }
      },
      a.module);
}

int cmd_sheet(const Args& a) {
  if (a.pos.empty()) return 2;
  const std::vector<Expr> list = expr_list(a);
  const int scale = a.scale > 0 ? a.scale : 2;
  const int cols = a.cols > 0 ? a.cols : 6;
  const int rows = ((int)list.size() + cols - 1) / cols;
  Screen s{scale, scale * 2};
  const int label_h = 5 * 2 + 6;
  const int gap = 8;
  const int cw = s.w() + gap, ch = s.h() + label_h + gap;
  Canvas cv;
  cv.init(cols * cw + gap, rows * ch + gap);
  sim::GifWriter g;
  if (!g.open(a.pos[0], cv.w, cv.h, sim::oled_palette(), a.anim)) return 1;

  auto label = [&](int idx, int x, int y) {
    std::string id = expression(list[idx]).id;
    sim::draw_text(cv, x, y + s.h() + 4, id, 2, sim::kText);
  };

  if (!a.anim) {
    for (size_t i = 0; i < list.size(); ++i) {
      Framebuffer fb;
      settled_frame(list[i], fb);
      int x = gap + (int)(i % cols) * cw, y = gap + (int)(i / cols) * ch;
      draw_screen(cv, fb, s, x, y, a.raw);
      label((int)i, x, y);
    }
    g.frame(cv.px, 100);
    g.close();
    return 0;
  }

  std::vector<FaceEngine> engines(list.size());
  for (size_t i = 0; i < list.size(); ++i) {
    engines[i].options().drift = false;
    engines[i].init((uint32_t)(i * 7919 + 1), 0);
    style(engines[i]);
    engines[i].set_expression(list[i], 0, 0);
  }
  const float sec = a.seconds > 0 ? a.seconds : 4.0f;
  const uint32_t step = 40;
  for (uint32_t t = 0; t < (uint32_t)(sec * 1000); t += step) {
    for (size_t i = 0; i < list.size(); ++i) {
      engines[i].update(t);
      int x = gap + (int)(i % cols) * cw, y = gap + (int)(i / cols) * ch;
      draw_screen(cv, engines[i].frame(), s, x, y, a.raw);
      if (t == 0) label((int)i, x, y);
    }
    g.frame(cv.px, (int)(step / 10));
  }
  g.close();
  return 0;
}

int cmd_bench(const Args& a) {
  const float sec = a.seconds > 0 ? a.seconds : 60.0f;
  printf("%-18s %8s %8s %9s %10s %9s\n", "emotion", "render", "fps", "bytes/s", "i2c_ms/s", "lit_px");
  double total_us = 0;
  int total_renders = 0;
  for (int i = 0; i < kExprCount; ++i) {
    FaceEngine eng;
    eng.init(7, 0);
    eng.set_expression((Expr)i, 0, 0);
    // Как прошивка: просыпаемся только тогда, когда движок попросил.
    uint32_t t = 0;
    const uint32_t end = (uint32_t)(sec * 1000.0f);
    double us = 0;
    int lit = 0;
    while (t < end) {
      auto t0 = std::chrono::steady_clock::now();
      eng.update(t);
      auto t1 = std::chrono::steady_clock::now();
      us += std::chrono::duration<double, std::micro>(t1 - t0).count();
      lit = eng.frame().count_lit();
      t = eng.next_update_ms();
    }
    // Если экспрессия сама переходит в другую, считаем всё вместе — так и будет на устройстве.
    const EngineStats& st = eng.stats();
    const double fps = st.frames / sec;
    const double bps = st.bytes / sec;
    // I2C 400 кГц: 9 бит на байт плюс ~8 байт команд и адреса на окно.
    const double i2c_ms = (bps + fps * 8.0 * 3.0) * 9.0 / 400000.0 * 1000.0;
    printf("%-18s %6.1fus %8.2f %9.0f %10.1f %9d\n", expression((Expr)i).id, us / (st.renders ? st.renders : 1), fps,
           bps, i2c_ms, lit);
    total_us += us;
    total_renders += (int)st.renders;
  }
  printf("\nСреднее время кадра на ПК: %.1f мкс (%d кадров)\n", total_us / (total_renders ? total_renders : 1),
         total_renders);
  return 0;
}

int cmd_scene(const Args& a) {
  if (a.pos.size() < 2) return 2;
  Scene sc;
  if (!scene_from_id(a.pos[0].c_str(), &sc)) {
    fprintf(stderr, "Нет такой сцены: %s\n", a.pos[0].c_str());
    return 1;
  }
  SceneParams p;
  p.set_text(a.text.c_str());
  p.value = a.value;
  p.key = a.key ? (uint32_t)a.key : 0x5EC12E7u;
  const float sec = a.seconds > 0 ? a.seconds : scene_duration_ms(sc) / 1000.0f + 1.0f;
  return record(a.pos[1], sec, a.scale > 0 ? a.scale : 4, a.raw, (uint32_t)a.seed, false,
                [&](FaceEngine& eng, uint32_t t) {
                  if (t == 0) eng.play_scene(sc, 0, p);
                });
}

int cmd_id(const Args& a) {
  if (a.pos.empty()) return 2;
  const Identity id = make_identity((uint64_t)atoll(a.pos[0].c_str()));
  const Personality& p = id.personality;
  const Quirks& q = id.quirks;
  const Temperament& t = temperament(id.archetype);
  printf("%s: %s (%s), %s\n", id.code, archetype_name(id.archetype), t.tag, rarity_name(id.rarity));
  printf("  в покое %s, рад %s, скучает %s; на касание %s/%s/%s/%s; темп ×%.2f, активность ×%.2f\n",
         expression(t.rest).id, expression(t.rest_happy).id, expression(t.rest_bored).id, expression(t.tap[0]).id,
         expression(t.tap[1]).id, expression(t.tap[2]).id, expression(t.tap[3]).id, t.tempo, t.activity);
  printf("  игривость %.2f, ворчливость %.2f, сонливость %.2f, застенчивость %.2f\n", p.playful, p.grumpy, p.sleepy,
         p.shy);
  printf("  глаза %+.0f px и ×%.2f, зрачки ×%.2f, моргает ×%.2f, брови %+.0f px%s\n", q.spacing, q.eye_scale,
         q.pupil_scale, q.blink_rate, q.brow_lift, q.cyber_eye ? ", золотой кибер-глаз" : "");
  return 0;
}

// Ноты голоса → меандр 22 кГц. Фаза непрерывна между нотами, как у ШИМ пищалки.
void synth(const Voice& v, std::vector<int16_t>& pcm, int rate) {
  double ph = 0;
  for (int i = 0; i < v.length(); ++i) {
    const Note& n = v.note(i);
    const int len = n.ms * rate / 1000;
    for (int k = 0; k < len; ++k) {
      if (!n.hz) {
        pcm.push_back(0);
        continue;
      }
      ph += (double)n.hz / rate;
      ph -= (int)ph;
      pcm.push_back(ph < 0.5 ? 6000 : -6000);
    }
  }
}

bool write_wav(const std::string& path, const std::vector<int16_t>& pcm, int rate) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  auto u32 = [&](uint32_t x) { fwrite(&x, 4, 1, f); };
  auto u16 = [&](uint16_t x) { fwrite(&x, 2, 1, f); };
  const uint32_t data = (uint32_t)pcm.size() * 2;
  fwrite("RIFF", 1, 4, f), u32(36 + data), fwrite("WAVEfmt ", 1, 8, f), u32(16), u16(1), u16(1), u32((uint32_t)rate);
  u32((uint32_t)rate * 2), u16(2), u16(16), fwrite("data", 1, 4, f), u32(data);
  fwrite(pcm.data(), 2, pcm.size(), f);
  fclose(f);
  return true;
}

int cmd_sound(const Args& a) {
  if (a.pos.size() < 2) return 2;
  const Identity id = make_identity(a.serial >= 0 ? (uint64_t)a.serial : 2u);
  const Temperament& t = temperament(id.archetype);
  constexpr int kRate = 22050;
  std::vector<int16_t> pcm;
  Voice v;
  v.init(&t, 1);
  Cue c;
  Scene sc;
  const std::string& what = a.pos[0];
  if (what == "all") {
    for (int i = 0; i < kCueCount; ++i) {
      v.say((Cue)i, 0);
      synth(v, pcm, kRate);
      pcm.insert(pcm.end(), kRate * 4 / 10, 0);
    }
  } else if (what.rfind("cue:", 0) == 0 ? cue_from_id(what.c_str() + 4, &c)
                                          : !scene_from_id(what.c_str(), &sc) && cue_from_id(what.c_str(), &c)) {
    // boot и duel — и реплики, и сцены: без приставки cue: берётся сцена.
    v.say(c, 0);
    synth(v, pcm, kRate);
  } else if (scene_from_id(what.c_str(), &sc)) {
    SceneParams p;
    p.value = a.value;
    p.key = a.key ? (uint32_t)a.key : 0x5EC12E7u;
    if (sc == Scene::chat)
      v.chat(p.key, true, 0, &temperament(archetype_from_hash(p.key)));  // обе стороны разговора
    else
      v.scene(sc, p, 0);
    synth(v, pcm, kRate);
  } else {
    fprintf(stderr, "Нет такой реплики или сцены: %s\n", what.c_str());
    return 1;
  }
  if (!write_wav(a.pos[1], pcm, kRate)) return 1;
  printf("%s: голос %s (%s), %.1f с\n", a.pos[1].c_str(), t.name, t.tag, (double)pcm.size() / kRate);
  return 0;
}

void usage() {
  printf(
      "facegen list\n"
      "facegen still <emotion> out.gif [--scale 6] [--raw]\n"
      "facegen anim <emotion> out.gif [--seconds 4] [--scale 4] [--seed 1] [--drift]\n"
      "facegen sheet out.gif [--scale 2] [--cols 6] [--anim] [--seconds 4] [--cat N] [--ids a,b] [--raw]\n"
      "facegen trans <from> <to> out.gif [--scale 4]\n"
      "facegen clip <emotion> <clip> out.gif [--scale 4]\n"
      "facegen demo out.gif [--scale 3] [--per 2.5] [--cat N] [--ids a,b] [--module]\n"
      "facegen bench [--seconds 60]\n"
      "facegen catalog [--lang en] [docs/emotions.md]\n"
      "facegen scene <scene> out.gif [--text X] [--value 1] [--key N] [--scale 4]\n"
      "facegen id <serial>\n"
      "facegen sound <cue|scene|all> out.wav [--serial N] [--value 1] [--key N]\n"
      "общие ключи: --theme cyber, --serial N, --lang en|ru\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  std::string cmd = argv[1];
  Args a = parse(argc, argv);
  g_style = a;
  set_lang(a.lang);
  int rc = 2;
  if (cmd == "list")
    rc = cmd_list();
  else if (cmd == "still")
    rc = cmd_still(a);
  else if (cmd == "anim")
    rc = cmd_anim(a);
  else if (cmd == "sheet")
    rc = cmd_sheet(a);
  else if (cmd == "trans")
    rc = cmd_trans(a);
  else if (cmd == "clip")
    rc = cmd_clip(a);
  else if (cmd == "demo")
    rc = cmd_demo(a);
  else if (cmd == "bench")
    rc = cmd_bench(a);
  else if (cmd == "catalog")
    rc = cmd_catalog(a);
  else if (cmd == "scene")
    rc = cmd_scene(a);
  else if (cmd == "id")
    rc = cmd_id(a);
  else if (cmd == "sound")
    rc = cmd_sound(a);
  if (rc == 2) usage();
  return rc;
}
