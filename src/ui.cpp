#include "ui.h"
#include "fonts.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

struct Theme {
  uint32_t bg, card, line, ink, muted, faint, empty, gray, chip, warn, claude, live, amber;
  uint32_t sun, cloud, rain;
};

static const Theme LIGHT = {0xF1F1EF, 0xFFFFFF, 0xE3E3E0, 0x151515, 0x8A8A86, 0xB9B9B4, 0xECECE9,
                            0xA8A8A3, 0xF2F2F0, 0xE5533D, 0xD97757, 0x2FA36B, 0xE8A93A,
                            0xF2B33D, 0xCFCFCB, 0x4F7FE0};
static const Theme DARK = {0x0D0D0D, 0x171717, 0x262626, 0xECECEC, 0x7A7A7A, 0x4A4A4A, 0x242424,
                           0x5C5C5C, 0x212121, 0xF0654F, 0xD97757, 0x3DBE7E, 0xE8A93A,
                           0xE8A93A, 0x3E3E3E, 0x5B8AEA};

static const char* WD[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* WDs[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* WDl[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char* MON[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

struct Ctx {
  Gfx& g;
  const Theme& T;
  const Model& m;
  const UiState& st;
  const Widget& w;
  int x, y, W, H, p;
  uint32_t bg;
  bool card;
};

static int u8len(uint8_t c) { return c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1; }

static void upper(char* dst, const char* src, size_t n) {
  size_t i = 0;
  const uint8_t* s = (const uint8_t*)src;
  while (*s && i + 2 < n) {
    uint8_t a = s[0], b = s[1];
    if (a >= 'a' && a <= 'z') { dst[i++] = a - 32; s++; continue; }
    if (a == 0xD0 && b >= 0xB0 && b <= 0xBF) { dst[i++] = 0xD0; dst[i++] = b - 0x20; s += 2; continue; }
    if (a == 0xD1 && b >= 0x80 && b <= 0x8F) { dst[i++] = 0xD0; dst[i++] = b + 0x20; s += 2; continue; }
    if (a == 0xD1 && b == 0x91) { dst[i++] = 0xD0; dst[i++] = 0x81; s += 2; continue; }
    int l = u8len(a);
    if (i + l >= n) break;
    memcpy(dst + i, s, l);
    i += l;
    s += l;
  }
  dst[i] = 0;
}

static void fit(Gfx& g, char* s, int maxw) {
  if (maxw <= 0) { s[0] = 0; return; }
  if (g.textW(s) <= maxw) return;
  size_t n = strlen(s);
  char buf[160];
  while (n > 0) {
    n--;
    while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
    size_t k = n;
    while (k > 0 && s[k - 1] == ' ') k--;
    if (k > sizeof buf - 4) k = sizeof buf - 4;
    memcpy(buf, s, k);
    memcpy(buf + k, "\xE2\x80\xA6", 4);
    if (g.textW(buf) <= maxw) break;
  }
  strcpy(s, n ? buf : "");
}

static int wrap(Gfx& g, const char* text, int maxw, char lines[][96], int maxLines, bool* cut) {
  int n = 0;
  char cur[96] = "";
  *cut = false;
  const char* p = text;
  while (*p) {
    if (*p == '\n') {
      if (n == maxLines) { *cut = true; return n; }
      strcpy(lines[n++], cur);
      cur[0] = 0;
      p++;
      continue;
    }
    const char* e = p;
    while (*e && *e != ' ' && *e != '\n') e++;
    char word[96];
    size_t wl = e - p < 95 ? e - p : 95;
    memcpy(word, p, wl);
    word[wl] = 0;
    char trial[192];
    snprintf(trial, sizeof trial, "%s%s%s", cur, cur[0] ? " " : "", word);
    if (g.textW(trial) <= maxw && strlen(trial) < 95) {
      strcpy(cur, trial);
    } else if (cur[0]) {
      if (n == maxLines) { *cut = true; return n; }
      strcpy(lines[n++], cur);
      strcpy(cur, word);
      if (g.textW(cur) > maxw) fit(g, cur, maxw);
    } else {
      strcpy(cur, word);
      fit(g, cur, maxw);
    }
    p = e;
    while (*p == ' ') p++;
  }
  if (cur[0]) {
    if (n == maxLines) { *cut = true; return n; }
    strcpy(lines[n++], cur);
  }
  return n;
}

static void card(Gfx& g, const Theme& T, int x, int y, int w, int h) {
  int r = h < 60 ? 9 : 10;
  g.rrect(x, y, w, h, r, T.line);
  g.rrect(x + 1, y + 1, w - 2, h - 2, r - 1, T.card);
}

static void mono(Ctx& c, const char* s, int x, int y, int al, uint32_t col = 0xFFFFFFFF) {
  c.g.font(F_MONO);
  c.g.text(s, x, y, al, col == 0xFFFFFFFF ? c.T.muted : col, c.bg);
}

static void txt(Ctx& c, const uint8_t* f, const char* s, int x, int y, int al, uint32_t col) {
  c.g.font(f);
  c.g.text(s, x, y, al, col, c.bg);
}

static void blocks(Gfx& g, int x, int y, int w, int h, float pct, int n, uint32_t on, uint32_t off,
                   float pace = -1, uint32_t paceC = 0) {
  const int gap = 2;
  if (n < 1) n = 1;
  int cw = (w + gap) / n - gap;
  while (cw < 2 && n > 4) {
    n /= 2;
    cw = (w + gap) / n - gap;
  }
  if (cw < 1) cw = 1;
  int used = n * cw + (n - 1) * gap;
  int x0 = x + (w - used) / 2;
  for (int i = 0; i < n; i++) g.rect(x0 + i * (cw + gap), y, cw, h, pct >= (i + 0.5f) * 100.f / n ? on : off);
  if (pace >= 0 && pace <= 100) g.rect(x0 + (int)roundf(pace / 100.f * used) - 1, y + h + 2, 2, 3, paceC);
}

static void fmtDur(char* b, size_t n, long sec) {
  if (sec < 0) sec = 0;
  long d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60;
  if (d > 0) snprintf(b, n, "%ldd %ldh", d, h);
  else if (h > 0) snprintf(b, n, "%ldh %02ldm", h, m);
  else snprintf(b, n, "%ldm", m < 1 ? 1 : m);
}

static void fmtReset(char* b, size_t n, const Window& w, const UiState& st, bool shortForm = false) {
  if (!w.reset || !st.timeOk) { snprintf(b, n, "-"); return; }
  long left = (long)w.reset - (long)st.now;
  if (left < 86400) {
    char d[16];
    fmtDur(d, sizeof d, left);
    snprintf(b, n, shortForm ? "%s" : "in %s", d);
  } else {
    time_t t = w.reset;
    struct tm r;
    localtime_r(&t, &r);
    snprintf(b, n, "%s %02d:%02d", shortForm ? WD[r.tm_wday] : WDs[r.tm_wday], r.tm_hour, r.tm_min);
  }
}

static void hhmm(char* b, size_t n, uint32_t t) {
  time_t tt = t;
  struct tm r;
  localtime_r(&tt, &r);
  snprintf(b, n, "%02d:%02d", r.tm_hour, r.tm_min);
}

static int dayOf(uint32_t t) {
  time_t tt = t;
  struct tm r;
  localtime_r(&tt, &r);
  return r.tm_year * 400 + r.tm_yday;
}

static bool fresh(const Model& m, const UiState& st) { return m.updated && st.now - m.updated < 180; }

static void center(Ctx& c, const char* main, const char* sub) {
  int cy = c.y + c.H / 2;
  txt(c, F_TITLE, main, c.x + c.W / 2, sub ? cy : cy + 5, 1, c.T.muted);
  if (sub) {
    c.g.font(F_MONO);
    if (c.g.textW(sub) <= c.W - 16) mono(c, sub, c.x + c.W / 2, cy + 16, 1, c.T.faint);
  }
}

// ---------- weather icons ----------
enum Wx { X_SUN, X_PARTLY, X_CLOUD, X_FOG, X_RAIN, X_SNOW, X_STORM };

static Wx wxKind(int c) {
  if (c == 0) return X_SUN;
  if (c <= 2) return X_PARTLY;
  if (c == 3) return X_CLOUD;
  if (c == 45 || c == 48) return X_FOG;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return X_SNOW;
  if (c >= 95) return X_STORM;
  return X_RAIN;
}

static const char* wxText(int c) {
  switch (wxKind(c)) {
    case X_SUN: return "Clear";
    case X_PARTLY: return "Partly cloudy";
    case X_CLOUD: return "Overcast";
    case X_FOG: return "Fog";
    case X_SNOW: return "Snow";
    case X_STORM: return "Thunderstorm";
    default: return (c >= 51 && c <= 57) ? "Drizzle" : "Rain";
  }
}

static void cloud(Gfx& g, int cx, int cy, float s, uint32_t c) {
  g.circle(cx - (int)(7 * s), cy + (int)(1 * s), (int)(6 * s), c);
  g.circle(cx + (int)(1 * s), cy - (int)(3 * s), (int)(8 * s), c);
  g.circle(cx + (int)(9 * s), cy + (int)(2 * s), (int)(5 * s), c);
  g.rrect(cx - (int)(13 * s), cy + (int)(1 * s), (int)(27 * s), (int)(6 * s), (int)(3 * s), c);
}

static void wxIcon(Gfx& g, const Theme& T, int code, bool day, int cx, int cy, float s, uint32_t bg) {
  Wx k = wxKind(code);
  if (k == X_SUN) {
    g.circle(cx, cy, (int)(9 * s), T.sun);
    if (!day) g.circle(cx + (int)(5 * s), cy - (int)(4 * s), (int)(8 * s), bg);
    return;
  }
  if (k == X_PARTLY) {
    g.circle(cx + (int)(5 * s), cy - (int)(6 * s), (int)(7 * s), day ? T.sun : T.faint);
    cloud(g, cx - (int)(1 * s), cy + (int)(2 * s), s, bg);
    cloud(g, cx - (int)(2 * s), cy + (int)(3 * s), s * 0.92f, T.cloud);
    return;
  }
  if (k == X_FOG) {
    for (int i = 0; i < 3; i++)
      g.rrect(cx - (int)(12 * s) + (i % 2) * (int)(4 * s), cy - (int)(7 * s) + i * (int)(6 * s),
              (int)(22 * s), (int)(3 * s), 1, T.cloud);
    return;
  }
  cloud(g, cx, cy - (int)(3 * s), s, T.cloud);
  int y0 = cy + (int)(8 * s);
  if (k == X_RAIN)
    for (int i = -1; i <= 1; i++) g.rrect(cx + i * (int)(7 * s) - 1, y0, 2, (int)(6 * s), 1, T.rain);
  if (k == X_SNOW)
    for (int i = -1; i <= 1; i++) g.circle(cx + i * (int)(7 * s), y0 + (int)(3 * s), (int)(1.6f * s) + 1, T.muted);
  if (k == X_STORM) {
    g.rect(cx, y0, (int)(3 * s), (int)(4 * s), T.sun);
    g.rect(cx - (int)(2 * s), y0 + (int)(3 * s), (int)(3 * s), (int)(4 * s), T.sun);
  }
}

// ---------- clock ----------
static void wClock(Ctx& c) {
  char t[8], d[32];
  const UiState& st = c.st;
  if (st.timeOk) snprintf(t, sizeof t, "%02d:%02d", st.lt.tm_hour, st.lt.tm_min);
  else snprintf(t, sizeof t, "--:--");
  if (st.timeOk) snprintf(d, sizeof d, "%s \xC2\xB7 %02d %s", WD[st.lt.tm_wday], st.lt.tm_mday, MON[st.lt.tm_mon]);
  else snprintf(d, sizeof d, "SYNCING TIME");
  uint32_t dot = fresh(c.m, st) ? c.T.live : c.T.faint;
  int l = c.x + c.p, r = c.x + c.W - c.p;

  if (c.H >= 100) {
    bool big = c.W >= 180;
    int cap = big ? 42 : 29, top = c.y + (c.H - cap - 22) / 2;
    int al = big && c.w.align != 1 ? 0 : 1;
    int ax = al ? c.x + c.W / 2 : l;
    txt(c, big ? F_HUGE : F_CLOCK, t, ax, top + cap, al, c.T.ink);
    c.g.font(F_MONO);
    int tw = c.g.textW(d);
    mono(c, d, al ? ax + 6 : ax + 10, top + cap + 22, al);
    c.g.circle(al ? ax - tw / 2 : ax + 3, top + cap + 19, 2, dot);
    return;
  }
  if (c.W >= 220) {
    txt(c, F_CLOCK, t, l, c.y + c.H / 2 + 15, 0, c.T.ink);
    c.g.font(F_MONO);
    int tw = c.g.textW(d);
    mono(c, d, r, c.y + c.H / 2 - 6, 2);
    c.g.circle(r - tw - 7, c.y + c.H / 2 - 9, 2, dot);
    if (st.timeOk) txt(c, F_TITLE, WDl[st.lt.tm_wday], r, c.y + c.H / 2 + 13, 2, c.T.ink);
    return;
  }
  bool small = c.card || c.W < 120;
  mono(c, d, l + 8, c.y + (c.card ? 17 : 9), 0);
  c.g.circle(l + 2, c.y + (c.card ? 14 : 6), 2, dot);
  txt(c, small ? F_BIG : F_CLOCK, t, l, c.y + c.H - (c.card ? 9 : 3), 0, c.T.ink);
}

// ---------- weather ----------
static void hourly(Ctx& c, int top, int n) {
  const Weather& w = c.m.wx;
  int colw = c.W / n;
  char b[16];
  for (int i = 0; i < n; i++) {
    int cx = c.x + colw * i + colw / 2;
    snprintf(b, sizeof b, "%02d", w.hHour[i]);
    mono(c, i == 0 ? "NOW" : b, cx, top + 18, 1);
    wxIcon(c.g, c.T, w.hCode[i], w.hHour[i] >= 7 && w.hHour[i] <= 18, cx, top + 40, 0.8f, c.bg);
    snprintf(b, sizeof b, "%d\xC2\xB0", w.hTemp[i]);
    txt(c, F_BODY, b, cx + 2, top + 70, 1, c.T.ink);
  }
}

static void wWeather(Ctx& c) {
  const Weather& w = c.m.wx;
  if (!w.has) { center(c, "No weather yet", nullptr); return; }
  char b[48], city[40];
  upper(city, w.city, sizeof city);
  int l = c.x + c.p, r = c.x + c.W - c.p;

  if (c.H < 60) {
    int al = c.w.align == 2 ? 2 : 0;
    snprintf(b, sizeof b, "%s \xC2\xB7 H %d\xC2\xB0 L %d\xC2\xB0", city, w.hi, w.lo);
    c.g.font(F_MONO);
    fit(c.g, b, c.W - 2 * c.p);
    mono(c, b, al ? r : l, c.y + (c.card ? 17 : 9), al);
    snprintf(b, sizeof b, "%d\xC2\xB0  %s", (int)roundf(w.t), wxText(w.code));
    c.g.font(F_TITLE);
    fit(c.g, b, c.W - 2 * c.p - 26);
    int tw = c.g.textW(b);
    int by = c.y + c.H - (c.card ? 11 : 5);
    int tx = al ? r : l + 24;
    txt(c, F_TITLE, b, tx, by, al, c.T.ink);
    wxIcon(c.g, c.T, w.code, w.day, al ? r - tw - 13 : l + 9, by - 5, 0.5f, c.bg);
    return;
  }

  bool wide = c.W >= 250;
  int top = c.y + (c.card ? 6 : 0);
  bool full = c.H >= 150;
  snprintf(b, sizeof b, "%d\xC2\xB0", (int)roundf(w.t));
  bool huge = c.W >= 140;
  int tb = top + (full ? 72 : 52);
  if (full) {
    mono(c, city, l, top + 14, 0);
    if (c.st.timeOk && wide) {
      snprintf(b + 16, 30, "%s \xC2\xB7 %02d:%02d", WD[c.st.lt.tm_wday], c.st.lt.tm_hour, c.st.lt.tm_min);
      mono(c, b + 16, r, top + 14, 2);
    }
  }
  txt(c, huge ? F_HUGE : F_BIG, b, l - (huge ? 3 : 0), tb, 0, c.T.ink);
  txt(c, F_TITLE, wxText(w.code), l, tb + (huge ? 22 : 18), 0, c.T.ink);
  snprintf(b, sizeof b, "H %d\xC2\xB0   L %d\xC2\xB0", w.hi, w.lo);
  txt(c, F_BODY, b, l, tb + (huge ? 39 : 34), 0, c.T.muted);
  float s = wide ? 2.0f : (c.W >= 140 ? 1.3f : 0.9f);
  wxIcon(c.g, c.T, w.code, w.day, r - (int)(22 * s), top + (full ? 48 : 30), s, c.bg);
  if (wide && full) {
    snprintf(b, sizeof b, "\xE2\x86\x91 %s   \xE2\x86\x93 %s", w.sunrise, w.sunset);
    mono(c, b, r, tb + 39, 2);
  }
  if (!full) return;
  int ht = c.y + c.H - 80;
  if (!c.card) {
    card(c.g, c.T, c.x, ht, c.W, 80);
    Ctx in = c;
    in.bg = c.T.card;
    hourly(in, ht, wide ? 6 : 3);
  } else {
    c.g.rect(l, ht - 2, c.W - 2 * c.p, 1, c.T.line);
    hourly(c, ht - 4, wide ? 6 : 3);
  }
}

// ---------- agents ----------
static uint32_t agentColor(const Agent& a, const Theme& T) {
  if (a.color) return a.color;
  if (!strcmp(a.kind, "claude")) return T.claude;
  return T.ink;
}

static uint32_t barColor(const Agent& a, float pct, const Theme& T) {
  if (!a.ok) return T.faint;
  return pct >= 90 ? T.warn : T.ink;
}

static int chip(Ctx& c, const Agent& a, int right, int y) {
  char up[12];
  upper(up, a.ok ? a.plan : "STALE", sizeof up);
  if (!*up) return 0;
  c.g.font(F_MONO);
  int tw = c.g.textW(up);
  int cx = right - tw - 12;
  c.g.rrect(cx, y, tw + 12, 16, 8, a.ok ? c.T.chip : c.T.warn);
  c.g.text(up, cx + 6 + tw / 2, y + 11, 1, a.ok ? c.T.muted : c.T.card, a.ok ? c.T.chip : c.T.warn);
  return tw + 12;
}

static void pctStr(char* b, size_t n, float p) {
  if (p < 0) snprintf(b, n, "-");
  else snprintf(b, n, "%d%%", (int)roundf(p));
}

static void section(Ctx& c, int x, int y, int w, const char* label, const Window& win, const Agent& a, bool pace) {
  char b[24];
  mono(c, label, x, y + 8, 0);
  pctStr(b, sizeof b, win.pct);
  txt(c, F_BIG, b, x, y + 33, 0, a.ok ? c.T.ink : c.T.muted);
  fmtReset(b, sizeof b, win, c.st);
  txt(c, F_BODY, b, x + w, y + 32, 2, c.T.muted);
  float p = -1;
  if (pace && win.len && win.reset && c.st.timeOk) p = 100.f * (1.f - (float)((long)win.reset - (long)c.st.now) / win.len);
  blocks(c.g, x, y + 39, w, 9, win.pct, w > 200 ? 30 : 20, barColor(a, win.pct, c.T), c.T.empty, p, c.T.muted);
}

static void agentHead(Ctx& c, const Agent& a, int y) {
  int l = c.x + c.p, r = c.x + c.W - c.p;
  c.g.circle(l + 4, y - 5, 4, agentColor(a, c.T));
  char up[12];
  upper(up, a.ok ? a.plan : "STALE", sizeof up);
  c.g.font(F_MONO);
  int cw = *up && c.W >= 120 ? c.g.textW(up) + 12 : 0;
  char name[40];
  snprintf(name, sizeof name, "%s", a.name);
  c.g.font(F_TITLE);
  fit(c.g, name, r - l - 14 - cw - 6);
  txt(c, F_TITLE, name, l + 14, y, 0, c.T.ink);
  if (cw) chip(c, a, r, y - 13);
}

static void wAgent(Ctx& c) {
  if (c.w.i >= c.m.n) {
    if (c.m.n == 0) center(c, "Waiting for data", c.st.ip);
    else center(c, "No such account", nullptr);
    return;
  }
  const Agent& a = c.m.agents[c.w.i];
  int l = c.x + c.p, r = c.x + c.W - c.p, iw = r - l;
  char b[24];
  if (c.H >= 150) {
    agentHead(c, a, c.y + 24);
    section(c, l, c.y + 38, iw, "SESSION 5H", a.s, a, false);
    c.g.rect(l, c.y + 98, iw, 1, c.T.line);
    section(c, l, c.y + 105, iw, "WEEK", a.w, a, true);
    return;
  }
  if (c.H >= 100) {
    agentHead(c, a, c.y + 24);
    if (c.W >= 140) {
      int cw = (iw - 12) / 2;
      const Window* ws[2] = {&a.s, &a.w};
      bool roomy = cw >= 110;
      const char* lb[2] = {roomy ? "SESSION" : "5H", roomy ? "WEEK" : "WK"};
      for (int k = 0; k < 2; k++) {
        int x = l + k * (cw + 12);
        mono(c, lb[k], x, c.y + 46, 0);
        if (roomy) fmtReset(b, sizeof b, *ws[k], c.st, true);
        else if (ws[k]->reset && c.st.timeOk) fmtDur(b, sizeof b, (long)ws[k]->reset - (long)c.st.now);
        else snprintf(b, sizeof b, "-");
        mono(c, b, x + cw, c.y + 46, 2);
        pctStr(b, sizeof b, ws[k]->pct);
        txt(c, F_BIG, b, x, c.y + 73, 0, a.ok ? c.T.ink : c.T.muted);
        blocks(c.g, x, c.y + 81, cw, 7, ws[k]->pct, 10, barColor(a, ws[k]->pct, c.T), c.T.empty);
      }
    } else {
      const Window* ws[2] = {&a.s, &a.w};
      const char* lb[2] = {"5H", "WK"};
      for (int k = 0; k < 2; k++) {
        int y = c.y + 44 + k * 28;
        mono(c, lb[k], l, y, 0);
        pctStr(b, sizeof b, ws[k]->pct);
        txt(c, F_TITLE, b, r, y + 1, 2, a.ok ? c.T.ink : c.T.muted);
        blocks(c.g, l, y + 6, iw, 6, ws[k]->pct, 10, barColor(a, ws[k]->pct, c.T), c.T.empty);
      }
    }
    return;
  }
  int y = c.y + (c.card ? 21 : 16);
  c.g.circle(l + 4, y - 5, 4, agentColor(a, c.T));
  char pb[24], wb[16], name[40];
  pctStr(pb, sizeof pb, a.s.pct);
  pctStr(wb, sizeof wb, a.w.pct);
  snprintf(name, sizeof name, "%s", a.name);
  int by = c.y + c.H - (c.card ? 17 : 13);
  if (c.W < 140) {
    c.g.font(F_TITLE);
    fit(c.g, name, iw - 14);
    txt(c, F_TITLE, name, l + 14, y, 0, c.T.ink);
    snprintf(b, sizeof b, "5H %s  WK %s", pb, wb);
    mono(c, b, l, by + 6, 0, a.ok ? c.T.muted : c.T.warn);
    return;
  }
  snprintf(b, sizeof b, "%s / %s", pb, wb);
  c.g.font(F_TITLE);
  int vw = c.g.textW(b);
  fit(c.g, name, iw - 14 - vw - 8);
  txt(c, F_TITLE, name, l + 14, y, 0, c.T.ink);
  txt(c, F_TITLE, b, r, y, 2, a.ok ? c.T.ink : c.T.muted);
  int bw = (iw - 6) / 2;
  blocks(c.g, l, by, bw, 6, a.s.pct, 10, barColor(a, a.s.pct, c.T), c.T.empty);
  blocks(c.g, l + bw + 6, by, bw, 6, a.w.pct, 10, barColor(a, a.w.pct, c.T), c.T.empty);
}

static void tableCell(Ctx& c, int x, int w, int top, int mid, bool two, const Window& win, const Agent& a) {
  char b[16];
  pctStr(b, sizeof b, win.pct);
  uint32_t on = barColor(a, win.pct, c.T);
  if (two) {
    txt(c, F_TITLE, b, x, top + 11, 0, a.ok ? c.T.ink : c.T.muted);
    fmtReset(b, sizeof b, win, c.st, true);
    mono(c, b, x + w, top + 10, 2);
    blocks(c.g, x, top + 16, w, 7, win.pct, 14, on, c.T.empty);
  } else {
    txt(c, F_TITLE, b, x, mid + 5, 0, a.ok ? c.T.ink : c.T.muted);
    blocks(c.g, x + 38, mid - 3, w - 38, 7, win.pct, 8, on, c.T.empty);
  }
}

static void wAgents(Ctx& c) {
  const Model& m = c.m;
  if (!m.n) { center(c, "Waiting for data", c.st.ip); return; }
  int l = c.x + c.p, r = c.x + c.W - c.p;
  int cx1 = c.x + c.W * 38 / 100, cx2 = c.x + c.W * 68 / 100, cw = r - cx2;
  mono(c, "ACCOUNT", l, c.y + 16, 0);
  mono(c, "SESSION 5H", cx1, c.y + 16, 0);
  mono(c, "WEEK", cx2, c.y + 16, 0);
  c.g.rect(l, c.y + 22, r - l, 1, c.T.line);
  int rowH = (c.H - 26) / m.n;
  bool two = rowH >= 34;
  for (int i = 0; i < m.n; i++) {
    const Agent& a = m.agents[i];
    int ry = c.y + 24 + i * rowH;
    if (i) c.g.rect(l, ry, r - l, 1, c.T.line);
    int top = ry + (rowH - 24) / 2, mid = ry + rowH / 2;
    int ny = two ? top + 11 : mid + 5;
    c.g.circle(l + 4, ny - 4, 3, agentColor(a, c.T));
    char name[40];
    snprintf(name, sizeof name, "%s", a.name);
    c.g.font(F_TITLE);
    fit(c.g, name, cx1 - l - 18);
    txt(c, F_TITLE, name, l + 12, ny, 0, c.T.ink);
    if (two) {
      char p[12];
      upper(p, a.ok ? a.plan : "STALE", sizeof p);
      mono(c, p, l + 12, top + 23, 0, a.ok ? c.T.muted : c.T.warn);
    }
    tableCell(c, cx1, cx2 - cx1 - 12, top, mid, two, a.s, a);
    tableCell(c, cx2, cw, top, mid, two, a.w, a);
  }
}

// ---------- activity ----------
static uint32_t shade(const Theme& T, int i) {
  const uint32_t s[] = {T.ink, T.gray, T.claude, T.faint, T.rain, T.amber};
  return s[i % 6];
}

static void wActivity(Ctx& c) {
  const Model& m = c.m;
  char b[32];
  int l = c.x + c.p, r = c.x + c.W - c.p;
  bool full = c.H >= 150;
  long mx = 0;
  for (int h = 0; h < 24; h++) {
    long t = 0;
    for (int i = 0; i < m.n; i++) t += m.agents[i].hist[h];
    if (t > mx) mx = t;
  }
  int top = c.y + (c.card ? 6 : 0);
  mono(c, "ACTIVITY \xC2\xB7 24H", l, top + 12, 0);
  mono(c, "% OF WEEKLY LIMIT", r, top + 12, 2);
  int gridTop = top + 22;
  if (full) {
    int cols = m.n < 3 ? m.n : 3;
    int colW = (r - l) / (cols ? cols : 1);
    for (int i = 0; i < cols; i++) {
      const Agent& a = m.agents[i];
      long sum = 0;
      for (int h = 0; h < 24; h++) sum += a.hist[h];
      int cx = l + i * colW;
      c.g.rect(cx, top + 27, 7, 7, shade(c.T, i));
      char name[40];
      upper(name, a.name, sizeof name);
      c.g.font(F_MONO);
      fit(c.g, name, colW - 16);
      mono(c, name, cx + 11, top + 34, 0);
      snprintf(b, sizeof b, "+%ld.%ld%%", sum / 10, sum % 10);
      txt(c, cols <= 2 ? F_BIG : F_TITLE, b, cx, top + (cols <= 2 ? 60 : 54), 0, c.T.ink);
    }
    gridTop = top + 74;
  }
  int gx = c.x, gw = c.W, gy = gridTop, gh = c.y + c.H - gridTop;
  Ctx in = c;
  if (!c.card) {
    card(c.g, c.T, gx, gy, gw, gh);
    in.bg = c.T.card;
  }
  int rows = gh >= 120 ? 9 : (gh >= 80 ? 6 : 4);
  int pitch = (gw - 24 + 2) / 24;
  int vp = (gh - 34 + 2) / rows;
  if (vp < pitch) pitch = vp;
  int cell = pitch - 2;
  int tw = 24 * pitch - 2;
  int x0 = gx + (gw - tw) / 2, y0 = gy + 20;
  long unit = (mx + rows - 1) / rows;
  if (unit < 1) unit = 1;
  for (int col = 0; col < 24; col++) {
    int rr = 0;
    uint32_t colr[12];
    for (int i = 0; i < m.n && rr < rows; i++) {
      long v = m.agents[i].hist[col];
      int k = (int)((v + unit / 2) / unit);
      if (v && !k) k = 1;
      for (; k > 0 && rr < rows; k--) colr[rr++] = shade(c.T, i);
    }
    for (; rr < rows; rr++) colr[rr] = c.T.empty;
    for (rr = 0; rr < rows; rr++) c.g.rect(x0 + col * pitch, y0 + (rows - 1 - rr) * pitch, cell, cell, colr[rr]);
  }
  c.g.rect(x0, gy + 7, 7, 7, c.T.ink);
  snprintf(b, sizeof b, "= %ld.%ld%%", unit / 10, unit % 10);
  mono(in, b, x0 + 11, gy + 14, 0);
  snprintf(b, sizeof b, "PEAK %ld.%ld%%/H", mx / 10, mx % 10);
  mono(in, b, x0 + tw, gy + 14, 2);
  int ly = y0 + rows * pitch + 10;
  const char* lab[] = {"-24h", "-18h", "-12h", "-6h"};
  for (int i = 0; i < 4; i++) mono(in, lab[i], x0 + i * 6 * pitch, ly, 0);
  mono(in, "now", x0 + tw, ly, 2);
}

// ---------- host ----------
static void tile(Ctx& c, int x, int y, int w, int h, const char* label, const char* value, float bar, uint32_t bc,
                 const char* note) {
  card(c.g, c.T, x, y, w, h);
  Ctx in = c;
  in.bg = c.T.card;
  mono(in, label, x + 12, y + 20, 0);
  txt(in, F_BIG, value, x + 12, y + 50, 0, c.T.ink);
  if (note && *note) txt(in, F_BODY, note, x + w - 12, y + 49, 2, c.T.muted);
  if (bar >= 0) blocks(c.g, x + 12, y + h - 24, w - 24, 8, bar, 20, bc, c.T.empty);
}

static void wHost(Ctx& c) {
  const Host& h = c.m.pc;
  int l = c.x + c.p, r = c.x + c.W - c.p;
  char b[32];
  if (!h.has) { center(c, "No server stats", nullptr); return; }
  int vals[3] = {h.cpu, h.ram, h.disk};
  const char* lb[3] = {"CPU", "MEMORY", "DISK"};
  int lim[3] = {85, 85, 90};
  if (c.H >= 150 && c.W >= 250 && !c.card) {
    snprintf(b, sizeof b, "%s", h.name);
    c.g.font(F_TITLE);
    fit(c.g, b, 180);
    txt(c, F_TITLE, b, l + 2, c.y + 17, 0, c.T.ink);
    if (*h.up) {
      char u[24];
      snprintf(u, sizeof u, "UP %s", h.up);
      upper(b, u, sizeof b);
      mono(c, b, r, c.y + 15, 2);
    }
    int tw = (c.W - 6) / 2, th = (c.H - 32 - 6) / 2;
    for (int i = 0; i < 4; i++) {
      int x = c.x + (i % 2) * (tw + 6), y = c.y + 32 + (i / 2) * (th + 6);
      if (i < 3) {
        snprintf(b, sizeof b, "%d%%", vals[i]);
        tile(c, x, y, tw, th, lb[i], b, vals[i], vals[i] >= lim[i] ? c.T.warn : c.T.ink, "");
      } else if (h.bat >= 0) {
        snprintf(b, sizeof b, "%d%%", h.bat);
        tile(c, x, y, tw, th, "BATTERY", b, h.bat, h.bat <= 15 && !h.chg ? c.T.warn : c.T.ink, h.chg ? "charging" : "");
      } else {
        tile(c, x, y, tw, th, "UPTIME", *h.up ? h.up : "-", -1, 0, "");
      }
    }
    return;
  }
  int top = c.y + (c.card ? 18 : 12);
  snprintf(b, sizeof b, "%s", h.name);
  char up[32];
  upper(up, b, sizeof up);
  c.g.font(F_MONO);
  fit(c.g, up, r - l);
  mono(c, up, l, top, 0);
  int rowH = (c.y + c.H - c.p - top - 4) / 3;
  if (rowH > 30) rowH = 30;
  for (int i = 0; i < 3; i++) {
    int y = top + 8 + i * rowH;
    mono(c, lb[i], l, y + 11, 0);
    snprintf(b, sizeof b, "%d%%", vals[i]);
    txt(c, F_TITLE, b, r, y + 12, 2, c.T.ink);
    if (rowH >= 22) blocks(c.g, l, y + 16, r - l, 5, vals[i], 20, vals[i] >= lim[i] ? c.T.warn : c.T.ink, c.T.empty);
  }
}

// ---------- focus ----------
static int focusLeft(const UiState& st) {
  int left = st.focusRun ? (int)((long)st.focusEnd - (long)st.now) : st.focusLeft;
  return left < 0 ? 0 : left;
}

static void wFocus(Ctx& c) {
  const UiState& st = c.st;
  char b[32];
  int l = c.x + c.p, r = c.x + c.W - c.p;
  int left = focusLeft(st);
  float done = 100.f * (st.focusLen - left) / st.focusLen;
  const char* s = st.focusRun ? "Focusing" : (left == st.focusLen ? "Ready" : (left == 0 ? "Done" : "Paused"));
  snprintf(b, sizeof b, "%02d:%02d", left / 60, left % 60);
  if (c.H >= 150) {
    int top = c.y + (c.card ? 6 : 0);
    mono(c, "FOCUS", l, top + 12, 0);
    char d[32];
    snprintf(d, sizeof d, "TODAY \xC2\xB7 %d %s", st.focusDone, st.focusDone == 1 ? "SESSION" : "SESSIONS");
    mono(c, d, r, top + 12, 2);
    bool huge = c.W >= 180;
    int mid = c.y + c.H / 2;
    txt(c, huge ? F_HUGE : F_CLOCK, b, c.x + c.W / 2, mid - 6, 1, c.T.ink);
    blocks(c.g, l, mid + 14, r - l, 14, done, c.W >= 250 ? st.focusLen / 60 : 15, c.T.ink, c.T.empty);
    txt(c, F_TITLE, s, c.x + c.W / 2, mid + 58, 1, st.focusRun ? c.T.ink : c.T.muted);
    if (c.W >= 250) mono(c, "TAP  START / PAUSE      HOLD  RESET", c.x + c.W / 2, c.y + c.H - 12, 1, c.T.faint);
    return;
  }
  if (c.H >= 100) {
    mono(c, "FOCUS", l, c.y + 20, 0);
    txt(c, F_TITLE, s, r, c.y + 21, 2, st.focusRun ? c.T.ink : c.T.muted);
    txt(c, c.W >= 140 ? F_CLOCK : F_BIG, b, l - 1, c.y + 66, 0, c.T.ink);
    blocks(c.g, l, c.y + c.H - 22, r - l, 9, done, 15, c.T.ink, c.T.empty);
    return;
  }
  txt(c, F_BIG, b, l, c.y + (c.card ? 29 : 24), 0, c.T.ink);
  if (c.W >= 140) mono(c, s, r, c.y + (c.card ? 27 : 22), 2, st.focusRun ? c.T.live : c.T.muted);
  else c.g.circle(r - 3, c.y + (c.card ? 13 : 8), 3, st.focusRun ? c.T.live : c.T.faint);
  blocks(c.g, l, c.y + c.H - (c.card ? 15 : 10), r - l, 5, done, 15, c.T.ink, c.T.empty);
}

// ---------- calendar ----------
static void evStatus(Ctx& c, const Event& e, char* b, size_t n, uint32_t* col) {
  long now = c.st.now;
  *col = c.T.muted;
  if (e.allDay) { snprintf(b, n, "ALL DAY"); return; }
  if ((long)e.start <= now) {
    char t[8];
    hhmm(t, sizeof t, e.end);
    snprintf(b, n, "NOW \xE2\x86\x92 %s", t);
    *col = c.T.live;
    return;
  }
  long d = (long)e.start - now;
  if (d < 6 * 3600) {
    char t[16];
    fmtDur(t, sizeof t, d);
    char up[24];
    snprintf(up, sizeof up, "IN %s", t);
    upper(b, up, n);
    if (d < 15 * 60) *col = c.T.amber;
    return;
  }
  char s1[8], s2[8];
  hhmm(s1, sizeof s1, e.start);
  hhmm(s2, sizeof s2, e.end);
  snprintf(b, n, "%s\xE2\x80\x93%s", s1, s2);
}

static uint32_t evColor(Ctx& c, const Event& e) { return e.color ? e.color : c.T.rain; }

static void wCalendar(Ctx& c) {
  const Model& m = c.m;
  int l = c.x + c.p, r = c.x + c.W - c.p;
  int first = -1, count = 0;
  for (int i = 0; i < m.nEv; i++) {
    if ((long)m.ev[i].end <= (long)c.st.now) continue;
    if (first < 0) first = i;
    count++;
  }
  char b[96], s[32];
  uint32_t sc;
  if (c.H < 60) {
    if (first < 0) { center(c, "Nothing planned", nullptr); return; }
    const Event& e = m.ev[first];
    int y = c.y + (c.card ? 21 : 16);
    c.g.circle(l + 3, y - 5, 3, evColor(c, e));
    evStatus(c, e, s, sizeof s, &sc);
    c.g.font(F_MONO);
    int sw = c.g.textW(s);
    snprintf(b, sizeof b, "%s", e.title);
    c.g.font(F_TITLE);
    fit(c.g, b, r - l - 12);
    txt(c, F_TITLE, b, l + 12, y, 0, c.T.ink);
    mono(c, s, l + 12, c.y + c.H - (c.card ? 12 : 8), 0, sc);
    if (count > 1 && c.W - 2 * c.p - sw > 70) {
      snprintf(s, sizeof s, "+%d MORE", count - 1);
      mono(c, s, r, c.y + c.H - (c.card ? 12 : 8), 2, c.T.faint);
    }
    return;
  }
  int top = c.y + (c.card ? 6 : 0);
  char head[40] = "TODAY";
  if (c.w.title[0]) upper(head, c.w.title, sizeof head);
  else if (c.st.timeOk) snprintf(head, sizeof head, "TODAY \xC2\xB7 %s %02d %s", WD[c.st.lt.tm_wday], c.st.lt.tm_mday, MON[c.st.lt.tm_mon]);
  c.g.font(F_MONO);
  fit(c.g, head, r - l - 60);
  mono(c, head, l, top + 12, 0);
  if (first < 0) {
    Ctx k = c;
    k.y = top + 16;
    k.H = c.y + c.H - k.y;
    center(k, "Nothing planned", "FREE");
    return;
  }
  snprintf(s, sizeof s, "%d %s", count, count == 1 ? "EVENT" : "EVENTS");
  mono(c, s, r, top + 12, 2);
  int y = top + 20, bottom = c.y + c.H - (c.card ? 6 : 2);
  int today = c.st.timeOk ? dayOf(c.st.now) : 0;
  int lastDay = today;
  bool timeCol = c.W >= 150;
  int tx = timeCol ? l + 44 : l + 10;
  for (int i = first; i < m.nEv; i++) {
    const Event& e = m.ev[i];
    if ((long)e.end <= (long)c.st.now) continue;
    int d = dayOf(e.start < c.st.now ? c.st.now : e.start);
    if (d != lastDay) {
      if (y + 16 + 28 > bottom) break;
      c.g.rect(l, y + 6, r - l, 1, c.T.line);
      time_t tt = e.start;
      struct tm rr;
      localtime_r(&tt, &rr);
      if (d == today + 1 || (d > today && d - today < 400 && rr.tm_yday == 0)) snprintf(s, sizeof s, "TOMORROW");
      else snprintf(s, sizeof s, "%s %02d %s", WD[rr.tm_wday], rr.tm_mday, MON[rr.tm_mon]);
      c.g.font(F_MONO);
      int sw = c.g.textW(s);
      c.g.rect(l, y + 1, sw + 8, 11, c.bg);
      mono(c, s, l, y + 10, 0, c.T.faint);
      y += 16;
      lastDay = d;
    }
    if (y + 28 > bottom) break;
    count--;
    c.g.rect(l, y + 4, 3, 22, evColor(c, e));
    if (timeCol) {
      if (e.allDay) mono(c, "ALL", l + 9, y + 13, 0);
      else {
        hhmm(s, sizeof s, e.start);
        mono(c, s, l + 9, y + 13, 0, c.T.ink);
        hhmm(s, sizeof s, e.end);
        mono(c, s, l + 9, y + 24, 0);
      }
    }
    snprintf(b, sizeof b, "%s", e.title);
    c.g.font(F_BODY);
    fit(c.g, b, r - tx);
    txt(c, F_BODY, b, tx, y + 14, 0, c.T.ink);
    evStatus(c, e, s, sizeof s, &sc);
    if (sc != c.T.muted || !timeCol) mono(c, s, tx, y + 25, 0, sc);
    y += 30;
  }
  if (count > 0 && y + 12 <= bottom) {
    snprintf(s, sizeof s, "+%d MORE", count);
    mono(c, s, l + (timeCol ? 9 : 10), y + 10, 0, c.T.faint);
  }
}

// ---------- text ----------
static void wText(Ctx& c) {
  const char* t = c.m.text(c.w.text);
  int l = c.x + c.p, r = c.x + c.W - c.p;
  int top = c.y + (c.card ? 10 : 4), bottom = c.y + c.H - (c.card ? 10 : 4);
  if (c.w.title[0]) {
    char h[40];
    upper(h, c.w.title, sizeof h);
    c.g.font(F_MONO);
    fit(c.g, h, r - l);
    mono(c, h, c.w.align == 1 ? c.x + c.W / 2 : l, top + 8, c.w.align == 1 ? 1 : 0);
    top += 14;
  }
  if (!*t) { center(c, "Empty note", nullptr); return; }
  const uint8_t* fonts[3] = {F_TEXT, F_TITLE, F_BODY};
  const int lh[3] = {25, 18, 16};
  const int cap[3] = {15, 10, 9};
  int from = c.w.size == 0 ? 0 : 3 - c.w.size;
  char lines[10][96];
  int n = 0, f = from;
  bool cut = false;
  for (; f < 3; f++) {
    int maxL = (bottom - top + lh[f] - cap[f]) / lh[f];
    if (maxL > 10) maxL = 10;
    if (maxL < 1) continue;
    c.g.font(fonts[f]);
    n = wrap(c.g, t, r - l, lines, maxL, &cut);
    if (!cut || c.w.size || f == 2) break;
  }
  if (f > 2) f = 2;
  if (cut && n) {
    char* last = lines[n - 1];
    size_t k = strlen(last);
    if (k < 90) strcat(last, "\xE2\x80\xA6");
    c.g.font(fonts[f]);
    fit(c.g, last, r - l);
  }
  int block = (n - 1) * lh[f] + cap[f];
  int y = top + (bottom - top - block) / 2 + cap[f];
  uint32_t col = c.w.color ? c.w.color : c.T.ink;
  for (int i = 0; i < n; i++) {
    int ax = c.w.align == 1 ? c.x + c.W / 2 : (c.w.align == 2 ? r : l);
    txt(c, fonts[f], lines[i], ax, y + i * lh[f], c.w.align, col);
  }
}

// ---------- ping ----------
static void spark(Ctx& c, const Target& t, int x, int y, int w, int h, int mx) {
  int bw = w / 24;
  if (bw < 2) return;
  for (int i = 0; i < 24; i++) {
    uint8_t v = t.hist[i];
    if (!v) continue;
    int bh = v == 255 ? h : 2 + (h - 2) * v / (mx ? mx : 1);
    if (bh > h) bh = h;
    c.g.rect(x + i * bw, y + h - bh, bw - 1, bh, v == 255 ? c.T.warn : c.T.faint);
  }
}

static void wPing(Ctx& c) {
  const Model& m = c.m;
  int l = c.x + c.p, r = c.x + c.W - c.p;
  char b[40];
  if (!m.nPing) { center(c, "No servers yet", "ADD THEM IN THE EDITOR"); return; }
  int up = 0, sum = 0, mx = 1;
  for (int i = 0; i < m.nPing; i++) {
    if (m.pings[i].ms >= 0) { up++; sum += m.pings[i].ms; }
    for (int k = 0; k < 24; k++)
      if (m.pings[i].hist[k] != 255 && m.pings[i].hist[k] > mx) mx = m.pings[i].hist[k];
  }
  uint32_t state = up == m.nPing ? c.T.live : (up ? c.T.amber : c.T.warn);
  if (c.H < 60) {
    int y = c.y + (c.card ? 21 : 16);
    c.g.circle(l + 4, y - 5, 4, state);
    snprintf(b, sizeof b, "%d/%d up", up, m.nPing);
    txt(c, F_TITLE, b, l + 14, y, 0, c.T.ink);
    if (up < m.nPing) {
      char down[96] = "";
      for (int i = 0; i < m.nPing; i++)
        if (m.pings[i].ms < 0) {
          if (*down) strncat(down, ", ", sizeof down - strlen(down) - 1);
          strncat(down, m.pings[i].name, sizeof down - strlen(down) - 1);
        }
      char u[96];
      upper(u, down, sizeof u);
      c.g.font(F_MONO);
      fit(c.g, u, r - l);
      mono(c, u, l, c.y + c.H - (c.card ? 12 : 8), 0, c.T.warn);
    } else {
      snprintf(b, sizeof b, "AVG %d MS", up ? sum / up : 0);
      mono(c, b, l, c.y + c.H - (c.card ? 12 : 8), 0);
    }
    return;
  }
  int top = c.y + (c.card ? 6 : 0);
  char head[32];
  upper(head, c.w.title[0] ? c.w.title : "SERVERS", sizeof head);
  mono(c, head, l, top + 12, 0);
  snprintf(b, sizeof b, "%d/%d UP", up, m.nPing);
  mono(c, b, r, top + 12, 2, state);
  int y0 = top + 20, avail = c.y + c.H - (c.card ? 6 : 2) - y0;
  int rowH = 20;
  int perCol = avail / rowH;
  int cols = (m.nPing > perCol && c.W >= 250) ? 2 : 1;
  int colW = (r - l - (cols - 1) * 12) / cols;
  bool sp = cols == 1 && c.W >= 200;
  for (int i = 0; i < m.nPing; i++) {
    int col = i / perCol, row = i % perCol;
    if (col >= cols) break;
    if (cols == 1 && i == perCol - 1 && m.nPing > perCol) {
      snprintf(b, sizeof b, "+%d MORE", m.nPing - i);
      mono(c, b, l, y0 + row * rowH + 14, 0, c.T.faint);
      break;
    }
    const Target& t = m.pings[i];
    int x = l + col * (colW + 12), y = y0 + row * rowH;
    c.g.circle(x + 3, y + 9, 3, t.ms >= 0 ? c.T.live : c.T.warn);
    if (t.ms >= 0) snprintf(b, sizeof b, "%d ms", t.ms);
    else snprintf(b, sizeof b, "DOWN");
    mono(c, b, x + colW, y + 13, 2, t.ms >= 0 ? c.T.muted : c.T.warn);
    char name[40];
    snprintf(name, sizeof name, "%s", t.name);
    c.g.font(F_BODY);
    int nameW = sp ? colW * 42 / 100 : colW - 52;
    fit(c.g, name, nameW);
    txt(c, F_BODY, name, x + 12, y + 13, 0, c.T.ink);
    if (sp) spark(c, t, x + 12 + nameW + 8, y + 3, colW - nameW - 20 - 52, 12, mx);
  }
}

// ---------- remnawave nodes ----------
static void person(Gfx& g, int x, int y, uint32_t c) {
  g.circle(x + 3, y + 2, 2, c);
  g.rrect(x, y + 5, 7, 4, 2, c);
}

static uint32_t nodeColor(const Theme& T, uint8_t st) { return st == 1 ? T.live : st == 0 ? T.warn : T.faint; }

static void wNodes(Ctx& c) {
  const Model& m = c.m;
  int l = c.x + c.p, r = c.x + c.W - c.p;
  char b[40];
  if (!m.hasNodes) { center(c, "Remnawave is off", "SET IT UP ON THE SERVER"); return; }
  if (!m.nNodes) { center(c, m.nodesStale ? "No data yet" : "No nodes", nullptr); return; }
  int up = 0, total = 0;
  for (int i = 0; i < m.nNodes; i++) {
    if (m.nodes[i].state == 1) up++;
    total += m.nodes[i].users;
  }
  uint32_t state = m.nodesStale ? c.T.faint : (up == m.nNodes ? c.T.live : (up ? c.T.amber : c.T.warn));
  if (c.H < 60) {
    int y = c.y + (c.card ? 21 : 16);
    c.g.circle(l + 4, y - 5, 4, state);
    if (m.nodesStale) snprintf(b, sizeof b, "%d nodes", m.nNodes);
    else snprintf(b, sizeof b, "%d/%d up", up, m.nNodes);
    txt(c, F_TITLE, b, l + 14, y, 0, c.T.ink);
    int by = c.y + c.H - (c.card ? 12 : 8);
    if (c.W < 140) {
      snprintf(b, sizeof b, "%d ONLINE", total);
      mono(c, m.nodesStale ? "STALE" : b, l, by, 0, m.nodesStale ? c.T.amber : c.T.muted);
      return;
    }
    person(c.g, r - 7, y - 10, c.T.muted);
    snprintf(b, sizeof b, "%d", total);
    txt(c, F_TITLE, b, r - 11, y, 2, c.T.ink);
    mono(c, m.nodesStale ? "STALE" : "USERS ONLINE", l, by, 0, m.nodesStale ? c.T.amber : c.T.muted);
    return;
  }
  int top = c.y + (c.card ? 6 : 0);
  char head[32];
  upper(head, c.w.title[0] ? c.w.title : "NODES", sizeof head);
  mono(c, head, l, top + 12, 0);
  bool wide = c.W >= 250, mid = c.W >= 190, narrow = c.W < 120;
  if (m.nodesStale) mono(c, "STALE", r, top + 12, 2, c.T.amber);
  else {
    snprintf(b, sizeof b, c.W < 140 ? "%d" : "%d ONLINE", total);
    mono(c, b, r, top + 12, 2);
  }
  int cpuX = r - 96, ramX = r - 44, usersR = wide ? r - 112 : (mid ? r - 48 : r);
  if (wide) {
    mono(c, "CPU", cpuX, top + 26, 0, c.T.faint);
    mono(c, "RAM", ramX, top + 26, 0, c.T.faint);
  }
  int y0 = top + (wide ? 30 : 18), rowH = 19;
  int bottom = c.y + c.H - (c.card ? 6 : 2);
  int rows = (bottom - y0) / rowH;
  if (rows < 1) rows = 1;
  int show = m.nNodes > rows ? rows - 1 : m.nNodes;
  for (int i = 0; i < show; i++) {
    const NodeRow& n = m.nodes[i];
    int y = y0 + i * rowH;
    uint32_t dim = n.state == 1 ? c.T.ink : c.T.muted;
    c.g.circle(l + 3, y + 9, 3, nodeColor(c.T, n.state));
    char name[24];
    snprintf(name, sizeof name, "%s", n.name);
    snprintf(b, sizeof b, "%d", n.users);
    c.g.font(F_BODY);
    int uw = c.g.textW(b) + (narrow ? 6 : 16);
    fit(c.g, name, usersR - uw - (l + 12));
    txt(c, F_BODY, name, l + 12, y + 13, 0, dim);
    txt(c, F_BODY, b, usersR, y + 13, 2, dim);
    if (!narrow) {
      c.g.font(F_BODY);
      person(c.g, usersR - c.g.textW(b) - 11, y + 4, c.T.faint);
    }
    if (wide) {
      const int vals[2] = {n.cpu, n.ram};
      const int xs[2] = {cpuX, ramX};
      for (int k = 0; k < 2; k++) {
        if (vals[k] < 0) { mono(c, "-", xs[k], y + 12, 0, c.T.faint); continue; }
        blocks(c.g, xs[k], y + 6, 22, 6, vals[k], 5, vals[k] >= 85 ? c.T.warn : c.T.ink, c.T.empty);
        snprintf(b, sizeof b, "%d%%", vals[k]);
        mono(c, b, xs[k] + 26, y + 12, 0, vals[k] >= 85 ? c.T.warn : c.T.muted);
      }
    } else if (mid) {
      if (n.cpu >= 0 || n.ram >= 0) {
        char cb[8] = "-", rb[8] = "-";
        if (n.cpu >= 0) snprintf(cb, sizeof cb, "%d", n.cpu);
        if (n.ram >= 0) snprintf(rb, sizeof rb, "%d", n.ram);
        snprintf(b, sizeof b, "C%s R%s", cb, rb);
        mono(c, b, r, y + 12, 2, (n.cpu >= 85 || n.ram >= 85) ? c.T.warn : c.T.muted);
      }
    }
  }
  if (show < m.nNodes) {
    snprintf(b, sizeof b, "+%d MORE", m.nNodes - show);
    mono(c, b, l + 12, y0 + show * rowH + 12, 0, c.T.faint);
  }
}

// ---------- pet ----------
static const char* PET[16] = {
    "................", "......l..l......", ".......ll.......", "....oooooooo....",
    "...obbbbbbbbo...", "..obhhbbbbbbbo..", "..obhbbbbbbbbo..", ".obbbbbbbbbbbbo.",
    ".obbbbbbbbbbbbo.", ".obbbbbbbbbbbbo.", ".obbbbbbbbbbbbo.", ".obbbbbbbbbbbbo.",
    "..obbbbbbbbbbo..", "...oobbbbbboo...", "....oo....oo....", "................"};

enum Mood { M_HAPPY, M_OK, M_TIRED, M_KO, M_SLEEP, M_LOST };

static Mood petMood(const Model& m, const UiState& st, float* energy, float* food) {
  float s = 0, w = 0;
  bool stale = false;
  for (int i = 0; i < m.n; i++) {
    if (!m.agents[i].ok) stale = true;
    if (m.agents[i].s.pct > s) s = m.agents[i].s.pct;
    if (m.agents[i].w.pct > w) w = m.agents[i].w.pct;
  }
  *energy = 100 - s;
  *food = 100 - w;
  if (st.timeOk && (st.lt.tm_hour >= 1 && st.lt.tm_hour < 7)) return M_SLEEP;
  if (!m.n || stale) return M_LOST;
  if (s >= 99 || w >= 99) return M_KO;
  if (s >= 80 || w >= 90) return M_TIRED;
  if (s < 40) return M_HAPPY;
  return M_OK;
}

static void px(Gfx& g, int ox, int oy, int s, int x, int y, uint32_t c) { g.rect(ox + x * s, oy + y * s, s, s, c); }

static void drawPet(Ctx& c, int ox, int oy, int s, Mood mood) {
  uint32_t ink = 0x262626, body = c.w.color ? c.w.color : 0xFFC857;
  uint32_t leaf = 0x3DBE7E, hi = 0xFFF4D6, cheek = 0xF29A9A;
  bool bob = (c.st.ms / 600) % 2 && mood != M_KO && mood != M_SLEEP;
  if (mood == M_SLEEP) bob = (c.st.ms / 1400) % 2;
  oy += bob ? s : 0;
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      char k = PET[y][x];
      if (k == '.') continue;
      uint32_t col = k == 'o' ? ink : k == 'b' ? body : k == 'h' ? hi : leaf;
      if (bob && y == 14) continue;
      px(c.g, ox, oy, s, x, y, col);
    }
  if (bob) {
    px(c.g, ox, oy, s, 4, 13, ink);
    px(c.g, ox, oy, s, 11, 13, ink);
  }
  bool blink = (c.st.ms % 4200) < 160;
  px(c.g, ox, oy, s, 3, 10, cheek);
  px(c.g, ox, oy, s, 12, 10, cheek);
  auto eyePair = [&](int dx) {
    switch (mood) {
      case M_HAPPY:
        px(c.g, ox, oy, s, 4 + dx, 9, ink);
        px(c.g, ox, oy, s, 5 + dx, 8, ink);
        px(c.g, ox, oy, s, 6 + dx, 9, ink);
        break;
      case M_KO:
        px(c.g, ox, oy, s, 4 + dx, 8, ink);
        px(c.g, ox, oy, s, 6 + dx, 8, ink);
        px(c.g, ox, oy, s, 5 + dx, 9, ink);
        px(c.g, ox, oy, s, 4 + dx, 10, ink);
        px(c.g, ox, oy, s, 6 + dx, 10, ink);
        break;
      case M_SLEEP:
      case M_TIRED:
        px(c.g, ox, oy, s, 4 + dx, 9, ink);
        px(c.g, ox, oy, s, 5 + dx, 9, ink);
        if (mood == M_TIRED) px(c.g, ox, oy, s, 5 + dx, 8, blink ? body : ink);
        break;
      default:
        if (blink) {
          px(c.g, ox, oy, s, 4 + dx, 9, ink);
          px(c.g, ox, oy, s, 5 + dx, 9, ink);
        } else {
          px(c.g, ox, oy, s, 5 + dx, 8, ink);
          px(c.g, ox, oy, s, 5 + dx, 9, ink);
        }
    }
  };
  eyePair(0);
  eyePair(5);
  if (mood == M_HAPPY) {
    px(c.g, ox, oy, s, 7, 11, ink);
    px(c.g, ox, oy, s, 8, 11, ink);
    px(c.g, ox, oy, s, 6, 10, ink);
    px(c.g, ox, oy, s, 9, 10, ink);
  } else if (mood == M_KO || mood == M_TIRED) {
    px(c.g, ox, oy, s, 7, 11, ink);
    px(c.g, ox, oy, s, 8, 11, ink);
    if (mood == M_TIRED && (c.st.ms / 900) % 2) {
      px(c.g, ox, oy, s, 13, 5, c.T.rain);
      px(c.g, ox, oy, s, 13, 6, c.T.rain);
    }
  } else if (mood == M_SLEEP) {
    px(c.g, ox, oy, s, 7, 11, ink);
    int z = (c.st.ms / 700) % 3;
    for (int k = 0; k <= z; k++) {
      c.g.font(F_MONO);
      c.g.text("z", ox + (13 + k) * s, oy + (4 - k * 2) * s, 0, c.T.muted, c.bg);
    }
  } else if (mood == M_LOST) {
    px(c.g, ox, oy, s, 7, 11, ink);
    c.g.font(F_TITLE);
    c.g.text("?", ox + 14 * s, oy + 5 * s, 0, c.T.muted, c.bg);
  } else {
    px(c.g, ox, oy, s, 7, 11, ink);
    px(c.g, ox, oy, s, 8, 11, ink);
  }
}

static const char* moodText(Mood m) {
  switch (m) {
    case M_HAPPY: return "CHILL";
    case M_OK: return "WORKING";
    case M_TIRED: return "TIRED";
    case M_KO: return "OUT OF JUICE";
    case M_SLEEP: return "ZZZ";
    default: return "CONFUSED";
  }
}

static void wPet(Ctx& c) {
  float energy, food;
  Mood mood = petMood(c.m, c.st, &energy, &food);
  const char* name = c.w.title[0] ? c.w.title : "Mochi";
  int l = c.x + c.p, r = c.x + c.W - c.p;
  if (c.H < 60) {
    int s = (c.H - 8) / 16;
    if (s < 1) s = 1;
    drawPet(c, l - 2, c.y + (c.H - 16 * s) / 2 - s, s, mood);
    int tx = l + 16 * s + 6;
    char n[40];
    snprintf(n, sizeof n, "%s", name);
    c.g.font(F_TITLE);
    fit(c.g, n, r - tx);
    txt(c, F_TITLE, n, tx, c.y + c.H / 2 - 1, 0, c.T.ink);
    mono(c, moodText(mood), tx, c.y + c.H / 2 + 13, 0);
    return;
  }
  bool stats = c.H >= 150;
  int textH = stats ? 70 : 34;
  int s = (c.W - 2 * c.p) / 16;
  int sv = (c.H - 2 * c.p - textH) / 15;
  if (sv < s) s = sv;
  if (s < 1) s = 1;
  int ox = c.x + (c.W - 16 * s) / 2, oy = c.y + c.p - s;
  drawPet(c, ox, oy, s, mood);
  int ty = oy + 16 * s + 4;
  char n[40];
  snprintf(n, sizeof n, "%s", name);
  c.g.font(F_TITLE);
  fit(c.g, n, r - l);
  txt(c, F_TITLE, n, c.x + c.W / 2, ty + 10, 1, c.T.ink);
  mono(c, moodText(mood), c.x + c.W / 2, ty + 24, 1);
  if (!stats) return;
  const char* lb[2] = {"ENERGY", "FOOD"};
  float v[2] = {energy, food};
  for (int k = 0; k < 2; k++) {
    int y = c.y + c.H - c.p - 30 + k * 16;
    mono(c, lb[k], l, y + 7, 0);
    blocks(c.g, l + 44, y, r - l - 44, 7, v[k], 10, v[k] < 15 ? c.T.warn : c.T.ink, c.T.empty);
  }
}

// ---------- gif ----------
static void wGif(Ctx& c) {
  if (c.w.asset[0] && c.g.gif(c.w.asset, c.x, c.y, c.W, c.H, c.st.ms, c.bg)) return;
  if (!c.card) card(c.g, c.T, c.x, c.y, c.W, c.H);
  Ctx in = c;
  in.bg = c.T.card;
  center(in, c.w.asset[0] ? "Loading" : "No GIF", "UPLOAD IN THE EDITOR");
}

// ---------- screen ----------
static const Theme& theme(const UiState& st) { return st.dark ? DARK : LIGHT; }

void drawWidget(Gfx& g, const Model& m, const UiState& st, const Widget& w) {
  const Theme& T = theme(st);
  int x, y, W, H;
  cellRect(w, x, y, W, H);
  g.rect(x, y, W, H, T.bg);
  if (w.card) card(g, T, x, y, W, H);
  Ctx c{g, T, m, st, w, x, y, W, H, w.card ? (H < 60 ? 10 : 12) : 2, w.card ? T.card : T.bg, w.card};
  switch (w.type) {
    case W_CLOCK: wClock(c); break;
    case W_AGENT: wAgent(c); break;
    case W_AGENTS: wAgents(c); break;
    case W_WEATHER: wWeather(c); break;
    case W_CALENDAR: wCalendar(c); break;
    case W_TEXT: wText(c); break;
    case W_PING: wPing(c); break;
    case W_PET: wPet(c); break;
    case W_GIF: wGif(c); break;
    case W_ACTIVITY: wActivity(c); break;
    case W_HOST: wHost(c); break;
    case W_FOCUS: wFocus(c); break;
    case W_NODES: wNodes(c); break;
  }
}

void drawScreen(Gfx& g, const Model& m, const UiState& st) {
  const Theme& T = theme(st);
  g.clear(T.bg);
  int n = screenCount(m);
  int s = st.screen < n ? st.screen : 0;
  if (s < m.nScreens) {
    const ScreenDef& sd = m.screens[s];
    for (int i = 0; i < sd.n; i++) drawWidget(g, m, st, sd.w[i]);
  }
  if (n > 1) {
    int cx = 160 - (n - 1) * 4;
    for (int i = 0; i < n; i++) g.circle(cx + i * 8, 234, i == s ? 2 : 1, i == s ? T.ink : T.faint);
  }
}
