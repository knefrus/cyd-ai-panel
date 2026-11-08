#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <sys/time.h>
#if __has_include("config.h")
#include "config.h"
#else
#error "Copy src/config.example.h to src/config.h and fill it in"
#endif
#include "lgfx_cyd.h"

#ifndef TOUCH_FLIP_X
#define TOUCH_FLIP_X 0
#endif
#ifndef TOUCH_FLIP_Y
#define TOUCH_FLIP_Y 0
#endif
#include "ui.h"

static LGFX lcd;
static LGFX_Sprite strip(&lcd);
static WebServer server(80);
static Model model;
static UiState st;
static String baseUrl, query;
static bool otaActive = false;

static const int PIN_BOOT = 0, PIN_SPK = 26, BEEP_CH = 4;
static const int LEDS[] = {4, 16, 17};

struct GifCache {
  char id[20] = "";
  File f;
  uint16_t w = 0, h = 0, n = 0;
  uint16_t delay[64];
  uint32_t total = 0;
  uint32_t pal[256];
  uint32_t data = 0;
};
static GifCache gifs[2];
static int gifNext = 0;

static GifCache* openGif(const char* id) {
  for (auto& g : gifs)
    if (!strcmp(g.id, id) && g.f) return &g;
  GifCache& g = gifs[gifNext];
  gifNext = (gifNext + 1) % 2;
  if (g.f) g.f.close();
  g.id[0] = 0;
  char path[40];
  snprintf(path, sizeof path, "/a/%s.bin", id);
  g.f = LittleFS.open(path, "r");
  if (!g.f) return nullptr;
  uint8_t hd[10];
  if (g.f.read(hd, 10) != 10 || memcmp(hd, "CYDG", 4)) { g.f.close(); return nullptr; }
  g.w = hd[4] | hd[5] << 8;
  g.h = hd[6] | hd[7] << 8;
  g.n = hd[8] | hd[9] << 8;
  g.total = 0;
  for (int i = 0; i < g.n; i++) {
    uint8_t d[2];
    g.f.read(d, 2);
    if (i < 64) {
      g.delay[i] = d[0] | d[1] << 8;
      g.total += g.delay[i];
    }
  }
  if (g.n > 64) g.n = 64;
  uint8_t raw[512];
  g.f.read(raw, 512);
  for (int i = 0; i < 256; i++) {
    uint16_t c = raw[i * 2] | raw[i * 2 + 1] << 8;
    g.pal[i] = ((c >> 11) << 19) | (((c >> 5) & 63) << 10) | ((c & 31) << 3);
  }
  g.data = 10 + 2 * (hd[8] | hd[9] << 8) + 512;
  strlcpy(g.id, id, sizeof g.id);
  return &g;
}

class StripGfx : public Gfx {
 public:
  LGFX_Sprite& s;
  int oy = 0;
  const uint8_t* cur = nullptr;
  explicit StripGfx(LGFX_Sprite& sp) : s(sp) {}
  void clear(uint32_t c) override { s.fillScreen(c); }
  void rect(int x, int y, int w, int h, uint32_t c) override { s.fillRect(x, y - oy, w, h, c); }
  void rrect(int x, int y, int w, int h, int r, uint32_t c) override { s.fillRoundRect(x, y - oy, w, h, r, c); }
  void circle(int x, int y, int r, uint32_t c) override { s.fillCircle(x, y - oy, r, c); }
  void font(const uint8_t* f) override {
    if (f != cur) {
      s.loadFont(f);
      cur = f;
    }
  }
  void text(const char* str, int x, int y, int al, uint32_t fg, uint32_t bg) override {
    static const textdatum_t D[] = {textdatum_t::baseline_left, textdatum_t::baseline_center,
                                    textdatum_t::baseline_right};
    s.setTextDatum(D[al]);
    s.setTextColor(fg, bg);
    s.drawString(str, x, y - oy);
  }
  int textW(const char* str) override { return s.textWidth(str); }
  bool gif(const char* id, int x, int y, int w, int h, uint32_t ms, uint32_t bg) override {
    GifCache* g = openGif(id);
    if (!g) return false;
    uint32_t t = g->total ? ms % g->total : 0;
    int fr = 0;
    while (fr < g->n - 1 && t >= g->delay[fr]) t -= g->delay[fr++];
    int ox = x + (w - g->w) / 2, top = y + (h - g->h) / 2;
    static uint8_t idx[320];
    int n = g->w > 320 ? 320 : g->w;
    for (int j = 0; j < g->h; j++) {
      int sy = top + j - oy;
      if (sy < 0 || sy >= s.height()) continue;
      g->f.seek(g->data + ((uint32_t)fr * g->h + j) * g->w);
      g->f.read(idx, n);
      for (int i = 0; i < n;) {
        int k = i;
        while (k < n && idx[k] == idx[i]) k++;
        if (idx[i] != 255) s.drawFastHLine(ox + i, sy, k - i, g->pal[idx[i]]);
        i = k;
      }
    }
    return true;
  }
};
static StripGfx gfx(strip);

static void render() {
  for (int half = 0; half < 2; half++) {
    gfx.oy = half * 120;
    drawScreen(gfx, model, st);
    strip.pushSprite(0, half * 120);
  }
}

static void renderWidget(const Widget& w) {
  int x, y, W, H;
  cellRect(w, x, y, W, H);
  lcd.setClipRect(x, y, W, H);
  for (int half = 0; half < 2; half++) {
    if (y + H <= half * 120 || y >= half * 120 + 120) continue;
    gfx.oy = half * 120;
    drawWidget(gfx, model, st, w);
    strip.pushSprite(0, half * 120);
  }
  lcd.clearClipRect();
}

static void applyTz() {
  const char* tz = *model.set.tz ? model.set.tz : TZ_POSIX;
  static char cur[48] = "";
  if (strcmp(cur, tz)) {
    strlcpy(cur, tz, sizeof cur);
    setenv("TZ", cur, 1);
    tzset();
  }
}

static bool loadBody(const String& body) {
  if (!parseModel(body.c_str(), body.length(), model)) return false;
  model.updated = time(nullptr);
  applyTz();
  return true;
}

static bool httpGet(const String& path, String& out, int timeout = 6000) {
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure tls;
  bool https = baseUrl.startsWith("https");
  if (https) tls.setInsecure();
  http.setTimeout(timeout);
  if (!http.begin(https ? (WiFiClient&)tls : plain, baseUrl + path + query)) return false;
  int code = http.GET();
  if (code == 200) out = http.getString();
  http.end();
  return code == 200;
}

static bool download(const char* id) {
  char path[40];
  snprintf(path, sizeof path, "/a/%s.bin", id);
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure tls;
  bool https = baseUrl.startsWith("https");
  if (https) tls.setInsecure();
  http.setTimeout(15000);
  if (!http.begin(https ? (WiFiClient&)tls : plain, baseUrl + "/a/" + id + ".bin" + query)) return false;
  bool ok = false;
  if (http.GET() == 200) {
    String tmp = String(path) + ".tmp";
    File f = LittleFS.open(tmp, "w");
    if (f) {
      int n = http.writeToStream(&f);
      f.close();
      ok = n > 0 && LittleFS.rename(tmp, path);
      if (!ok) LittleFS.remove(tmp);
    }
  }
  http.end();
  return ok;
}

static bool referenced(const char* name) {
  for (int s = 0; s < model.nScreens; s++)
    for (int i = 0; i < model.screens[s].n; i++) {
      const Widget& w = model.screens[s].w[i];
      if (w.type == W_GIF && w.asset[0] && !strncmp(name, w.asset, strlen(w.asset))) return true;
    }
  return false;
}

static void syncAssets() {
  for (int s = 0; s < model.nScreens; s++)
    for (int i = 0; i < model.screens[s].n; i++) {
      const Widget& w = model.screens[s].w[i];
      if (w.type != W_GIF || !w.asset[0]) continue;
      char path[40];
      snprintf(path, sizeof path, "/a/%s.bin", w.asset);
      if (!LittleFS.exists(path)) download(w.asset);
    }
  File dir = LittleFS.open("/a");
  if (!dir) return;
  String stale[16];
  int n = 0;
  for (File f = dir.openNextFile(); f && n < 16; f = dir.openNextFile())
    if (!referenced(f.name())) stale[n++] = String("/a/") + f.name();
  dir.close();
  for (int i = 0; i < n; i++) {
    for (auto& g : gifs)
      if (g.f) { g.f.close(); g.id[0] = 0; }
    LittleFS.remove(stale[i]);
  }
}

static void pull(bool force) {
  if (!baseUrl.length()) return;
  String v;
  if (!force && httpGet("/v", v, 3000) && v == model.version) {
    model.updated = time(nullptr);
    return;
  }
  String body;
  if (!httpGet("/panel.json", body)) return;
  if (!loadBody(body)) return;
  File f = LittleFS.open("/last.json", "w");
  if (f) {
    f.print(body);
    f.close();
  }
  syncAssets();
}

static void handleUpdate() {
  if (strlen(PANEL_TOKEN) && server.header("X-Token") != PANEL_TOKEN) {
    server.send(403, "text/plain", "bad token");
    return;
  }
  if (loadBody(server.arg("plain"))) server.send(200, "application/json", "{\"ok\":true}");
  else server.send(400, "text/plain", "bad json");
}

static void handleRoot() {
  char b[200];
  snprintf(b, sizeof b, "{\"panel\":\"%s\",\"ip\":\"%s\",\"heap\":%u,\"screens\":%d,\"version\":\"%s\"}", HOSTNAME,
           st.ip, (unsigned)ESP.getFreeHeap(), model.nScreens, model.version);
  server.send(200, "application/json", b);
}

static void handleScreen() {
  int n = server.arg("n").toInt();
  if (n >= 0 && n < screenCount(model)) st.screen = n;
  server.send(200, "text/plain", "ok");
}

static void otaScreen(int pct) {
  lcd.fillRect(60, 110, 200, 20, 0x0D0D0D);
  lcd.drawRect(60, 110, 200, 20, 0x7A7A7A);
  lcd.fillRect(62, 112, 196 * pct / 100, 16, 0xECECEC);
}

static bool netUp = false;

static void netLoop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (netUp) {
      netUp = false;
      strlcpy(st.ip, "no wifi", sizeof st.ip);
    }
    return;
  }
  if (!netUp) {
    netUp = true;
    WiFi.setSleep(false);
    strlcpy(st.ip, WiFi.localIP().toString().c_str(), sizeof st.ip);
    MDNS.begin(HOSTNAME);
    MDNS.addService("http", "tcp", 80);
    configTzTime(*model.set.tz ? model.set.tz : TZ_POSIX, "pool.ntp.org", "time.google.com", "ru.pool.ntp.org");
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.onStart([] {
      otaActive = true;
      lcd.fillScreen(0x0D0D0D);
      otaScreen(0);
    });
    ArduinoOTA.onProgress([](unsigned a, unsigned b) { otaScreen(b ? a * 100 / b : 0); });
    ArduinoOTA.onError([](ota_error_t) { otaActive = false; });
    ArduinoOTA.begin();
    Serial.printf("wifi ok: %s (%s.local)\n", st.ip, HOSTNAME);
    pull(true);
  }
  server.handleClient();
  ArduinoOTA.handle();
  static uint32_t last = 0;
  if (millis() - last > 5000) {
    last = millis();
    pull(false);
  }
}

static void beep(int n) {
  for (int i = 0; i < n; i++) {
    ledcWriteTone(BEEP_CH, 1760);
    delay(90);
    ledcWriteTone(BEEP_CH, 0);
    delay(110);
  }
}

static void focusToggle() {
  uint32_t now = time(nullptr);
  if (st.focusRun) {
    st.focusLeft = max<long>(0, (long)st.focusEnd - (long)now);
    st.focusRun = false;
  } else {
    if (st.focusLeft <= 0) st.focusLeft = st.focusLen;
    st.focusEnd = now + st.focusLeft;
    st.focusRun = true;
  }
}

static int focusScreen() {
  for (int s = 0; s < model.nScreens; s++)
    for (int i = 0; i < model.screens[s].n; i++)
      if (model.screens[s].w[i].type == W_FOCUS) return s;
  return -1;
}

static void focusTick() {
  if (!st.focusRun || st.now < st.focusEnd) return;
  st.focusRun = false;
  st.focusLeft = 0;
  st.focusDone++;
  int s = focusScreen();
  if (s >= 0) st.screen = s;
  lcd.setBrightness(BRIGHT_DAY);
  beep(3);
}

static uint32_t lastInput = 0;
static bool themeFlip = false;

static void step(int d) {
  int n = screenCount(model);
  st.screen = (st.screen + n + d) % n;
}

static const Widget* hit(int x, int y) {
  int i = widgetAt(model, st.screen, x, y);
  return i < 0 ? nullptr : &model.screens[st.screen].w[i];
}

static void onTap(int x, int y) {
  const Widget* w = hit(x, y);
  if (w && w->type == W_FOCUS) return focusToggle();
  if (x < 107) step(-1);
  else if (x > 213) step(1);
}

static void onHold(int x, int y) {
  const Widget* w = hit(x, y);
  if (w && w->type == W_FOCUS) {
    st.focusRun = false;
    st.focusLeft = st.focusLen;
  } else if (w && w->type == W_CLOCK) {
    themeFlip = !themeFlip;
  } else {
    st.screen = 0;
  }
}

static bool inputLoop() {
  static bool down = false, held = false, bootPrev = true;
  static uint32_t t0 = 0, lastSeen = 0;
  static int32_t sx = 0, sy = 0, lx = 0, ly = 0;
  int32_t x, y;
  uint32_t ms = millis();
  bool changed = false;

  if (lcd.getTouch(&x, &y)) {
    if (TOUCH_FLIP_X) x = 319 - x;
    if (TOUCH_FLIP_Y) y = 239 - y;
    if (!down) {
      down = true;
      held = false;
      t0 = ms;
      sx = lx = x;
      sy = ly = y;
    }
    lx = x;
    ly = y;
    lastSeen = ms;
    if (!held && ms - t0 > 800 && abs(lx - sx) < 30 && abs(ly - sy) < 30) {
      held = true;
      onHold(sx, sy);
      changed = true;
    }
  } else if (down && ms - lastSeen > 80) {
    down = false;
    if (!held) {
      int dx = lx - sx, dy = ly - sy;
      if (abs(dx) > 50 && abs(dx) > abs(dy)) step(dx < 0 ? 1 : -1);
      else onTap(sx, sy);
      changed = true;
    }
  }

  bool boot = digitalRead(PIN_BOOT);
  if (!boot && bootPrev) {
    step(1);
    changed = true;
  }
  bootPrev = boot;

  if (changed) lastInput = ms;
  return changed;
}

static int setting(int v, int def) { return v >= 0 ? v : def; }

static bool isNight() {
  if (!st.timeOk) return false;
  int h = st.lt.tm_hour, from = setting(model.set.nightFrom, NIGHT_FROM), to = setting(model.set.nightTo, NIGHT_TO);
  return from > to ? (h >= from || h < to) : (h >= from && h < to);
}

static void tick() {
  time_t now = time(nullptr);
  st.now = now;
  st.ms = millis();
  st.timeOk = now > 1700000000;
  localtime_r(&now, &st.lt);
  if (st.screen >= screenCount(model)) st.screen = 0;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  for (int p : LEDS) {
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);
  }
  ledcSetup(BEEP_CH, 2000, 8);
  ledcAttachPin(PIN_SPK, BEEP_CH);

  lcd.init();
  lcd.setRotation(SCREEN_ROTATION);
  lcd.setBrightness(BRIGHT_DAY);
  strip.setColorDepth(16);
  strip.createSprite(320, 120);

  strlcpy(st.ip, "connecting", sizeof st.ip);
  defaultScreens(model);
  if (LittleFS.begin(true)) {
    LittleFS.mkdir("/a");
    File f = LittleFS.open("/last.json", "r");
    if (f) {
      String body = f.readString();
      f.close();
      loadBody(body);
      model.updated = 0;
      model.version[0] = 0;
    }
  }
  applyTz();

  String url = DATA_URL;
  int q = url.indexOf('?');
  if (url.length()) {
    String path = q < 0 ? url : url.substring(0, q);
    query = q < 0 ? "" : url.substring(q);
    int slash = path.lastIndexOf('/');
    baseUrl = slash > 8 ? path.substring(0, slash) : path;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/update", HTTP_POST, handleUpdate);
  server.on("/screen", HTTP_POST, handleScreen);
  const char* hdrs[] = {"X-Token"};
  server.collectHeaders(hdrs, 1);
  server.begin();
  tick();
  render();
}

void loop() {
  netLoop();
  if (otaActive) return;
  bool dirty = inputLoop();
  uint32_t ms = millis();

  static uint32_t lastDraw = 0, lastAnim = 0;
  if (dirty || ms - lastDraw >= 1000) {
    lastDraw = lastAnim = ms;
    tick();
    static int yday = -1;
    if (st.timeOk && st.lt.tm_yday != yday) {
      yday = st.lt.tm_yday;
      st.focusDone = 0;
    }
    bool night = isNight();
    int theme = setting(model.set.theme, THEME_MODE);
    static int lastTheme = -2;
    if (theme != lastTheme) {
      lastTheme = theme;
      themeFlip = false;
    }
    st.dark = (theme == 1 || (theme == 2 && night)) != themeFlip;
    lcd.setBrightness(night ? setting(model.set.brightNight, BRIGHT_NIGHT) : setting(model.set.brightDay, BRIGHT_DAY));

    int homeAfter = setting(model.set.homeAfter, 120);
    if (homeAfter > 0 && st.screen != 0 && !st.focusRun && ms - lastInput > (uint32_t)homeAfter * 1000) st.screen = 0;

    focusTick();
    render();
  } else if (ms - lastAnim >= 90 && st.screen < model.nScreens) {
    lastAnim = ms;
    st.ms = ms;
    const ScreenDef& sd = model.screens[st.screen];
    for (int i = 0; i < sd.n; i++)
      if (widgetAnimated(model, sd.w[i])) renderWidget(sd.w[i]);
  }
  delay(5);
}
