#include "ui.h"
#include "fonts.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

struct Theme {
  uint32_t bg, card, line, ink, muted, faint, empty, gray, chip, warn, claude, live;
  uint32_t sun, cloud, rain;
};

static const Theme LIGHT = {0xF1F1EF, 0xFFFFFF, 0xE3E3E0, 0x151515, 0x8A8A86, 0xB9B9B4,
                            0xECECE9, 0xA8A8A3, 0xF2F2F0, 0xE5533D, 0xD97757, 0x2FA36B,
                            0xF2B33D, 0xCFCFCB, 0x4F7FE0};
static const Theme DARK = {0x0D0D0D, 0x171717, 0x262626, 0xECECEC, 0x7A7A7A, 0x4A4A4A,
                           0x242424, 0x5C5C5C, 0x212121, 0xF0654F, 0xD97757, 0x3DBE7E,
                           0xE8A93A, 0x3E3E3E, 0x5B8AEA};

static const char* WD[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* WDs[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* MON[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                            "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static void upper(char* dst, const char* src, size_t n) {
  size_t i = 0;
  for (; src[i] && i + 1 < n; i++) dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i];
  dst[i] = 0;
}

static void fit(Gfx& g, char* s, int maxw) {
  if (g.textW(s) <= maxw) return;
  size_t n = strlen(s);
  char buf[40];
  do {
    while (n > 1 && s[n - 1] == ' ') n--;
    snprintf(buf, sizeof buf, "%.*s\xE2\x80\xA6", (int)--n, s);
  } while (n > 1 && g.textW(buf) > maxw);
  strcpy(s, buf);
}

static void card(Gfx& g, const Theme& T, int x, int y, int w, int h) {
  g.rrect(x, y, w, h, 10, T.line);
  g.rrect(x + 1, y + 1, w - 2, h - 2, 9, T.card);
}

static void mono(Gfx& g, const Theme& T, const char* s, int x, int y, int al, uint32_t bg,
                 uint32_t c = 0xFFFFFFFF) {
  g.font(F_MONO);
  g.text(s, x, y, al, c == 0xFFFFFFFF ? T.muted : c, bg);
}

static void blocks(Gfx& g, int x, int y, int w, int h, float pct, int n, uint32_t on,
                   uint32_t off, float pace = -1, uint32_t paceC = 0) {
  const int gap = 2;
  int cw = (w + gap) / n - gap;
  if (cw < 1) cw = 1;
  int used = n * cw + (n - 1) * gap;
  int x0 = x + (w - used) / 2;
  for (int i = 0; i < n; i++)
    g.rect(x0 + i * (cw + gap), y, cw, h, pct >= (i + 0.5f) * 100.f / n ? on : off);
  if (pace >= 0 && pace <= 100) {
    int px = x0 + (int)roundf(pace / 100.f * used);
    g.rect(px - 1, y + h + 3, 2, 4, paceC);
  }
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

static uint32_t agentColor(const Agent& a, const Theme& T) {
  if (a.color) return a.color;
  if (!strcmp(a.kind, "claude")) return T.claude;
  return T.ink;
}

static uint32_t barColor(const Agent& a, float pct, const Theme& T) {
  if (!a.ok) return T.faint;
  return pct >= 90 ? T.warn : T.ink;
}

static void pageDots(Gfx& g, const Theme& T, const UiState& st) {
  int cx = 160 - (SCR_COUNT - 1) * 4;
  for (int i = 0; i < SCR_COUNT; i++)
    g.circle(cx + i * 8, 234, i == st.screen ? 2 : 1, i == st.screen ? T.ink : T.faint);
}

enum Wx { W_SUN, W_PARTLY, W_CLOUD, W_FOG, W_RAIN, W_SNOW, W_STORM };

static Wx wxKind(int c) {
  if (c == 0) return W_SUN;
  if (c <= 2) return W_PARTLY;
  if (c == 3) return W_CLOUD;
  if (c == 45 || c == 48) return W_FOG;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return W_SNOW;
  if (c >= 95) return W_STORM;
  return W_RAIN;
}

static const char* wxText(int c) {
  switch (wxKind(c)) {
    case W_SUN: return "Clear";
    case W_PARTLY: return "Partly cloudy";
    case W_CLOUD: return "Overcast";
    case W_FOG: return "Fog";
    case W_SNOW: return "Snow";
    case W_STORM: return "Thunderstorm";
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
  if (k == W_SUN) {
    g.circle(cx, cy, (int)(9 * s), T.sun);
    if (!day) g.circle(cx + (int)(5 * s), cy - (int)(4 * s), (int)(8 * s), bg);
    return;
  }
  if (k == W_PARTLY) {
    g.circle(cx + (int)(5 * s), cy - (int)(6 * s), (int)(7 * s), day ? T.sun : T.faint);
    cloud(g, cx - (int)(1 * s), cy + (int)(2 * s), s, bg);
    cloud(g, cx - (int)(2 * s), cy + (int)(3 * s), s * 0.92f, T.cloud);
    return;
  }
  if (k == W_FOG) {
    for (int i = 0; i < 3; i++)
      g.rrect(cx - (int)(12 * s) + (i % 2) * (int)(4 * s), cy - (int)(7 * s) + i * (int)(6 * s),
              (int)(22 * s), (int)(3 * s), 1, T.cloud);
    return;
  }
  cloud(g, cx, cy - (int)(3 * s), s, T.cloud);
  int y0 = cy + (int)(8 * s);
  if (k == W_RAIN)
    for (int i = -1; i <= 1; i++) g.rrect(cx + i * (int)(7 * s) - 1, y0, 2, (int)(6 * s), 1, T.rain);
  if (k == W_SNOW)
    for (int i = -1; i <= 1; i++) g.circle(cx + i * (int)(7 * s), y0 + (int)(3 * s), (int)(1.6f * s) + 1, T.muted);
  if (k == W_STORM) {
    g.rect(cx, y0, (int)(3 * s), (int)(4 * s), T.sun);
    g.rect(cx - (int)(2 * s), y0 + (int)(3 * s), (int)(3 * s), (int)(4 * s), T.sun);
  }
}

static void header(Gfx& g, const Model& m, const UiState& st, const Theme& T) {
  char b[40];
  g.font(F_CLOCK);
  if (st.timeOk) snprintf(b, sizeof b, "%02d:%02d", st.lt.tm_hour, st.lt.tm_min);
  else snprintf(b, sizeof b, "--:--");
  g.text(b, 12, 46, 0, T.ink, T.bg);

  if (st.timeOk)
    snprintf(b, sizeof b, "%s \xC2\xB7 %02d %s", WD[st.lt.tm_wday], st.lt.tm_mday, MON[st.lt.tm_mon]);
  else snprintf(b, sizeof b, "SYNCING TIME");
  g.font(F_MONO);
  int tw = g.textW(b);
  mono(g, T, b, 308, 24, 2, T.bg);
  bool live = m.updated && st.now - m.updated < 180;
  g.circle(308 - tw - 8, 21, 2, live ? T.live : T.faint);

  g.font(F_TITLE);
  if (m.wx.has) snprintf(b, sizeof b, "%d\xC2\xB0  %s", (int)roundf(m.wx.t), wxText(m.wx.code));
  else snprintf(b, sizeof b, "Desk panel");
  g.text(b, 308, 45, 2, T.ink, T.bg);
}

static void section(Gfx& g, const Theme& T, int x, int y, int w, const char* label,
                    const Window& win, const Agent& a, const UiState& st, bool pace) {
  char b[24];
  mono(g, T, label, x, y + 8, 0, T.card);
  g.font(F_BIG);
  if (win.pct < 0) snprintf(b, sizeof b, "-");
  else snprintf(b, sizeof b, "%d%%", (int)roundf(win.pct));
  g.text(b, x, y + 33, 0, a.ok ? T.ink : T.muted, T.card);

  fmtReset(b, sizeof b, win, st);
  g.font(F_BODY);
  g.text(b, x + w, y + 32, 2, T.muted, T.card);

  float p = -1;
  if (pace && win.len && win.reset && st.timeOk)
    p = 100.f * (1.f - (float)((long)win.reset - (long)st.now) / win.len);
  blocks(g, x, y + 39, w, 9, win.pct, w > 200 ? 30 : 20, barColor(a, win.pct, T), T.empty, p, T.muted);
}

static void chip(Gfx& g, const Theme& T, const Agent& a, int right, int y) {
  char up[12];
  upper(up, a.ok ? a.plan : "STALE", sizeof up);
  if (!*up) return;
  g.font(F_MONO);
  int tw = g.textW(up);
  int cx = right - tw - 12;
  g.rrect(cx, y, tw + 12, 16, 8, a.ok ? T.chip : T.warn);
  g.text(up, cx + 6 + tw / 2, y + 11, 1, a.ok ? T.muted : T.card, a.ok ? T.chip : T.warn);
}

static void agentCard(Gfx& g, const Theme& T, int x, int y, int w, int h, const Agent& a,
                      const UiState& st) {
  card(g, T, x, y, w, h);
  g.circle(x + 16, y + 19, 4, agentColor(a, T));
  char name[24], up[12];
  upper(up, a.ok ? a.plan : "STALE", sizeof up);
  g.font(F_MONO);
  int chipW = *up ? g.textW(up) + 12 : 0;
  snprintf(name, sizeof name, "%s", a.name);
  g.font(F_TITLE);
  fit(g, name, w - 26 - 12 - chipW - 8);
  g.text(name, x + 26, y + 24, 0, T.ink, T.card);
  chip(g, T, a, x + w - 12, y + 11);

  int ix = x + 12, iw = w - 24;
  section(g, T, ix, y + 38, iw, "SESSION 5H", a.s, a, st, false);
  g.rect(ix, y + 99, iw, 1, T.line);
  section(g, T, ix, y + 107, iw, "WEEK", a.w, a, st, true);
}

static void cell(Gfx& g, const Theme& T, int x, int w, int top, int mid, bool two,
                 const Window& win, const Agent& a, const UiState& st) {
  char b[16];
  if (win.pct < 0) snprintf(b, sizeof b, "-");
  else snprintf(b, sizeof b, "%d%%", (int)roundf(win.pct));
  g.font(F_TITLE);
  uint32_t on = barColor(a, win.pct, T);
  if (two) {
    g.text(b, x, top + 11, 0, a.ok ? T.ink : T.muted, T.card);
    fmtReset(b, sizeof b, win, st, true);
    mono(g, T, b, x + w, top + 10, 2, T.card);
    blocks(g, x, top + 16, w, 7, win.pct, 14, on, T.empty);
  } else {
    g.text(b, x, mid + 5, 0, a.ok ? T.ink : T.muted, T.card);
    blocks(g, x + 38, mid - 3, w - 38, 7, win.pct, 8, on, T.empty);
  }
}

static void agentTable(Gfx& g, const Theme& T, const Model& m, const UiState& st) {
  const int x = 10, y = 58, w = 300, h = 168;
  const int cx1 = 124, cx2 = 214, cw = 86;
  card(g, T, x, y, w, h);
  mono(g, T, "ACCOUNT", 22, y + 16, 0, T.card);
  mono(g, T, "SESSION 5H", cx1, y + 16, 0, T.card);
  mono(g, T, "WEEK", cx2, y + 16, 0, T.card);
  g.rect(x + 12, y + 22, w - 24, 1, T.line);

  int rowH = (h - 26) / m.n;
  bool two = rowH >= 34;
  for (int i = 0; i < m.n; i++) {
    const Agent& a = m.agents[i];
    int ry = y + 24 + i * rowH;
    if (i) g.rect(x + 12, ry, w - 24, 1, T.line);
    int top = ry + (rowH - 24) / 2;
    int mid = ry + rowH / 2;
    int nameY = two ? top + 11 : mid + 5;

    g.circle(26, nameY - 4, 3, agentColor(a, T));
    char name[24];
    snprintf(name, sizeof name, "%s", a.name);
    g.font(F_TITLE);
    fit(g, name, cx1 - 40);
    g.text(name, 34, nameY, 0, T.ink, T.card);
    if (two) {
      char p[12];
      upper(p, a.ok ? a.plan : "STALE", sizeof p);
      mono(g, T, p, 34, top + 23, 0, T.card, a.ok ? T.muted : T.warn);
    }
    cell(g, T, cx1, cw - 12, top, mid, two, a.s, a, st);
    cell(g, T, cx2, cw, top, mid, two, a.w, a, st);
  }
}

static void home(Gfx& g, const Model& m, const UiState& st, const Theme& T) {
  header(g, m, st, T);
  if (m.n == 0) {
    card(g, T, 10, 58, 300, 168);
    g.font(F_TITLE);
    g.text("Waiting for data", 160, 138, 1, T.muted, T.card);
    mono(g, T, st.ip, 160, 158, 1, T.card, T.faint);
  } else if (m.n == 1) {
    agentCard(g, T, 10, 58, 300, 168, m.agents[0], st);
  } else if (m.n == 2) {
    agentCard(g, T, 10, 58, 147, 168, m.agents[0], st);
    agentCard(g, T, 163, 58, 147, 168, m.agents[1], st);
  } else {
    agentTable(g, T, m, st);
  }
}

static uint32_t shade(const Theme& T, int i) {
  const uint32_t s[] = {T.ink, T.gray, T.claude, T.faint};
  return s[i % 4];
}

static void activity(Gfx& g, const Model& m, const UiState& st, const Theme& T) {
  char b[32];
  mono(g, T, "ACTIVITY \xC2\xB7 LAST 24 HOURS", 14, 22, 0, T.bg);
  mono(g, T, "OF WEEKLY LIMIT", 306, 22, 2, T.bg);

  long mx = 0;
  for (int h = 0; h < 24; h++) {
    long t = 0;
    for (int i = 0; i < m.n; i++) t += m.agents[i].hist[h];
    if (t > mx) mx = t;
  }

  int cols = m.n < 3 ? m.n : 3;
  int colW = 292 / (cols ? cols : 1);
  for (int i = 0; i < cols; i++) {
    const Agent& a = m.agents[i];
    long sum = 0;
    for (int h = 0; h < 24; h++) sum += a.hist[h];
    int cx = 14 + i * colW;
    g.rect(cx, 37, 7, 7, shade(T, i));
    char name[24];
    upper(name, a.name, sizeof name);
    g.font(F_MONO);
    fit(g, name, colW - 16);
    mono(g, T, name, cx + 11, 44, 0, T.bg);
    g.font(cols <= 2 ? F_BIG : F_TITLE);
    snprintf(b, sizeof b, "+%ld.%ld%%", sum / 10, sum % 10);
    g.text(b, cx, cols <= 2 ? 72 : 66, 0, T.ink, T.bg);
  }
  if (m.n > 3) {
    snprintf(b, sizeof b, "+%d MORE", m.n - 3);
    mono(g, T, b, 306, 72, 2, T.bg);
  }

  card(g, T, 10, 84, 300, 142);
  const int rows = 9, cellS = 9, gap = 2, pitch = cellS + gap;
  int gw = 24 * pitch - gap;
  int x0 = 10 + (300 - gw) / 2, top = 104;
  long unit = (mx + rows - 1) / rows;
  if (unit < 1) unit = 1;
  for (int c = 0; c < 24; c++) {
    uint32_t col[rows];
    int r = 0;
    for (int i = 0; i < m.n && r < rows; i++) {
      long v = m.agents[i].hist[c];
      int k = (int)((v + unit / 2) / unit);
      if (v && !k) k = 1;
      for (; k > 0 && r < rows; k--) col[r++] = shade(T, i);
    }
    for (; r < rows; r++) col[r] = T.empty;
    for (r = 0; r < rows; r++) g.rect(x0 + c * pitch, top + (rows - 1 - r) * pitch, cellS, cellS, col[r]);
  }
  g.rect(x0, 90, 7, 7, T.ink);
  snprintf(b, sizeof b, "= %ld.%ld%%", unit / 10, unit % 10);
  mono(g, T, b, x0 + 11, 97, 0, T.card);
  snprintf(b, sizeof b, "PEAK %ld.%ld%%/H", mx / 10, mx % 10);
  mono(g, T, b, x0 + gw, 97, 2, T.card);
  const char* lab[] = {"-24h", "-18h", "-12h", "-6h"};
  for (int i = 0; i < 4; i++) mono(g, T, lab[i], x0 + i * 6 * pitch, 217, 0, T.card);
  mono(g, T, "now", x0 + gw, 217, 2, T.card);
}

static void weather(Gfx& g, const Model& m, const UiState& st, const Theme& T) {
  char b[40];
  const Weather& w = m.wx;
  if (!w.has) {
    mono(g, T, "WEATHER", 14, 22, 0, T.bg);
    g.font(F_BODY);
    g.text("No weather yet", 160, 124, 1, T.muted, T.bg);
    return;
  }
  upper(b, w.city, sizeof b);
  mono(g, T, b, 14, 22, 0, T.bg);
  if (st.timeOk) {
    snprintf(b, sizeof b, "%s \xC2\xB7 %02d:%02d", WD[st.lt.tm_wday], st.lt.tm_hour, st.lt.tm_min);
    mono(g, T, b, 306, 22, 2, T.bg);
  }

  g.font(F_HUGE);
  snprintf(b, sizeof b, "%d\xC2\xB0", (int)roundf(w.t));
  g.text(b, 10, 90, 0, T.ink, T.bg);
  g.font(F_TITLE);
  g.text(wxText(w.code), 14, 114, 0, T.ink, T.bg);
  g.font(F_BODY);
  snprintf(b, sizeof b, "H %d\xC2\xB0   L %d\xC2\xB0", w.hi, w.lo);
  g.text(b, 14, 132, 0, T.muted, T.bg);

  wxIcon(g, T, w.code, w.day, 250, 72, 2.0f, T.bg);
  snprintf(b, sizeof b, "\xE2\x86\x91 %s   \xE2\x86\x93 %s", w.sunrise, w.sunset);
  mono(g, T, b, 306, 132, 2, T.bg);

  card(g, T, 10, 146, 300, 80);
  for (int i = 0; i < 6; i++) {
    int cx = 35 + i * 50;
    snprintf(b, sizeof b, "%02d", w.hHour[i]);
    mono(g, T, i == 0 ? "NOW" : b, cx, 164, 1, T.card);
    wxIcon(g, T, w.hCode[i], w.hHour[i] >= 7 && w.hHour[i] <= 18, cx, 186, 0.8f, T.card);
    g.font(F_BODY);
    snprintf(b, sizeof b, "%d\xC2\xB0", w.hTemp[i]);
    g.text(b, cx + 2, 216, 1, T.ink, T.card);
  }
}

static void tile(Gfx& g, const Theme& T, int x, int y, const char* label, const char* value,
                 const char* note, float bar, uint32_t barC) {
  card(g, T, x, y, 147, 86);
  mono(g, T, label, x + 12, y + 20, 0, T.card);
  g.font(F_BIG);
  g.text(value, x + 12, y + 50, 0, T.ink, T.card);
  if (note && *note) {
    g.font(F_BODY);
    g.text(note, x + 135, y + 49, 2, T.muted, T.card);
  }
  if (bar >= 0) blocks(g, x + 12, y + 62, 123, 8, bar, 20, barC, T.empty);
}

static void pctTile(Gfx& g, const Theme& T, int x, int y, const char* label, bool has, int v,
                    const char* note, bool warn) {
  char b[12];
  if (has) snprintf(b, sizeof b, "%d%%", v);
  else snprintf(b, sizeof b, "-");
  tile(g, T, x, y, label, b, note, has ? v : 0, warn ? T.warn : T.ink);
}

static void system_(Gfx& g, const Model& m, const UiState& st, const Theme& T) {
  char b[32];
  const Host& c = m.pc;
  g.font(F_TITLE);
  snprintf(b, sizeof b, "%s", c.has ? c.name : "Server");
  fit(g, b, 180);
  g.text(b, 14, 27, 0, T.ink, T.bg);
  if (m.updated && st.timeOk) {
    time_t t = m.updated;
    struct tm r;
    localtime_r(&t, &r);
    snprintf(b, sizeof b, "UPDATED %02d:%02d", r.tm_hour, r.tm_min);
    mono(g, T, b, 306, 25, 2, T.bg);
  } else {
    mono(g, T, "OFFLINE", 306, 25, 2, T.bg);
  }

  pctTile(g, T, 10, 42, "CPU", c.has, c.cpu, "", c.cpu >= 85);
  pctTile(g, T, 163, 42, "MEMORY", c.has, c.ram, "", c.ram >= 85);
  pctTile(g, T, 10, 136, "DISK", c.has, c.disk, "", c.disk >= 90);
  if (c.has && c.bat >= 0)
    pctTile(g, T, 163, 136, "BATTERY", true, c.bat, c.chg ? "charging" : "", c.bat <= 15 && !c.chg);
  else
    tile(g, T, 163, 136, "UPTIME", c.has && *c.up ? c.up : "-", "", -1, T.ink);
}

static void focus(Gfx& g, const Model& m, const UiState& st, const Theme& T) {
  char b[32];
  mono(g, T, "FOCUS", 14, 22, 0, T.bg);
  snprintf(b, sizeof b, "TODAY \xC2\xB7 %d %s", st.focusDone, st.focusDone == 1 ? "SESSION" : "SESSIONS");
  mono(g, T, b, 306, 22, 2, T.bg);

  int left = st.focusRun ? (int)((long)st.focusEnd - (long)st.now) : st.focusLeft;
  if (left < 0) left = 0;
  g.font(F_HUGE);
  snprintf(b, sizeof b, "%02d:%02d", left / 60, left % 60);
  g.text(b, 160, 112, 1, T.ink, T.bg);

  float done = 100.f * (st.focusLen - left) / st.focusLen;
  blocks(g, 14, 134, 292, 16, done, st.focusLen / 60, T.ink, T.empty);

  g.font(F_TITLE);
  const char* s = st.focusRun ? "Focusing" : (left == st.focusLen ? "Ready" : (left == 0 ? "Done" : "Paused"));
  g.text(s, 160, 182, 1, st.focusRun ? T.ink : T.muted, T.bg);
  mono(g, T, "TAP CENTER  START / PAUSE    HOLD  RESET", 160, 208, 1, T.bg, T.faint);
}

void drawScreen(Gfx& g, const Model& m, const UiState& st) {
  const Theme& T = st.dark ? DARK : LIGHT;
  g.clear(T.bg);
  switch (st.screen) {
    case SCR_HOME: home(g, m, st, T); break;
    case SCR_ACTIVITY: activity(g, m, st, T); break;
    case SCR_WEATHER: weather(g, m, st, T); break;
    case SCR_SYSTEM: system_(g, m, st, T); break;
    case SCR_FOCUS: focus(g, m, st, T); break;
  }
  pageDots(g, T, st);
}
