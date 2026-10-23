#include "gif_writer.h"

#include <unordered_map>

namespace sim {

namespace {

class BitWriter {
 public:
  explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}
  void write(uint32_t code, int bits) {
    acc_ |= code << nbits_;
    nbits_ += bits;
    while (nbits_ >= 8) {
      out_.push_back((uint8_t)(acc_ & 0xFF));
      acc_ >>= 8;
      nbits_ -= 8;
    }
  }
  void flush() {
    if (nbits_ > 0) out_.push_back((uint8_t)(acc_ & 0xFF));
    acc_ = 0;
    nbits_ = 0;
  }

 private:
  std::vector<uint8_t>& out_;
  uint32_t acc_ = 0;
  int nbits_ = 0;
};

// LZW в варианте GIF; порядок увеличения разрядности как в giflib.
void lzw(const std::vector<uint8_t>& data, int min_code, std::vector<uint8_t>& out) {
  const int clear = 1 << min_code, eoi = clear + 1;
  int next = clear + 2, size = min_code + 1;
  std::unordered_map<uint32_t, int> dict;
  dict.reserve(8192);
  BitWriter bw(out);
  auto emit = [&](int code) {
    bw.write((uint32_t)code, size);
    if (next >= (1 << size) && size < 12) ++size;
  };
  emit(clear);
  if (data.empty()) {
    emit(eoi);
    bw.flush();
    return;
  }
  int cur = data[0];
  for (size_t i = 1; i < data.size(); ++i) {
    const int c = data[i];
    const uint32_t key = ((uint32_t)cur << 8) | (uint32_t)c;
    auto it = dict.find(key);
    if (it != dict.end()) {
      cur = it->second;
      continue;
    }
    emit(cur);
    cur = c;
    if (next >= 4095) {
      emit(clear);
      dict.clear();
      next = clear + 2;
      size = min_code + 1;
    } else {
      dict[key] = next++;
    }
  }
  emit(cur);
  emit(eoi);
  bw.flush();
}

}  // namespace

bool GifWriter::open(const std::string& path, int w, int h, const std::vector<Rgb>& palette, bool loop) {
  f_ = fopen(path.c_str(), "wb");
  if (!f_) return false;
  w_ = w;
  h_ = h;
  bits_ = 1;
  while ((1 << bits_) < (int)palette.size() && bits_ < 8) ++bits_;
  fwrite("GIF89a", 1, 6, f_);
  put16(w);
  put16(h);
  put((uint8_t)(0x80 | ((bits_ - 1) << 4) | (bits_ - 1)));
  put(0);
  put(0);
  for (int i = 0; i < (1 << bits_); ++i) {
    Rgb c = i < (int)palette.size() ? palette[i] : Rgb{0, 0, 0};
    put(c.r);
    put(c.g);
    put(c.b);
  }
  if (loop) {
    put(0x21);
    put(0xFF);
    put(11);
    fwrite("NETSCAPE2.0", 1, 11, f_);
    put(3);
    put(1);
    put16(0);
    put(0);
  }
  has_prev_ = has_pending_ = false;
  return true;
}

void GifWriter::frame(const std::vector<uint8_t>& px, int delay_cs) {
  if (!has_prev_) {
    pending_ = px;
    pending_delay_ = delay_cs;
    px0_ = 0, py0_ = 0, px1_ = w_ - 1, py1_ = h_ - 1;
    has_pending_ = true;
    prev_ = px;
    has_prev_ = true;
    return;
  }
  int x0 = w_, y0 = h_, x1 = -1, y1 = -1;
  for (int y = 0; y < h_; ++y) {
    const uint8_t* a = &prev_[(size_t)y * w_];
    const uint8_t* b = &px[(size_t)y * w_];
    for (int x = 0; x < w_; ++x) {
      if (a[x] != b[x]) {
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
      }
    }
  }
  if (x1 < 0) {
    pending_delay_ += delay_cs;
    return;
  }
  flush_pending();
  pending_ = px;
  pending_delay_ = delay_cs;
  px0_ = x0, py0_ = y0, px1_ = x1, py1_ = y1;
  has_pending_ = true;
  prev_ = px;
}

void GifWriter::flush_pending() {
  if (!has_pending_) return;
  write_image(pending_, px0_, py0_, px1_, py1_, pending_delay_);
  has_pending_ = false;
}

void GifWriter::write_image(const std::vector<uint8_t>& px, int x0, int y0, int x1, int y1, int delay_cs) {
  // Graphic Control Extension: «не стирать» — следующий кадр дорисовывается поверх.
  put(0x21);
  put(0xF9);
  put(4);
  put((uint8_t)(0x04 | (transparent_ >= 0 ? 0x01 : 0x00)));
  put16(delay_cs > 0xFFFF ? 0xFFFF : delay_cs);
  put((uint8_t)(transparent_ >= 0 ? transparent_ : 0));
  put(0);
  const int w = x1 - x0 + 1, h = y1 - y0 + 1;
  put(0x2C);
  put16(x0);
  put16(y0);
  put16(w);
  put16(h);
  put(0);
  std::vector<uint8_t> sub((size_t)w * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) sub[(size_t)y * w + x] = px[(size_t)(y0 + y) * w_ + (x0 + x)];
  const int min_code = bits_ < 2 ? 2 : bits_;
  std::vector<uint8_t> data;
  lzw(sub, min_code, data);
  put((uint8_t)min_code);
  for (size_t i = 0; i < data.size(); i += 255) {
    size_t n = data.size() - i < 255 ? data.size() - i : 255;
    put((uint8_t)n);
    fwrite(&data[i], 1, n, f_);
  }
  put(0);
}

bool GifWriter::close() {
  if (!f_) return false;
  flush_pending();
  put(0x3B);
  fclose(f_);
  f_ = nullptr;
  return true;
}

}  // namespace sim
