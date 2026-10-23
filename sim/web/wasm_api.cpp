// Тонкая обёртка движка для WebAssembly: тот же код, что пойдёт в прошивку.
// Собирается без libc (см. build.sh), поэтому здесь свои memset/memcpy.
#include "face/behavior.h"
#include "face/engine.h"
#include "face/identity.h"
#include "face/progress.h"

#define EXPORT(name) extern "C" __attribute__((export_name(#name)))

using namespace face;

static FaceEngine g_eng;
static Behavior g_bh;
static DirtyWindow g_dirty[kPages];
static int g_dirty_n = 0;

extern "C" {
__attribute__((no_builtin)) void* memset(void* dst, int c, unsigned long n) {
  unsigned char* d = (unsigned char*)dst;
  while (n--) *d++ = (unsigned char)c;
  return dst;
}
__attribute__((no_builtin)) void* memcpy(void* dst, const void* src, unsigned long n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  while (n--) *d++ = *s++;
  return dst;
}
__attribute__((no_builtin)) void* memmove(void* dst, const void* src, unsigned long n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  if (d < s) {
    while (n--) *d++ = *s++;
  } else {
    d += n, s += n;
    while (n--) *--d = *--s;
  }
  return dst;
}
}

EXPORT(fe_init) void fe_init(uint32_t seed, uint32_t now) {
  g_eng.init(seed, now);
  g_bh.init(&g_eng, seed, now);
}
// Выбор эмоции, жеста или сцены на странице — это внимание к брелоку, как кнопка на стенде.
EXPORT(fe_set_expr) void fe_set_expr(int id, uint32_t now, int ms) {
  g_eng.note_activity(now);
  g_eng.set_expression((Expr)id, now, ms);
}
EXPORT(fe_play_clip) void fe_play_clip(int id, uint32_t now) {
  g_eng.note_activity(now);
  g_eng.play_clip((Clip)id, now);
}
EXPORT(fe_look_at) void fe_look_at(float gx, float gy, uint32_t now, uint32_t hold) {
  g_eng.look_at(gx, gy, now, hold);
}
EXPORT(fe_update) int fe_update(uint32_t now) {
  int changed = g_eng.update(now) ? 1 : 0;
  g_dirty_n = changed ? g_eng.dirty(g_dirty) : 0;
  return changed;
}
// Следующее пробуждение — не дальше kMaxSleepMs. JS получает uint32 как знаковое число:
// сравнивать его со временем страницы нужно по разности, как time_reached.
EXPORT(fe_next) uint32_t fe_next() { return g_eng.next_update_ms(); }
EXPORT(fe_frame) const uint8_t* fe_frame() { return g_eng.frame().data; }
EXPORT(fe_current) int fe_current() { return (int)g_eng.expression(); }
EXPORT(fe_clip_active) int fe_clip_active() { return g_eng.clip() == Clip::None ? -1 : (int)g_eng.clip(); }

EXPORT(fe_dirty_count) int fe_dirty_count() { return g_dirty_n; }
// Окно упаковано как page << 16 | x0 << 8 | x1.
EXPORT(fe_dirty_window) uint32_t fe_dirty_window(int i) {
  if (i < 0 || i >= g_dirty_n) return 0;
  return (uint32_t)g_dirty[i].page << 16 | (uint32_t)g_dirty[i].x0 << 8 | g_dirty[i].x1;
}
EXPORT(fe_stats_frames) uint32_t fe_stats_frames() { return g_eng.stats().frames; }
EXPORT(fe_stats_bytes) uint32_t fe_stats_bytes() { return g_eng.stats().bytes; }
EXPORT(fe_stats_renders) uint32_t fe_stats_renders() { return g_eng.stats().renders; }

// 0 — дрейф, 1 — моргание и взгляд, 2 — построчный режим, 3 — вверх ногами.
EXPORT(fe_set_option) void fe_set_option(int opt, int v) {
  EngineOptions& o = g_eng.options();
  switch (opt) {
    case 0: o.drift = v != 0; break;
    case 1: o.idle_motion = v != 0; break;
    case 2: o.scanlines = v != 0; break;
    case 3: o.upside_down = v != 0; break;
    default: break;
  }
}

EXPORT(fe_expr_count) int fe_expr_count() { return kExprCount; }
EXPORT(fe_expr_id) const char* fe_expr_id(int i) { return expression((Expr)i).id; }
EXPORT(fe_expr_name) const char* fe_expr_name(int i) { return expression((Expr)i).name; }
EXPORT(fe_expr_when) const char* fe_expr_when(int i) { return expression((Expr)i).when; }
EXPORT(fe_expr_cat) int fe_expr_cat(int i) { return (int)expression((Expr)i).cat; }
EXPORT(fe_cat_count) int fe_cat_count() { return (int)Category::Count; }
EXPORT(fe_cat_name) const char* fe_cat_name(int c) { return category_name((Category)c); }
EXPORT(fe_clip_count) int fe_clip_count() { return kClipCount; }
EXPORT(fe_clip_id) const char* fe_clip_id(int i) { return clip_id((Clip)i); }

// ---- Поведение ----
EXPORT(fe_event) void fe_event(int id, uint32_t now) { g_bh.on_event((Event)id, now); }
EXPORT(fe_bh_update) void fe_bh_update(uint32_t now) { g_bh.update(now); }
EXPORT(fe_bh_next) uint32_t fe_bh_next() { return g_bh.next_update_ms(); }
EXPORT(fe_bh_auto) void fe_bh_auto(int on) { g_bh.set_auto(on != 0); }
EXPORT(fe_bh_hold_manual) void fe_bh_hold_manual(uint32_t now, uint32_t ms) { g_bh.hold_manual(now, ms); }
EXPORT(fe_bh_set_clock) void fe_bh_set_clock(int minutes) { g_bh.set_clock((uint16_t)minutes); }
EXPORT(fe_bh_reason) const char* fe_bh_reason() { return g_bh.last_reason(); }
// 0 радость, 1 возбуждение, 2 бодрость, 3 скука, 4 раздражение, 5 привязанность
EXPORT(fe_mood) float fe_mood(int i) {
  const Mood& m = g_bh.mood();
  switch (i) {
    case 0: return m.valence;
    case 1: return m.arousal;
    case 2: return m.energy;
    case 3: return m.boredom;
    case 4: return m.annoyance;
    case 5: return m.affection;
    default: return 0;
  }
}
EXPORT(fe_event_count) int fe_event_count() { return kEventCount; }
EXPORT(fe_event_id) const char* fe_event_id(int i) { return event_id((Event)i); }
EXPORT(fe_event_name) const char* fe_event_name(int i) { return event_name((Event)i); }

// ---- Характер, тема, сцены, наклон ----
static Identity g_id;
EXPORT(fe_set_theme) void fe_set_theme(int cyber) { g_eng.options().theme = cyber ? Theme::Cyber : Theme::Classic; }
EXPORT(fe_identity) void fe_identity(uint32_t lo, uint32_t hi, uint32_t now) {
  g_id = make_identity((uint64_t)hi << 32 | lo);
  g_bh.personality() = g_id.personality;
  g_eng.set_quirks(g_id.quirks, now);
}
EXPORT(fe_id_code) const char* fe_id_code() { return g_id.code; }
EXPORT(fe_id_arch) const char* fe_id_arch() { return archetype_name(g_id.archetype); }
EXPORT(fe_id_rarity) const char* fe_id_rarity() { return rarity_name(g_id.rarity); }
EXPORT(fe_id_cyber) int fe_id_cyber() { return g_id.quirks.cyber_eye ? 1 : 0; }
EXPORT(fe_scene_count) int fe_scene_count() { return kSceneCount; }
EXPORT(fe_scene_id) const char* fe_scene_id(int i) { return scene_id((Scene)i); }
EXPORT(fe_scene_name) const char* fe_scene_name(int i) { return scene_name((Scene)i); }
EXPORT(fe_play_scene) void fe_play_scene(int i, uint32_t now) {
  SceneParams p;
  if ((Scene)i == Scene::upgrade) p.set_text(implant(1 + (int)(now / 1000u) % (implant_count() - 1)).tag);
  if ((Scene)i == Scene::greet || (Scene)i == Scene::duel) p.set_text(make_identity(now).code);
  if ((Scene)i == Scene::duel) p.value = (int)(now & 1u);
  if ((Scene)i == Scene::new_year) p.set_text("2027");
  g_eng.note_activity(now);
  g_eng.play_scene((Scene)i, now, p);
}
EXPORT(fe_scene_active) int fe_scene_active() { return g_eng.scene() == Scene::None ? -1 : (int)g_eng.scene(); }
EXPORT(fe_set_tilt) void fe_set_tilt(float x, float y) { g_eng.set_tilt(x, y); }
