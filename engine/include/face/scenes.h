// Сцены — короткие полноэкранные сюжеты вместо лица: загрузка, «взлом»,
// дождь данных, праздники, апгрейд, встреча с другим брелоком, дуэль.
// Запускаются через FaceEngine::play_scene и сами заканчиваются.
#pragma once

#include <stdint.h>

#include "face/framebuffer.h"
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
  X(duel)

enum class Scene : uint8_t {
#define FACE_X(n) n,
  FACE_SCENE_LIST(FACE_X)
#undef FACE_X
      Count,
  None = 255
};

constexpr int kSceneCount = (int)Scene::Count;

// Подписи и исход: имя импланта для upgrade, позывной для greet и duel,
// value: greet — 1, если знакомый; duel — 1, если победа.
struct SceneParams {
  char text[16] = {};
  int value = 0;
  void set_text(const char* s) {
    int i = 0;
    for (; s && s[i] && i < 15; ++i) text[i] = s[i];
    text[i] = 0;
  }
};

const char* scene_id(Scene s);
const char* scene_name(Scene s);  // по-русски
bool scene_from_id(const char* id, Scene* out);
uint16_t scene_duration_ms(Scene s);
// Как часто сцене нужны новые кадры.
uint16_t scene_frame_ms(Scene s, uint32_t t_ms);

// Рисует кадр сцены. face — текущее лицо: его используют сцены, где
// персонаж остаётся на экране (прятки, перезагрузка).
void render_scene(Framebuffer& fb, Scene s, uint32_t t_ms, const SceneParams& p, const Face& face);

}  // namespace face
