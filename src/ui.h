#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define MAX_AGENTS 6
#define MAX_SCREENS 6
#define MAX_WIDGETS 12
#define MAX_EVENTS 16
#define MAX_TARGETS 12
#define MAX_NODES 16
#define TEXT_POOL 3072

#define GRID_COLS 6
#define GRID_ROWS 4

struct Window {
  float pct = -1;
  uint32_t reset = 0;
  uint32_t len = 0;
};

struct Agent {
  bool ok = false;
  char name[24] = "";
  char kind[8] = "";
  char plan[12] = "";
  uint32_t color = 0;
  Window s, w;
  uint16_t hist[24] = {0};
};

struct Weather {
  bool has = false;
  bool day = true;
  float t = 0;
  int code = 0, hi = 0, lo = 0;
  int hHour[6] = {0}, hTemp[6] = {0}, hCode[6] = {0};
  char sunrise[6] = "", sunset[6] = "", city[32] = "";
};

struct Host {
  bool has = false;
  int cpu = 0, ram = 0, bat = -1, disk = 0;
  bool chg = false;
  char name[24] = "", up[16] = "";
};

struct Event {
  uint32_t start = 0, end = 0;
  uint32_t color = 0;
  bool allDay = false;
  char title[64] = "";
};

struct Target {
  char name[24] = "";
  int16_t ms = -1;
  uint8_t hist[24] = {0};
};

struct NodeRow {
  char name[20] = "";
  uint8_t state = 0;
  int16_t users = 0;
  int8_t cpu = -1, ram = -1;
};

enum WType : uint8_t {
  W_CLOCK, W_AGENT, W_AGENTS, W_WEATHER, W_CALENDAR, W_TEXT,
  W_PING, W_PET, W_GIF, W_ACTIVITY, W_HOST, W_FOCUS, W_NODES, W_TYPES
};

struct Widget {
  uint8_t type = W_CLOCK;
  uint8_t x = 0, y = 0, w = 1, h = 1;
  bool card = true;
  int8_t i = 0;
  uint8_t size = 0;
  uint8_t align = 0;
  uint32_t color = 0;
  char title[32] = "";
  char asset[20] = "";
  int16_t text = -1;
};

struct ScreenDef {
  Widget w[MAX_WIDGETS];
  uint8_t n = 0;
};

struct Settings {
  int theme = -1;
  int nightFrom = -1, nightTo = -1;
  int brightDay = -1, brightNight = -1;
  int homeAfter = -1;
  char tz[48] = "";
};

struct Model {
  Agent agents[MAX_AGENTS];
  int n = 0;
  Weather wx;
  Host pc;
  Event ev[MAX_EVENTS];
  int nEv = 0;
  Target pings[MAX_TARGETS];
  int nPing = 0;
  NodeRow nodes[MAX_NODES];
  int nNodes = 0;
  bool hasNodes = false;
  bool nodesStale = false;
  ScreenDef screens[MAX_SCREENS];
  int nScreens = 0;
  Settings set;
  char pool[TEXT_POOL];
  int poolUsed = 0;
  uint32_t updated = 0;
  char version[20] = "";

  const char* text(int16_t off) const { return off < 0 ? "" : pool + off; }
};

struct UiState {
  int screen = 0;
  bool dark = false;
  bool timeOk = false;
  uint32_t now = 0;
  uint32_t ms = 0;
  struct tm lt {};
  char ip[16] = "";
  bool focusRun = false;
  uint32_t focusEnd = 0;
  int focusLeft = 25 * 60;
  int focusLen = 25 * 60;
  int focusDone = 0;
};

class Gfx {
 public:
  virtual ~Gfx() {}
  virtual void clear(uint32_t c) = 0;
  virtual void rect(int x, int y, int w, int h, uint32_t c) = 0;
  virtual void rrect(int x, int y, int w, int h, int r, uint32_t c) = 0;
  virtual void circle(int x, int y, int r, uint32_t c) = 0;
  virtual void font(const uint8_t* f) = 0;
  virtual void text(const char* s, int x, int y, int align, uint32_t fg, uint32_t bg) = 0;
  virtual int textW(const char* s) = 0;
  virtual bool gif(const char* id, int x, int y, int w, int h, uint32_t ms, uint32_t bg) = 0;
};

bool parseModel(const char* json, size_t len, Model& m);
void defaultScreens(Model& m);

void cellRect(const Widget& w, int& x, int& y, int& pw, int& ph);
int screenCount(const Model& m);
int widgetAt(const Model& m, int screen, int x, int y);
bool widgetAnimated(const Model& m, const Widget& w);

void drawScreen(Gfx& g, const Model& m, const UiState& st);
void drawWidget(Gfx& g, const Model& m, const UiState& st, const Widget& w);
