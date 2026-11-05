#pragma once
#include <stdint.h>
#include <time.h>

#define MAX_AGENTS 6

struct Window {
  float pct = -1;
  uint32_t reset = 0;
  uint32_t len = 0;
};

struct Agent {
  bool ok = false;
  char name[16] = "";
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
  char sunrise[6] = "", sunset[6] = "", city[24] = "";
};

struct Host {
  bool has = false;
  int cpu = 0, ram = 0, bat = -1, disk = 0;
  bool chg = false;
  char name[24] = "", up[16] = "";
};

struct Model {
  Agent agents[MAX_AGENTS];
  int n = 0;
  Weather wx;
  Host pc;
  uint32_t updated = 0;
};

enum Screen { SCR_HOME, SCR_ACTIVITY, SCR_WEATHER, SCR_SYSTEM, SCR_FOCUS, SCR_COUNT };

struct UiState {
  int screen = SCR_HOME;
  bool dark = false;
  bool timeOk = false;
  uint32_t now = 0;
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
};

void drawScreen(Gfx& g, const Model& m, const UiState& st);
