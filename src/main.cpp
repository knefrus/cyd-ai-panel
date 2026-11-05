#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <sys/time.h>
#if __has_include("config.h")
#include "config.h"
#else
#error "Copy src/config.example.h to src/config.h and fill it in"
#endif
#include "lgfx_cyd.h"
#include "ui.h"

static LGFX lcd;
static LGFX_Sprite strip(&lcd);
static WebServer server(80);
static Model model;
static UiState st;

static const int PIN_BOOT = 0, PIN_SPK = 26, BEEP_CH = 4;
static const int LEDS[] = {4, 16, 17};

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
    if (f != cur) { s.loadFont(f); cur = f; }
  }
  void text(const char* str, int x, int y, int al, uint32_t fg, uint32_t bg) override {
    static const textdatum_t D[] = {textdatum_t::baseline_left, textdatum_t::baseline_center,
                                    textdatum_t::baseline_right};
    s.setTextDatum(D[al]);
    s.setTextColor(fg, bg);
    s.drawString(str, x, y - oy);
  }
  int textW(const char* str) override { return s.textWidth(str); }
};
static StripGfx gfx(strip);

static void render() {
  for (int half = 0; half < 2; half++) {
    gfx.oy = half * 120;
    drawScreen(gfx, model, st);
    strip.pushSprite(0, half * 120);
  }
}

static void readWindow(JsonVariantConst v, Window& w) {
  w = Window();
  if (v.isNull()) return;
  w.pct = v[0] | -1.0f;
  w.reset = v[1] | 0u;
  w.len = v[2] | 0u;
}

static void readAgent(JsonVariantConst v, Agent& a) {
  strlcpy(a.name, v["n"] | "", sizeof a.name);
  strlcpy(a.kind, v["k"] | "", sizeof a.kind);
  strlcpy(a.plan, v["plan"] | "", sizeof a.plan);
  a.ok = v["ok"] | false;
  a.color = strtoul(v["c"] | "0", nullptr, 16);
  readWindow(v["s"], a.s);
  readWindow(v["w"], a.w);
  for (int i = 0; i < 24; i++) a.hist[i] = v["h"][i] | 0;
}

static void readWeather(JsonVariantConst w) {
  if (w.isNull()) return;
  Weather& x = model.wx;
  x.has = true;
  x.t = w["t"] | 0.0f;
  x.code = w["c"] | 0;
  x.day = w["d"] | 1;
  x.hi = w["hi"] | 0;
  x.lo = w["lo"] | 0;
  for (int i = 0; i < 6; i++) {
    x.hHour[i] = w["h"][i][0] | 0;
    x.hTemp[i] = w["h"][i][1] | 0;
    x.hCode[i] = w["h"][i][2] | 0;
  }
  strlcpy(x.sunrise, w["sr"] | "", sizeof x.sunrise);
  strlcpy(x.sunset, w["ss"] | "", sizeof x.sunset);
  strlcpy(x.city, w["city"] | "", sizeof x.city);
}

static void readHost(JsonVariantConst p) {
  if (p.isNull()) return;
  Host& c = model.pc;
  c.has = true;
  strlcpy(c.name, p["name"] | "", sizeof c.name);
  strlcpy(c.up, p["up"] | "", sizeof c.up);
  c.cpu = p["cpu"] | 0;
  c.ram = p["ram"] | 0;
  c.bat = p["bat"] | -1;
  c.chg = p["chg"] | false;
  c.disk = p["disk"] | 0;
}

static bool applyJson(const String& body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonVariantConst root = doc.as<JsonVariantConst>();

  uint32_t t = root["t"] | 0u;
  if (t > 1700000000 && time(nullptr) < 1700000000) {
    timeval tv{(time_t)t, 0};
    settimeofday(&tv, nullptr);
  }

  JsonArrayConst agents = root["agents"];
  if (!agents.isNull()) {
    model.n = 0;
    for (JsonVariantConst a : agents) {
      if (model.n == MAX_AGENTS) break;
      readAgent(a, model.agents[model.n++]);
    }
  }
  readWeather(root["wx"]);
  readHost(root["pc"]);
  model.updated = time(nullptr);
  return true;
}

static void handleUpdate() {
  if (strlen(PANEL_TOKEN) && server.header("X-Token") != PANEL_TOKEN) {
    server.send(403, "text/plain", "bad token");
    return;
  }
  if (applyJson(server.arg("plain"))) server.send(200, "application/json", "{\"ok\":true}");
  else server.send(400, "text/plain", "bad json");
}

static void handleRoot() {
  char b[200];
  snprintf(b, sizeof b, "{\"panel\":\"%s\",\"ip\":\"%s\",\"heap\":%u,\"agents\":%d,\"updated\":%u}",
           HOSTNAME, st.ip, (unsigned)ESP.getFreeHeap(), model.n, (unsigned)model.updated);
  server.send(200, "application/json", b);
}

static void handleScreen() {
  int n = server.arg("n").toInt();
  if (n >= 0 && n < SCR_COUNT) st.screen = n;
  server.send(200, "text/plain", "ok");
}

static void pullData() {
  const char* url = DATA_URL;
  if (!*url) return;
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure tls;
  bool https = !strncmp(url, "https", 5);
  if (https) tls.setInsecure();
  http.setTimeout(6000);
  if (!http.begin(https ? (WiFiClient&)tls : plain, url)) return;
  int code = http.GET();
  if (code != 200 || !applyJson(http.getString())) Serial.printf("pull failed: %d\n", code);
  http.end();
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
    strlcpy(st.ip, WiFi.localIP().toString().c_str(), sizeof st.ip);
    MDNS.begin(HOSTNAME);
    MDNS.addService("http", "tcp", 80);
    configTzTime(TZ_POSIX, "pool.ntp.org", "time.google.com", "ru.pool.ntp.org");
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.begin();
    Serial.printf("wifi ok: %s (%s.local)\n", st.ip, HOSTNAME);
  }
  server.handleClient();
  ArduinoOTA.handle();

  static uint32_t lastPull = 0;
  if (!lastPull || millis() - lastPull > PULL_EVERY_S * 1000UL) {
    lastPull = millis() | 1;
    pullData();
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

static void focusTick() {
  if (!st.focusRun || st.now < st.focusEnd) return;
  st.focusRun = false;
  st.focusLeft = 0;
  st.focusDone++;
  st.screen = SCR_FOCUS;
  lcd.setBrightness(BRIGHT_DAY);
  beep(3);
}

static bool themeFlip = false;
static uint32_t lastInput = 0;

static void step(int d) { st.screen = (st.screen + SCR_COUNT + d) % SCR_COUNT; }

static void onTap(int x) {
  if (x < 107) step(-1);
  else if (x > 213) step(1);
  else if (st.screen == SCR_FOCUS) focusToggle();
  else if (st.screen == SCR_HOME) themeFlip = !themeFlip;
  else step(1);
}

static void onHold() {
  if (st.screen == SCR_FOCUS) {
    st.focusRun = false;
    st.focusLeft = st.focusLen;
  } else {
    st.screen = SCR_HOME;
  }
}

static bool inputLoop() {
  static bool down = false, held = false, bootPrev = true;
  static uint32_t t0 = 0, lastSeen = 0;
  static int32_t sx = 0;
  int32_t x, y;
  uint32_t ms = millis();
  bool changed = false;

  if (lcd.getTouch(&x, &y)) {
    if (!down) {
      down = true;
      held = false;
      t0 = ms;
      sx = x;
    }
    lastSeen = ms;
    if (!held && ms - t0 > 800) {
      held = true;
      onHold();
      changed = true;
    }
  } else if (down && ms - lastSeen > 80) {
    down = false;
    if (!held) {
      onTap(sx);
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

static bool isNight() {
  int h = st.lt.tm_hour;
  if (!st.timeOk) return false;
  return NIGHT_FROM > NIGHT_TO ? (h >= NIGHT_FROM || h < NIGHT_TO) : (h >= NIGHT_FROM && h < NIGHT_TO);
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
  setenv("TZ", TZ_POSIX, 1);
  tzset();

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
  render();
}

void loop() {
  netLoop();
  bool dirty = inputLoop();

  static uint32_t lastDraw = 0;
  uint32_t ms = millis();
  if (dirty || ms - lastDraw >= 1000) {
    lastDraw = ms;
    time_t now = time(nullptr);
    st.now = now;
    st.timeOk = now > 1700000000;
    localtime_r(&now, &st.lt);

    static int yday = -1;
    if (st.timeOk && st.lt.tm_yday != yday) {
      yday = st.lt.tm_yday;
      st.focusDone = 0;
    }

    bool night = isNight();
    st.dark = (THEME_MODE == 1 || (THEME_MODE == 2 && night)) != themeFlip;
    lcd.setBrightness(night ? BRIGHT_NIGHT : BRIGHT_DAY);

    if (st.screen != SCR_HOME && !(st.screen == SCR_FOCUS && st.focusRun) && ms - lastInput > 120000)
      st.screen = SCR_HOME;

    focusTick();
    render();
  }
  delay(10);
}
