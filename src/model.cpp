#include "ui.h"
#include "json.h"
#include <string.h>

static const char* TYPES[W_TYPES] = {"clock", "agent", "agents", "weather", "calendar", "text",
                                     "ping", "pet", "gif", "activity", "host", "focus", "nodes"};

static int typeOf(const char* s) {
  for (int i = 0; i < W_TYPES; i++)
    if (!strcmp(TYPES[i], s)) return i;
  return -1;
}

static int16_t poolAdd(Model& m, const Json& j, int t) {
  if (t < 0) return -1;
  int room = TEXT_POOL - m.poolUsed;
  if (room < 2) return -1;
  int16_t off = m.poolUsed;
  size_t n = j.str(t, m.pool + off, room);
  m.poolUsed += n + 1;
  return off;
}

static void readWindow(const Json& j, int t, Window& w) {
  w = Window();
  if (j.type(t) != J_ARR) return;
  w.pct = j.dbl(j.at(t, 0), -1);
  w.reset = j.num(j.at(t, 1));
  w.len = j.num(j.at(t, 2));
}

static void readAgents(const Json& j, int arr, Model& m) {
  m.n = 0;
  for (int k = 0; k < j.size(arr) && m.n < MAX_AGENTS; k++) {
    int v = j.at(arr, k);
    Agent& a = m.agents[m.n++];
    a = Agent();
    j.str(j.get(v, "n"), a.name, sizeof a.name);
    j.str(j.get(v, "k"), a.kind, sizeof a.kind);
    j.str(j.get(v, "plan"), a.plan, sizeof a.plan);
    a.ok = j.flag(j.get(v, "ok"));
    a.color = j.hex(j.get(v, "c"));
    readWindow(j, j.get(v, "s"), a.s);
    readWindow(j, j.get(v, "w"), a.w);
    int h = j.get(v, "h");
    for (int i = 0; i < 24; i++) a.hist[i] = j.num(j.at(h, i));
  }
}

static void readWeather(const Json& j, int w, Weather& x) {
  x = Weather();
  if (j.type(w) != J_OBJ) return;
  x.has = true;
  x.t = j.dbl(j.get(w, "t"));
  x.code = j.num(j.get(w, "c"));
  x.day = j.flag(j.get(w, "d"), true);
  x.hi = j.num(j.get(w, "hi"));
  x.lo = j.num(j.get(w, "lo"));
  int h = j.get(w, "h");
  for (int i = 0; i < 6; i++) {
    int r = j.at(h, i);
    x.hHour[i] = j.num(j.at(r, 0));
    x.hTemp[i] = j.num(j.at(r, 1));
    x.hCode[i] = j.num(j.at(r, 2));
  }
  j.str(j.get(w, "sr"), x.sunrise, sizeof x.sunrise);
  j.str(j.get(w, "ss"), x.sunset, sizeof x.sunset);
  j.str(j.get(w, "city"), x.city, sizeof x.city);
}

static void readHost(const Json& j, int p, Host& c) {
  c = Host();
  if (j.type(p) != J_OBJ) return;
  c.has = true;
  j.str(j.get(p, "name"), c.name, sizeof c.name);
  j.str(j.get(p, "up"), c.up, sizeof c.up);
  c.cpu = j.num(j.get(p, "cpu"));
  c.ram = j.num(j.get(p, "ram"));
  c.bat = j.num(j.get(p, "bat"), -1);
  c.chg = j.flag(j.get(p, "chg"));
  c.disk = j.num(j.get(p, "disk"));
}

static void readEvents(const Json& j, int arr, Model& m) {
  m.nEv = 0;
  for (int k = 0; k < j.size(arr) && m.nEv < MAX_EVENTS; k++) {
    int v = j.at(arr, k);
    Event& e = m.ev[m.nEv++];
    e.start = j.num(j.at(v, 0));
    e.end = j.num(j.at(v, 1));
    j.str(j.at(v, 2), e.title, sizeof e.title);
    e.color = j.hex(j.at(v, 3));
    e.allDay = j.flag(j.at(v, 4));
  }
}

static void readPings(const Json& j, int arr, Model& m) {
  m.nPing = 0;
  for (int k = 0; k < j.size(arr) && m.nPing < MAX_TARGETS; k++) {
    int v = j.at(arr, k);
    Target& t = m.pings[m.nPing++];
    t = Target();
    j.str(j.at(v, 0), t.name, sizeof t.name);
    t.ms = j.num(j.at(v, 1), -1);
    int h = j.at(v, 2);
    for (int i = 0; i < 24; i++) t.hist[i] = j.num(j.at(h, i));
  }
}

static void readNodes(const Json& j, int v, Model& m) {
  m.nNodes = 0;
  m.hasNodes = j.type(v) == J_OBJ;
  if (!m.hasNodes) return;
  m.nodesStale = j.flag(j.get(v, "st"));
  int arr = j.get(v, "list");
  for (int k = 0; k < j.size(arr) && m.nNodes < MAX_NODES; k++) {
    int r = j.at(arr, k);
    NodeRow& n = m.nodes[m.nNodes++];
    j.str(j.at(r, 0), n.name, sizeof n.name);
    n.state = j.num(j.at(r, 1));
    n.users = j.num(j.at(r, 2));
    n.cpu = j.num(j.at(r, 3), -1);
    n.ram = j.num(j.at(r, 4), -1);
  }
}

static void readScreens(const Json& j, int arr, Model& m) {
  m.nScreens = 0;
  for (int s = 0; s < j.size(arr) && m.nScreens < MAX_SCREENS; s++) {
    int ws = j.at(arr, s);
    ScreenDef& sd = m.screens[m.nScreens++];
    sd.n = 0;
    for (int k = 0; k < j.size(ws) && sd.n < MAX_WIDGETS; k++) {
      int v = j.at(ws, k);
      char type[12];
      j.str(j.get(v, "t"), type, sizeof type);
      int ty = typeOf(type);
      if (ty < 0) continue;
      Widget& w = sd.w[sd.n++];
      w = Widget();
      w.type = ty;
      w.x = j.num(j.get(v, "x"));
      w.y = j.num(j.get(v, "y"));
      w.w = j.num(j.get(v, "w"), 1);
      w.h = j.num(j.get(v, "h"), 1);
      w.card = j.flag(j.get(v, "card"), ty != W_CLOCK && ty != W_GIF);
      w.i = j.num(j.get(v, "i"));
      w.size = j.num(j.get(v, "size"));
      w.align = j.num(j.get(v, "align"));
      w.color = j.hex(j.get(v, "color"));
      j.str(j.get(v, "title"), w.title, sizeof w.title);
      j.str(j.get(v, "a"), w.asset, sizeof w.asset);
      w.text = poolAdd(m, j, j.get(v, "text"));
    }
  }
}

static void readSettings(const Json& j, int v, Settings& s) {
  s = Settings();
  if (j.type(v) != J_OBJ) return;
  s.theme = j.num(j.get(v, "theme"), -1);
  s.nightFrom = j.num(j.get(v, "nf"), -1);
  s.nightTo = j.num(j.get(v, "nt"), -1);
  s.brightDay = j.num(j.get(v, "bd"), -1);
  s.brightNight = j.num(j.get(v, "bn"), -1);
  s.homeAfter = j.num(j.get(v, "home"), -1);
  j.str(j.get(v, "tz"), s.tz, sizeof s.tz);
}

bool parseModel(const char* json, size_t len, Model& m) {
  Json j;
  if (!j.parse(json, len)) return false;
  int r = j.root();
  if (j.type(r) != J_OBJ) return false;
  m.poolUsed = 0;
  readAgents(j, j.get(r, "agents"), m);
  readWeather(j, j.get(r, "wx"), m.wx);
  readHost(j, j.get(r, "pc"), m.pc);
  readEvents(j, j.get(r, "cal"), m);
  readPings(j, j.get(r, "ping"), m);
  readNodes(j, j.get(r, "nodes"), m);
  readSettings(j, j.get(r, "set"), m.set);
  j.str(j.get(r, "v"), m.version, sizeof m.version);
  int scr = j.get(r, "screens");
  if (j.type(scr) == J_ARR && j.size(scr)) readScreens(j, scr, m);
  else defaultScreens(m);
  return true;
}

static Widget mk(uint8_t type, uint8_t x, uint8_t y, uint8_t w, uint8_t h, int8_t i = 0, bool card = true) {
  Widget r;
  r.type = type;
  r.x = x;
  r.y = y;
  r.w = w;
  r.h = h;
  r.i = i;
  r.card = card;
  return r;
}

void defaultScreens(Model& m) {
  m.nScreens = 5;
  ScreenDef* s = m.screens;
  s[0].n = 0;
  s[0].w[s[0].n++] = mk(W_CLOCK, 0, 0, 3, 1, 0, false);
  s[0].w[s[0].n++] = mk(W_WEATHER, 3, 0, 3, 1, 0, false);
  s[0].w[s[0].n++] = mk(W_AGENT, 0, 1, 3, 3, 0);
  s[0].w[s[0].n++] = mk(W_AGENT, 3, 1, 3, 3, 1);
  s[1].n = 0;
  s[1].w[s[1].n++] = mk(W_ACTIVITY, 0, 0, 6, 4);
  s[2].n = 0;
  s[2].w[s[2].n++] = mk(W_WEATHER, 0, 0, 6, 4, 0, false);
  s[3].n = 0;
  s[3].w[s[3].n++] = mk(W_HOST, 0, 0, 6, 4, 0, false);
  s[4].n = 0;
  s[4].w[s[4].n++] = mk(W_FOCUS, 0, 0, 6, 4, 0, false);
}

static const int GX = 10, GY = 10, GAP = 6, CW = 45, RH = 49;

void cellRect(const Widget& w, int& x, int& y, int& pw, int& ph) {
  x = GX + w.x * (CW + GAP);
  y = GY + w.y * (RH + GAP);
  pw = w.w * CW + (w.w - 1) * GAP;
  ph = w.h * RH + (w.h - 1) * GAP;
}

int screenCount(const Model& m) { return m.nScreens ? m.nScreens : 1; }

int widgetAt(const Model& m, int screen, int px, int py) {
  if (screen >= m.nScreens) return -1;
  const ScreenDef& s = m.screens[screen];
  for (int i = 0; i < s.n; i++) {
    int x, y, w, h;
    cellRect(s.w[i], x, y, w, h);
    if (px >= x && px < x + w && py >= y && py < y + h) return i;
  }
  return -1;
}

bool widgetAnimated(const Model& m, const Widget& w) {
  return w.type == W_PET || (w.type == W_GIF && w.asset[0]);
}
