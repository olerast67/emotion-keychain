#include "snapshot.h"

#include <string.h>

bool settings_ok(const Settings& s) { return s.magic == Settings{}.magic && s.version == Settings{}.version; }

SnapLoaded snapshot_load(const uint8_t* raw, int n, face::Progress* progress, face::Friends* friends, Settings* cfg) {
  SnapLoaded got;
  if (!raw || n <= 0) return got;
  // Части записи: смещение и длина каждой.
  const uint8_t *pp = nullptr, *fp = nullptr, *sp = nullptr;
  size_t pl = 0, fl = 0, sl = 0;
  SnapHead h;
  if (n >= (int)sizeof h) memcpy(&h, raw, sizeof h);
  if (n >= (int)sizeof h && h.magic == SnapHead{}.magic &&
      sizeof h + (size_t)h.p_len + h.f_len + h.s_len <= (size_t)n) {
    pp = raw + sizeof h, pl = h.p_len;
    fp = pp + pl, fl = h.f_len;
    sp = fp + fl, sl = h.s_len;
  } else if (n == (int)(sizeof(face::Progress) + sizeof(face::Friends) + sizeof(Settings))) {
    // Запись прошивки до заголовка: части подряд.
    pp = raw, pl = sizeof(face::Progress);
    fp = pp + pl, fl = sizeof(face::Friends);
    sp = fp + fl, sl = sizeof(Settings);
  }
  if (!pp) return got;
  got.found = true;
  got.progress = progress->load(pp, (uint32_t)pl);
  got.friends = friends->load(fp, (uint32_t)fl);
  Settings st;
  memcpy(&st, sp, sl < sizeof st ? sl : sizeof st);
  got.settings = settings_ok(st);
  if (got.settings) *cfg = st;
  return got;
}
