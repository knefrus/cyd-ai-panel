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

class SimGfx : public Gfx {
 public:
  std::vector<uint32_t> px = std::vector<uint32_t>(W * H);
  std::vector<Glyph> gl;

  void put(int x, int y, uint32_t c) {
    if (x >= 0 && y >= 0 && x < W && y < H) px[y * W + x] = q565(c);
  }
  void clear(uint32_t c) override { for (auto& p : px) p = q565(c); }
  void rect(int x, int y, int w, int h, uint32_t c) override {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) put(i, j, c);
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
      if (c >= 0xE0) { c = (c & 0x0F) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F); p += 2; }
      else if (c >= 0xC0) { c = (c & 0x1F) << 6 | (p[0] & 0x3F); p += 1; }
      out.push_back(c);
    }
    return out;
  }
  const Glyph* find(int u) {
    for (auto& g : gl) if (g.u == u) return &g;
    fprintf(stderr, "missing glyph U+%04X\n", u);
    return nullptr;
  }
  int textW(const char* s) override {
    int w = 0;
    for (int u : utf8(s)) if (auto g = find(u)) w += g->adv;
    return w;
  }
  static uint32_t mix(uint32_t a, uint32_t b, int t) {
    uint32_t r = 0;
    for (int sh = 0; sh < 24; sh += 8)
      r |= ((((a >> sh) & 255) * t + ((b >> sh) & 255) * (255 - t)) / 255) << sh;
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
  void save(const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    fprintf(f, "P6 %d %d 255\n", W, H);
    for (uint32_t p : px) { uint8_t c[3] = {(uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p}; fwrite(c, 1, 3, f); }
    fclose(f);
  }
};

static void agent(Model& m, const char* name, const char* kind, const char* plan, float s, long sIn,
                  float w, long wIn, uint32_t now, bool ok = true) {
  Agent& a = m.agents[m.n++];
  strcpy(a.name, name);
  strcpy(a.kind, kind);
  strcpy(a.plan, plan);
  a.ok = ok;
  a.s = {s, (uint32_t)(now + sIn), 18000};
  a.w = {w, (uint32_t)(now + wIn), 604800};
  for (int h = 8; h < 24; h++) a.hist[h] = (uint16_t)((h * 7 + m.n * 13 + (int)s) % 23 * (h % 5 ? 1 : 0));
}

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : ".";
  setenv("TZ", "MSK-3", 1);
  tzset();

  UiState st;
  st.timeOk = true;
  st.now = 1793878920;
  time_t t = st.now;
  localtime_r(&t, &st.lt);
  strcpy(st.ip, "192.168.1.42");
  st.focusRun = true;
  st.focusEnd = st.now + 14 * 60 + 27;
  st.focusDone = 2;

  Model m;
  m.updated = st.now - 20;
  agent(m, "Claude", "claude", "pro", 58, 72 * 60, 25, 4 * 86400 + 5 * 3600, st.now);
  agent(m, "Codex", "codex", "plus", 4, 3 * 3600 + 20 * 60, 18, 2 * 86400, st.now);
  m.wx.has = true;
  m.wx.t = 4.3; m.wx.code = 2; m.wx.hi = 4; m.wx.lo = 0;
  int hh[6] = {15, 16, 17, 18, 19, 20}, ht[6] = {4, 4, 3, 2, 1, 0}, hc[6] = {3, 2, 61, 63, 3, 71};
  memcpy(m.wx.hHour, hh, sizeof hh); memcpy(m.wx.hTemp, ht, sizeof ht); memcpy(m.wx.hCode, hc, sizeof hc);
  strcpy(m.wx.sunrise, "07:44"); strcpy(m.wx.sunset, "16:41"); strcpy(m.wx.city, "Moscow");
  m.pc.has = true;
  m.pc.cpu = 3; m.pc.ram = 44; m.pc.disk = 23;
  strcpy(m.pc.name, "following-lime"); strcpy(m.pc.up, "10d 19h");

  SimGfx g;
  const char* names[] = {"home", "activity", "weather", "system", "focus"};
  for (int dark = 0; dark < 2; dark++)
    for (int s = 0; s < SCR_COUNT; s++) {
      st.screen = s;
      st.dark = dark;
      drawScreen(g, m, st);
      g.save(out + "/" + names[s] + (dark ? "_dark.ppm" : "_light.ppm"));
    }

  st.screen = SCR_HOME;
  st.dark = false;
  Model m3 = m;
  agent(m3, "Claude Work", "claude", "max", 91, 40 * 60, 63, 30 * 3600, st.now);
  drawScreen(g, m3, st);
  g.save(out + "/home_3.ppm");
  Model m4 = m3;
  agent(m4, "Codex Team", "codex", "team", 37, 2 * 3600, 52, 5 * 86400, st.now, false);
  drawScreen(g, m4, st);
  g.save(out + "/home_4.ppm");
  st.screen = SCR_ACTIVITY;
  drawScreen(g, m4, st);
  g.save(out + "/activity_4.ppm");
  st.screen = SCR_HOME;
  Model m6 = m4;
  agent(m6, "Claude Alt", "claude", "pro", 12, 4 * 3600, 8, 6 * 86400, st.now);
  agent(m6, "Codex Alt", "codex", "plus", 70, 3600, 33, 3 * 86400, st.now);
  drawScreen(g, m6, st);
  g.save(out + "/home_6.ppm");
  Model m1;
  m1.updated = m.updated;
  m1.wx = m.wx;
  agent(m1, "Claude", "claude", "max", 63, 72 * 60, 41, 4 * 86400, st.now);
  drawScreen(g, m1, st);
  g.save(out + "/home_1.ppm");
  st.dark = true;
  drawScreen(g, m4, st);
  g.save(out + "/home_4_dark.ppm");
  return 0;
}
