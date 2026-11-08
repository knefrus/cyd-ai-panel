#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "ui.h"
#include "fonts.h"

static const int W = 320, H = 240;

static uint32_t q565(uint32_t c) {
  uint32_t r = (c >> 16) & 0xF8, g = (c >> 8) & 0xFC, b = c & 0xF8;
  return (r | r >> 5) << 16 | (g | g >> 6) << 8 | (b | b >> 5);
}

struct Glyph { int u, h, w, adv, dy, dx; const uint8_t* bmp; };

struct Asset {
  std::string id;
  int w = 0, h = 0, n = 0;
  std::vector<uint16_t> delay;
  uint16_t pal[256];
  std::vector<uint8_t> px;
  uint32_t total = 0;
};

class SimGfx : public Gfx {
 public:
  std::vector<uint32_t> px = std::vector<uint32_t>(W * H);
  std::vector<Glyph> gl;
  std::string assets = ".";
  std::vector<Asset> cache;

  void put(int x, int y, uint32_t c) {
    if (x >= 0 && y >= 0 && x < W && y < H) px[y * W + x] = q565(c);
  }
  void clear(uint32_t c) override { for (auto& p : px) p = q565(c); }
  void rect(int x, int y, int w, int h, uint32_t c) override {
    for (int j = y; j < y + h; j++)
      for (int i = x; i < x + w; i++) put(i, j, c);
  }
  void rrect(int x, int y, int w, int h, int r, uint32_t c) override {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        int cx = i < r ? r - 1 : (i >= w - r ? w - r : i);
        int cy = j < r ? r - 1 : (j >= h - r ? h - r : j);
        int dx = i - cx, dy = j - cy;
        if (dx * dx + dy * dy <= r * r - r / 2) put(x + i, y + j, c);
      }
  }
  void circle(int x, int y, int r, uint32_t c) override {
    for (int j = -r; j <= r; j++)
      for (int i = -r; i <= r; i++)
        if (i * i + j * j <= r * r + r) put(x + i, y + j, c);
  }
  static int be(const uint8_t* p) { return (int)((uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); }
  void font(const uint8_t* f) override {
    gl.clear();
    int n = be(f);
    const uint8_t* bm = f + 24 + n * 28;
    for (int i = 0; i < n; i++) {
      const uint8_t* m = f + 24 + i * 28;
      Glyph g{be(m), be(m + 4), be(m + 8), be(m + 12), be(m + 16), be(m + 20), bm};
      bm += g.w * g.h;
      gl.push_back(g);
    }
  }
  static std::vector<int> utf8(const char* s) {
    std::vector<int> out;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
      int c = *p++;
      if (c >= 0xE0 && p[0] && p[1]) { c = (c & 0x0F) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F); p += 2; }
      else if (c >= 0xC0 && p[0]) { c = (c & 0x1F) << 6 | (p[0] & 0x3F); p += 1; }
      out.push_back(c);
    }
    return out;
  }
  const Glyph* find(int u) {
    for (auto& g : gl)
      if (g.u == u) return &g;
    return nullptr;
  }
  int textW(const char* s) override {
    int w = 0;
    for (int u : utf8(s))
      if (auto g = find(u)) w += g->adv;
    return w;
  }
  static uint32_t mix(uint32_t a, uint32_t b, int t) {
    uint32_t r = 0;
    for (int sh = 0; sh < 24; sh += 8) r |= ((((a >> sh) & 255) * t + ((b >> sh) & 255) * (255 - t)) / 255) << sh;
    return r;
  }
  void text(const char* s, int x, int y, int al, uint32_t fg, uint32_t bg) override {
    int w = textW(s);
    if (al == 1) x -= w / 2;
    if (al == 2) x -= w;
    for (int u : utf8(s)) {
      const Glyph* g = find(u);
      if (!g) continue;
      for (int j = 0; j < g->h; j++)
        for (int i = 0; i < g->w; i++)
          if (int a = g->bmp[j * g->w + i]) put(x + g->dx + i, y - g->dy + j, mix(fg, bg, a));
      x += g->adv;
    }
  }
  Asset* load(const char* id) {
    for (auto& a : cache)
      if (a.id == id) return &a;
    std::string path = assets + "/" + id + ".bin";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    Asset a;
    a.id = id;
    uint8_t hd[10];
    if (fread(hd, 1, 10, f) != 10 || memcmp(hd, "CYDG", 4)) { fclose(f); return nullptr; }
    a.w = hd[4] | hd[5] << 8;
    a.h = hd[6] | hd[7] << 8;
    a.n = hd[8] | hd[9] << 8;
    a.delay.resize(a.n);
    for (int i = 0; i < a.n; i++) {
      uint8_t d[2];
      if (fread(d, 1, 2, f) != 2) break;
      a.delay[i] = d[0] | d[1] << 8;
      a.total += a.delay[i];
    }
    uint8_t p[512];
    if (fread(p, 1, 512, f) != 512) { fclose(f); return nullptr; }
    for (int i = 0; i < 256; i++) a.pal[i] = p[i * 2] | p[i * 2 + 1] << 8;
    a.px.resize((size_t)a.w * a.h * a.n);
    if (fread(a.px.data(), 1, a.px.size(), f) != a.px.size()) { fclose(f); return nullptr; }
    fclose(f);
    cache.push_back(a);
    return &cache.back();
  }
  bool gif(const char* id, int x, int y, int w, int h, uint32_t ms, uint32_t bg) override {
    Asset* a = load(id);
    if (!a) return false;
    uint32_t t = a->total ? ms % a->total : 0;
    int fr = 0;
    while (fr < a->n - 1 && t >= a->delay[fr]) t -= a->delay[fr++];
    int ox = x + (w - a->w) / 2, oy = y + (h - a->h) / 2;
    const uint8_t* f = a->px.data() + (size_t)fr * a->w * a->h;
    for (int j = 0; j < a->h; j++)
      for (int i = 0; i < a->w; i++) {
        uint8_t k = f[j * a->w + i];
        if (k == 255) continue;
        uint16_t c = a->pal[k];
        uint32_t r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
        put(ox + i, oy + j, r << 16 | g << 8 | b);
      }
    return true;
  }
  void save(const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    int row = W * 3, size = 54 + row * H;
    uint8_t hd[54] = {'B', 'M'};
    auto le = [&](int off, uint32_t v) { for (int i = 0; i < 4; i++) hd[off + i] = v >> (8 * i); };
    le(2, size); le(10, 54); le(14, 40); le(18, W); le(22, (uint32_t)-H);
    hd[26] = 1; hd[28] = 24;
    le(34, row * H);
    fwrite(hd, 1, 54, f);
    std::vector<uint8_t> line(row);
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        uint32_t p = px[y * W + x];
        line[x * 3] = p;
        line[x * 3 + 1] = p >> 8;
        line[x * 3 + 2] = p >> 16;
      }
      fwrite(line.data(), 1, row, f);
    }
    fclose(f);
  }
};

static std::string readAll(const char* path) {
  FILE* f = strcmp(path, "-") ? fopen(path, "rb") : stdin;
  if (!f) return "";
  std::string s;
  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  if (f != stdin) fclose(f);
  return s;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: render panel.json out.bmp [--screen N] [--dark 1] [--now T] [--ms T] [--tz TZ] [--assets DIR]\n");
    return 1;
  }
  static Model m;
  UiState st;
  SimGfx g;
  const char* tz = "MSK-3";
  long now = 0;
  for (int i = 3; i + 1 < argc; i += 2) {
    std::string k = argv[i];
    const char* v = argv[i + 1];
    if (k == "--screen") st.screen = atoi(v);
    else if (k == "--dark") st.dark = atoi(v);
    else if (k == "--now") now = atol(v);
    else if (k == "--ms") st.ms = atol(v);
    else if (k == "--tz") tz = v;
    else if (k == "--assets") g.assets = v;
    else if (k == "--focus") { st.focusRun = true; st.focusLeft = atoi(v); }
  }
  std::string js = readAll(argv[1]);
  if (!parseModel(js.data(), js.size(), m)) {
    fprintf(stderr, "bad json\n");
    return 2;
  }
  if (*m.set.tz) tz = m.set.tz;
  setenv("TZ", tz, 1);
  tzset();
  st.now = now ? now : time(nullptr);
  st.timeOk = true;
  m.updated = st.now;
  time_t t = st.now;
  localtime_r(&t, &st.lt);
  strcpy(st.ip, "192.168.1.42");
  if (st.focusRun) st.focusEnd = st.now + st.focusLeft;
  drawScreen(g, m, st);
  g.save(argv[2]);
  return 0;
}
