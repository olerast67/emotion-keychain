#include "face/framebuffer.h"

namespace face {

void Framebuffer::clear() { fill(0); }

void Framebuffer::fill(uint8_t pattern) {
  for (int i = 0; i < kFrameBytes; ++i) data[i] = pattern;
}

void Framebuffer::copy_from(const Framebuffer& other) {
  for (int i = 0; i < kFrameBytes; ++i) data[i] = other.data[i];
}

void Framebuffer::hspan(int y, int x0, int x1, Ink ink) {
  if ((unsigned)y >= (unsigned)kHeight) return;
  if (x0 > x1) {
    int t = x0;
    x0 = x1;
    x1 = t;
  }
  if (x1 < 0 || x0 >= kWidth) return;
  if (x0 < 0) x0 = 0;
  if (x1 >= kWidth) x1 = kWidth - 1;
  uint8_t* row = data + (y >> 3) * kWidth;
  const uint8_t m = (uint8_t)(1u << (y & 7));
  switch (ink) {
    case Ink::Set:
      for (int x = x0; x <= x1; ++x) row[x] |= m;
      break;
    case Ink::Clear:
      for (int x = x0; x <= x1; ++x) row[x] &= (uint8_t)~m;
      break;
    case Ink::Invert:
      for (int x = x0; x <= x1; ++x) row[x] ^= m;
      break;
  }
}

int Framebuffer::count_lit() const {
  int n = 0;
  for (int i = 0; i < kFrameBytes; ++i) {
    uint8_t b = data[i];
    while (b) {
      b &= (uint8_t)(b - 1);
      ++n;
    }
  }
  return n;
}

bool Framebuffer::equals(const Framebuffer& other) const {
  for (int i = 0; i < kFrameBytes; ++i)
    if (data[i] != other.data[i]) return false;
  return true;
}

static uint8_t reverse_bits(uint8_t b) {
  b = (uint8_t)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
  b = (uint8_t)((b & 0xCC) >> 2 | (b & 0x33) << 2);
  b = (uint8_t)((b & 0xAA) >> 1 | (b & 0x55) << 1);
  return b;
}

void Framebuffer::rotate180() {
  for (int i = 0, j = kFrameBytes - 1; i < j; ++i, --j) {
    uint8_t a = reverse_bits(data[i]);
    data[i] = reverse_bits(data[j]);
    data[j] = a;
  }
}

int diff_frames(const Framebuffer& prev, const Framebuffer& next, DirtyWindow out[kPages]) {
  int n = 0;
  for (int p = 0; p < kPages; ++p) {
    const uint8_t* a = prev.data + p * kWidth;
    const uint8_t* b = next.data + p * kWidth;
    int x0 = -1, x1 = -1;
    for (int x = 0; x < kWidth; ++x) {
      if (a[x] != b[x]) {
        if (x0 < 0) x0 = x;
        x1 = x;
      }
    }
    if (x0 >= 0) out[n++] = DirtyWindow{(uint8_t)p, (uint8_t)x0, (uint8_t)x1};
  }
  return n;
}

int dirty_bytes(const DirtyWindow* windows, int count) {
  int bytes = 0;
  for (int i = 0; i < count; ++i) bytes += windows[i].x1 - windows[i].x0 + 1;
  return bytes;
}

bool dither_pass(int x, int y, float alpha) {
  static const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  if (alpha <= 0.0f) return false;
  if (alpha >= 1.0f) return true;
  return (float)kBayer[y & 3][x & 3] < alpha * 16.0f;
}

}  // namespace face
