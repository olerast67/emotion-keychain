// Тонкая обёртка движка для WebAssembly: тот же код, что пойдёт в прошивку.
// Собирается без libc (см. build.sh), поэтому здесь свои memset/memcpy.
#include "face/engine.h"

#define EXPORT(name) extern "C" __attribute__((export_name(#name)))

using namespace face;

static FaceEngine g_eng;
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

EXPORT(fe_init) void fe_init(uint32_t seed, uint32_t now) { g_eng.init(seed, now); }
EXPORT(fe_set_expr) void fe_set_expr(int id, uint32_t now, int ms) { g_eng.set_expression((Expr)id, now, ms); }
EXPORT(fe_play_clip) void fe_play_clip(int id, uint32_t now) { g_eng.play_clip((Clip)id, now); }
EXPORT(fe_look_at) void fe_look_at(float gx, float gy, uint32_t now, uint32_t hold) {
  g_eng.look_at(gx, gy, now, hold);
}
EXPORT(fe_update) int fe_update(uint32_t now) {
  int changed = g_eng.update(now) ? 1 : 0;
  g_dirty_n = changed ? g_eng.dirty(g_dirty) : 0;
  return changed;
}
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
