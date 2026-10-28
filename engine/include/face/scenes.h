// Сцены — короткие полноэкранные сюжеты вместо лица: загрузка, «взлом»,
// дождь данных, праздники, апгрейд, встреча с другим брелоком, дуэль и секретный
// разговор шифром после неё, достижения, зарядка и выключение, пасхалки.
// Запускаются через FaceEngine::play_scene и сами заканчиваются.
#pragma once

#include <stdint.h>

#include "face/framebuffer.h"
#include "face/lang.h"
#include "face/params.h"

namespace face {

#define FACE_SCENE_LIST(X) \
  X(boot)                  \
  X(hack)                  \
  X(data_rain)             \
  X(error404)              \
  X(reboot)                \
  X(hide)                  \
  X(upgrade)               \
  X(new_year)              \
  X(birthday)              \
  X(halloween)             \
  X(greet)                 \
  X(duel)                  \
  X(chat)                  \
  X(achievement)           \
  X(charge)                \
  X(shutdown)              \
  X(banner)                \
  X(bsod)                  \
  X(visor)                 \
  X(screensaver)

enum class Scene : uint8_t {
#define FACE_X(n) n,
  FACE_SCENE_LIST(FACE_X)
#undef FACE_X
      Count,
  None = 255
};

constexpr int kSceneCount = (int)Scene::Count;

// Подписи и исход: имя импланта для upgrade, позывной для greet, duel и chat,
// value: greet — 1, если знакомый; duel — 1, если победа; chat — 1, если начинаем мы.
// key — ключ секретного разговора (sound.h, chat_key).
// achievement: text — название; charge: value — заряд в процентах (−1 — неизвестен);
// banner: text — крупная строка, sub — мелкая, value — значок (BannerIcon);
// screensaver: text — логотип; error404: sub — вместо «MOOD NOT FOUND».
struct SceneParams {
  char text[16] = {};
  char sub[16] = {};  // вторая строка: например, характер встреченного брелока
  int value = 0;
  uint32_t key = 0;
  void set_text(const char* s) { copy(text, s, 15); }
  void set_sub(const char* s) { copy(sub, s, 15); }

 private:
  static void copy(char* dst, const char* s, int n) {
    int i = 0;
    for (; s && s[i] && i < n; ++i) dst[i] = s[i];
    dst[i] = 0;
  }
};

// Значок под надписью сцены banner.
enum class BannerIcon : uint8_t { Sparkles, Heart, Gift, Star, Rocket, Pi, Skull };

const char* scene_id(Scene s);
const char* scene_name(Scene s);  // на языке lang(), по умолчанию по-русски
const char* scene_name(Scene s, Lang l);
bool scene_from_id(const char* id, Scene* out);
uint16_t scene_duration_ms(Scene s);
// Как часто сцене нужны новые кадры.
uint16_t scene_frame_ms(Scene s, uint32_t t_ms);

// Рисует кадр сцены. face — текущее лицо: его используют сцены, где
// персонаж остаётся на экране (прятки, перезагрузка).
void render_scene(Framebuffer& fb, Scene s, uint32_t t_ms, const SceneParams& p, const Face& face);

}  // namespace face
