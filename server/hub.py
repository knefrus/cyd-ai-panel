#!/usr/bin/env python3
import argparse
import base64
import copy
import datetime as dt
import getpass
import hashlib
import hmac
import html
import http.cookies
import json
import os
import platform
import re
import secrets
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ics  # noqa: E402
import remna  # noqa: E402

try:
    import psutil
except ImportError:
    psutil = None

try:
    from PIL import Image, ImageSequence
except ImportError:
    Image = None

try:
    from zoneinfo import ZoneInfo
except ImportError:
    ZoneInfo = None

ROOT = Path(__file__).resolve().parent
HOME = Path.home()

CLAUDE_CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
CLAUDE_TOKEN_URLS = ["https://platform.claude.com/v1/oauth/token", "https://console.anthropic.com/v1/oauth/token"]
CODEX_CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann"

GRID_W, GRID_H, CELL_W, CELL_H, GAP = 6, 4, 45, 49, 6
WIDGET_TYPES = ["clock", "agent", "agents", "weather", "calendar", "text", "ping", "pet", "gif", "activity", "host", "focus", "nodes"]
GIF_BUDGET = 180_000


def log(*a):
    print(f"{dt.datetime.now():%H:%M:%S}", *a, flush=True)


def http_json(url, headers=None, data=None, timeout=15):
    req = urllib.request.Request(url, headers=headers or {}, data=data)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


def post_json(url, body, headers=None):
    h = {"Content-Type": "application/json", "Accept": "application/json", **(headers or {})}
    return http_json(url, h, json.dumps(body).encode())


def save_json(path, data):
    path = Path(path)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    os.replace(tmp, path)


def to_epoch(v):
    if v in (None, ""):
        return 0
    if isinstance(v, (int, float)):
        return int(v / 1000 if v > 1e12 else v)
    return int(dt.datetime.fromisoformat(str(v).replace("Z", "+00:00")).timestamp())


def jwt_exp(tok):
    try:
        part = tok.split(".")[1]
        return json.loads(base64.urlsafe_b64decode(part + "=" * (-len(part) % 4))).get("exp", 0)
    except Exception:
        return 0


def hexcolor(v):
    v = (v or "").strip().lstrip("#")
    return v.upper() if re.fullmatch(r"[0-9a-fA-F]{6}", v) else ""


def cell_px(w, h):
    return w * CELL_W + (w - 1) * GAP, h * CELL_H + (h - 1) * GAP


# ---------------- accounts ----------------

class Claude:
    def __init__(self, folder):
        self.file = Path(folder).expanduser() / ".credentials.json"

    def exists(self):
        return self.file.exists()

    def refresh(self):
        data = json.loads(self.file.read_text(encoding="utf-8"))
        c = data.get("claudeAiOauth") or {}
        if not c.get("refreshToken"):
            return None
        body = {"grant_type": "refresh_token", "refresh_token": c["refreshToken"], "client_id": CLAUDE_CLIENT_ID}
        for url in CLAUDE_TOKEN_URLS:
            try:
                r = post_json(url, body, {"User-Agent": "claude-code/2.0"})
                break
            except Exception as e:
                log("claude refresh", url, e)
        else:
            return None
        c["accessToken"] = r["access_token"]
        c["refreshToken"] = r.get("refresh_token") or c["refreshToken"]
        c["expiresAt"] = int((time.time() + int(r.get("expires_in", 3600))) * 1000)
        data["claudeAiOauth"] = c
        save_json(self.file, data)
        log("claude token refreshed", self.file)
        return c

    def get(self, tok):
        return http_json("https://api.anthropic.com/api/oauth/usage", {
            "Authorization": f"Bearer {tok}", "anthropic-beta": "oauth-2025-04-20",
            "User-Agent": "claude-code/2.0", "Accept": "application/json"})

    def fetch(self):
        c = json.loads(self.file.read_text(encoding="utf-8")).get("claudeAiOauth")
        if not c:
            return None
        plan = (c.get("subscriptionType") or "").lower()
        if c.get("expiresAt") and c["expiresAt"] / 1000 < time.time() + 600:
            c = self.refresh() or c
        try:
            d = self.get(c["accessToken"])
        except urllib.error.HTTPError as e:
            fresh = self.refresh() if e.code == 401 else None
            if not fresh:
                log("claude HTTP", e.code)
                return {"ok": False, "plan": plan}
            d = self.get(fresh["accessToken"])

        def win(key, length):
            w = d.get(key)
            if not w or w.get("utilization") is None:
                return None
            return [round(float(w["utilization"]), 1), to_epoch(w.get("resets_at")), length]

        return {"ok": True, "plan": plan, "s": win("five_hour", 18000), "w": win("seven_day", 604800)}


class Codex:
    def __init__(self, folder):
        self.file = Path(folder).expanduser() / "auth.json"

    def exists(self):
        return self.file.exists()

    def refresh(self):
        data = json.loads(self.file.read_text(encoding="utf-8"))
        tok = data.get("tokens") or {}
        if not tok.get("refresh_token"):
            return None
        try:
            r = post_json("https://auth.openai.com/oauth/token", {
                "client_id": CODEX_CLIENT_ID, "grant_type": "refresh_token",
                "refresh_token": tok["refresh_token"], "scope": "openid profile email"})
        except Exception as e:
            log("codex refresh", e)
            return None
        for k in ("access_token", "refresh_token", "id_token"):
            if r.get(k):
                tok[k] = r[k]
        data["tokens"] = tok
        data["last_refresh"] = dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z")
        save_json(self.file, data)
        log("codex token refreshed", self.file)
        return tok

    def get(self, t):
        return http_json("https://chatgpt.com/backend-api/wham/usage", {
            "Authorization": f"Bearer {t['access_token']}", "ChatGPT-Account-Id": t.get("account_id") or "",
            "User-Agent": "codex_cli_rs/0.50.0", "Accept": "application/json"})

    def fetch(self):
        tok = json.loads(self.file.read_text(encoding="utf-8")).get("tokens") or {}
        if not tok.get("access_token"):
            return None
        exp = jwt_exp(tok["access_token"])
        if exp and exp < time.time() + 3600:
            tok = self.refresh() or tok
        try:
            d = self.get(tok)
        except urllib.error.HTTPError as e:
            fresh = self.refresh() if e.code == 401 else None
            if not fresh:
                log("codex HTTP", e.code)
                return {"ok": False, "plan": ""}
            d = self.get(fresh)
        now = time.time()

        def win(w):
            if not w or w.get("used_percent") is None:
                return None
            length = int(w.get("limit_window_seconds") or (w.get("window_minutes") or 0) * 60)
            reset = to_epoch(w.get("reset_at") or w.get("resets_at"))
            if not reset and w.get("reset_after_seconds") is not None:
                reset = int(now + w["reset_after_seconds"])
            return [round(float(w["used_percent"]), 1), reset, length]

        rl = d.get("rate_limit") or {}
        wins = [w for w in (win(rl.get("primary_window")), win(rl.get("secondary_window"))) if w]
        short = next((w for w in wins if w[2] and w[2] <= 86400), None)
        week = next((w for w in wins if w[2] and w[2] > 86400), None)
        return {"ok": True, "plan": (d.get("plan_type") or "").lower(), "s": short, "w": week}


# ---------------- config ----------------

def wid():
    return secrets.token_hex(4)


def W(type, x, y, w, h, **kw):
    return {"id": wid(), "type": type, "x": x, "y": y, "w": w, "h": h, **kw}


def default_config():
    return {
        "settings": {"theme": 2, "night_from": 22, "night_to": 8, "bright_day": 255, "bright_night": 40,
                     "home_after": 120, "tz": "Europe/Moscow", "city": "Moscow", "lat": 55.7558, "lon": 37.6173},
        "accounts": [{"name": "Claude", "kind": "claude", "dir": "~/.claude", "color": ""},
                     {"name": "Codex", "kind": "codex", "dir": "~/.codex", "color": ""}],
        "calendars": [],
        "ping": [],
        "gifs": {},
        "screens": [
            {"name": "Главный", "widgets": [
                W("clock", 0, 0, 3, 1, card=False), W("weather", 3, 0, 3, 1, card=False, align=2),
                W("agent", 0, 1, 3, 3, acc="Claude"), W("agent", 3, 1, 3, 3, acc="Codex")]},
            {"name": "День", "widgets": [
                W("clock", 0, 0, 3, 1, card=False), W("weather", 3, 0, 3, 1, card=False, align=2),
                W("calendar", 0, 1, 4, 3), W("pet", 4, 1, 2, 3, title="Mochi")]},
            {"name": "Активность", "widgets": [W("activity", 0, 0, 6, 4, card=False)]},
            {"name": "Фокус", "widgets": [W("focus", 0, 0, 6, 4, card=False)]},
        ],
    }


def clamp_widget(w):
    w["w"] = max(1, min(GRID_W, int(w.get("w", 1))))
    w["h"] = max(1, min(GRID_H, int(w.get("h", 1))))
    w["x"] = max(0, min(GRID_W - w["w"], int(w.get("x", 0))))
    w["y"] = max(0, min(GRID_H - w["h"], int(w.get("y", 0))))
    return w


def clean_config(c):
    base = default_config()
    out = {"settings": {**base["settings"], **(c.get("settings") or {})}}
    out["accounts"] = [a for a in (c.get("accounts") or base["accounts"]) if a.get("name") and a.get("kind") in ("claude", "codex")][:6]
    out["calendars"] = [x for x in (c.get("calendars") or []) if x.get("url")][:8]
    out["ping"] = [x for x in (c.get("ping") or []) if x.get("host")][:12]
    out["gifs"] = c.get("gifs") or {}
    screens = []
    for s in (c.get("screens") or base["screens"])[:6]:
        ws = []
        for w in (s.get("widgets") or [])[:12]:
            if w.get("type") not in WIDGET_TYPES:
                continue
            w.setdefault("id", wid())
            ws.append(clamp_widget(w))
        screens.append({"name": str(s.get("name") or "Экран")[:24], "widgets": ws})
    out["screens"] = screens or base["screens"]
    return out


def posix_tz(name):
    for base in ("/usr/share/zoneinfo", "/usr/lib/zoneinfo"):
        p = Path(base) / name
        if p.is_file():
            data = p.read_bytes()
            tail = data.rstrip(b"\n").rsplit(b"\n", 1)[-1]
            if data[:4] == b"TZif" and tail and len(tail) < 48:
                return tail.decode()
    return "MSK-3"


# ---------------- hub ----------------

class Hub:
    def __init__(self, data_dir, key, render_bin):
        self.data = Path(data_dir)
        self.data.mkdir(parents=True, exist_ok=True)
        (self.data / "gifs").mkdir(exist_ok=True)
        (self.data / "assets").mkdir(exist_ok=True)
        self.key = key
        self.render_bin = Path(render_bin)
        self.lock = threading.RLock()
        self.cfg_path = self.data / "config.json"
        self.config = clean_config(json.loads(self.cfg_path.read_text(encoding="utf-8"))) if self.cfg_path.exists() else default_config()
        if not self.cfg_path.exists():
            legacy = Path("/opt/ai-panel/accounts.json")
            if legacy.exists():
                try:
                    self.config["accounts"] = json.loads(legacy.read_text())
                except Exception:
                    pass
            save_json(self.cfg_path, self.config)
        self.hist_path = self.data / "history.json"
        old_hist = HOME / ".ai-panel" / "history.json"
        if not self.hist_path.exists() and old_hist.exists():
            self.hist_path.write_text(old_hist.read_text())
        self.agents = []
        self.last_ok = {}
        self.weather = None
        self.wx_key = None
        self.events = []
        self.cal_status = {}
        self.pings = {}
        self.ping_hist = {}
        self.body = b"{}"
        self.version = "0"
        self.timers = {}
        self.remna = remna.Remna()
        self.rn = []
        self.rn_ok = 0
        self.rn_err = ""
        self.rn_seen = {}

    # ----- collectors -----
    def due(self, name, every):
        now = time.time()
        if now - self.timers.get(name, 0) >= every:
            self.timers[name] = now
            return True
        return False

    def collect_agents(self):
        out = []
        for acc in self.config["accounts"]:
            src = (Claude if acc["kind"] == "claude" else Codex)(acc.get("dir") or ("~/.claude" if acc["kind"] == "claude" else "~/.codex"))
            if not src.exists():
                continue
            try:
                v = src.fetch()
            except Exception as e:
                log(acc["name"], e)
                v = {"ok": False, "plan": ""}
            if v is None:
                continue
            if v["ok"]:
                self.last_ok[acc["name"]] = v
            elif acc["name"] in self.last_ok:
                v = {**self.last_ok[acc["name"]], "ok": False}
            item = {"n": acc["name"], "k": acc["kind"], **v}
            if hexcolor(acc.get("color")):
                item["c"] = hexcolor(acc["color"])
            out.append(item)
        self.update_history(out)
        self.agents = out

    def update_history(self, agents):
        try:
            h = json.loads(self.hist_path.read_text())
        except Exception:
            h = []
        now = int(time.time())
        h.append([now, {a["n"]: a["w"][0] for a in agents if a.get("ok") and a.get("w")}])
        h = [r for r in h if len(r) == 2 and isinstance(r[1], dict) and r[0] > now - 26 * 3600]
        self.hist_path.write_text(json.dumps(h))
        hour = now - now % 3600
        for a in agents:
            bins = [0] * 24
            prev = None
            for t, vals in h:
                v = vals.get(a["n"])
                if v is None:
                    continue
                if prev is not None:
                    delta = v - prev if v >= prev else (v if prev - v > 10 else 0)
                    i = 23 - (hour - (t - t % 3600)) // 3600
                    if 0 <= i < 24 and delta > 0:
                        bins[i] += round(delta * 10)
                prev = v
            a["h"] = bins

    def collect_weather(self):
        s = self.config["settings"]
        url = ("https://api.open-meteo.com/v1/forecast"
               f"?latitude={s['lat']}&longitude={s['lon']}&timezone=auto&forecast_days=2"
               "&current=temperature_2m,weather_code,is_day&hourly=temperature_2m,weather_code"
               "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset")
        try:
            d = http_json(url)
        except Exception as e:
            log("weather", e)
            return
        cur, hourly, daily = d["current"], d["hourly"], d["daily"]
        key = cur["time"][:13] + ":00"
        i0 = hourly["time"].index(key) if key in hourly["time"] else 0
        self.weather = {
            "t": round(cur["temperature_2m"], 1), "c": cur["weather_code"], "d": cur["is_day"],
            "hi": round(daily["temperature_2m_max"][0]), "lo": round(daily["temperature_2m_min"][0]),
            "sr": daily["sunrise"][0][11:16], "ss": daily["sunset"][0][11:16], "city": s.get("city", ""),
            "h": [[int(hourly["time"][i][11:13]), round(hourly["temperature_2m"][i]), hourly["weather_code"][i]]
                  for i in range(i0, i0 + 6)],
        }

    def tz(self):
        if ZoneInfo:
            try:
                return ZoneInfo(self.config["settings"].get("tz") or "Europe/Moscow")
            except Exception:
                pass
        return dt.timezone(dt.timedelta(hours=3))

    def fetch_calendar(self, cal, tz, start, end):
        url = cal["url"].strip()
        if url.startswith("webcal://"):
            url = "https://" + url[9:]
        req = urllib.request.Request(url, headers={"User-Agent": "cyd-ai-panel"})
        with urllib.request.urlopen(req, timeout=20) as r:
            text = r.read().decode("utf-8", "replace")
        return ics.events(text, tz, start, end)

    def collect_calendars(self):
        tz = self.tz()
        now = dt.datetime.now(tz)
        start = now.replace(hour=0, minute=0, second=0, microsecond=0)
        end = start + dt.timedelta(days=3)
        out = []
        for cal in self.config["calendars"]:
            if cal.get("enabled") is False:
                continue
            try:
                evs = self.fetch_calendar(cal, tz, start, end)
                self.cal_status[cal["url"]] = f"ok, {len(evs)} events"
            except Exception as e:
                self.cal_status[cal["url"]] = f"error: {e}"
                log("calendar", cal.get("name"), e)
                continue
            color = hexcolor(cal.get("color")) or "4F7FE0"
            out += [[a, b, t, color, 1 if ad else 0] for a, b, t, ad in evs]
        out.sort(key=lambda e: (e[0], e[1]))
        self.events = out

    def ping_one(self, t):
        host, kind = t["host"].strip(), t.get("kind") or "icmp"
        start = time.perf_counter()
        try:
            if kind == "http":
                url = host if host.startswith("http") else "https://" + host
                req = urllib.request.Request(url, headers={"User-Agent": "cyd-ai-panel"}, method="GET")
                try:
                    with urllib.request.urlopen(req, timeout=6) as r:
                        ok = r.status < 500
                except urllib.error.HTTPError as e:
                    ok = e.code < 500
                return int((time.perf_counter() - start) * 1000) if ok else -1
            if kind == "tcp":
                port = int(t.get("port") or 443)
                with socket.create_connection((host, port), timeout=4):
                    pass
                return int((time.perf_counter() - start) * 1000)
            r = subprocess.run(["ping", "-c", "1", "-W", "2", host], capture_output=True, text=True, timeout=5)
            m = re.search(r"time[=<]([\d.]+)", r.stdout)
            return round(float(m.group(1))) if r.returncode == 0 and m else -1
        except Exception:
            return -1

    def collect_pings(self):
        targets = self.config["ping"]
        results = {}

        def run(t):
            results[t["name"] or t["host"]] = self.ping_one(t)

        threads = [threading.Thread(target=run, args=(t,)) for t in targets]
        for th in threads:
            th.start()
        for th in threads:
            th.join(10)
        for name, ms in results.items():
            h = self.ping_hist.setdefault(name, [])
            h.append(255 if ms < 0 else max(1, min(254, ms // 4)))
            del h[:-24]
        self.pings = results

    def collect_remna(self):
        try:
            nodes = self.remna.nodes()
        except Exception as e:
            self.rn_err = remna.short_error(e)
            log("remnawave", self.rn_err)
            return
        now = time.time()
        for n in nodes:
            if n["up"] and (n["cpu"] is not None or n["ram"] is not None or not (self.remna.f_cpu or self.remna.f_ram)):
                self.rn_seen[n["name"]] = now
            elif n["name"] not in self.rn_seen:
                self.rn_seen[n["name"]] = now if n["up"] else 0
        self.rn = nodes
        self.rn_ok = now
        self.rn_err = ""

    def nodes_payload(self):
        if not self.remna.configured:
            return None
        now = time.time()
        stale = not self.rn_ok or now - self.rn_ok > 180
        rows = []
        for n in sorted(self.rn, key=lambda n: (-n["up"], -n["users"], n["name"])):
            if stale or now - self.rn_seen.get(n["name"], 0) > 180 and n["up"]:
                st = 2
            else:
                st = 1 if n["up"] else 0
            rows.append([n["name"][:18], st, n["users"], -1 if n["cpu"] is None else n["cpu"], -1 if n["ram"] is None else n["ram"]])
        return {"st": 1 if stale else 0, "list": rows[:16]}

    def host_stats(self):
        name = platform.node().split(".")[0][:23]
        if not psutil:
            return {"name": name}
        up = int(time.time() - psutil.boot_time())
        bat = psutil.sensors_battery()
        return {"name": name, "cpu": round(psutil.cpu_percent()), "ram": round(psutil.virtual_memory().percent),
                "disk": round(psutil.disk_usage("/").percent), "up": f"{up // 86400}d {up % 86400 // 3600}h",
                "bat": round(bat.percent) if bat else -1, "chg": bool(bat and bat.power_plugged)}

    # ----- gifs -----
    def asset_for(self, gif_id, w, h, fit):
        if Image is None:
            return ""
        src = self.data / "gifs" / f"{gif_id}.gif"
        if not src.exists():
            return ""
        aid = hashlib.sha1(f"{gif_id}-{w}-{h}-{fit}".encode()).hexdigest()[:12]
        out = self.data / "assets" / f"{aid}.bin"
        if not out.exists():
            try:
                make_asset(src, out, w, h, fit)
            except Exception as e:
                log("gif", gif_id, e)
                return ""
        return aid

    def cleanup_assets(self, used):
        for p in (self.data / "assets").glob("*.bin"):
            if p.stem not in used:
                p.unlink(missing_ok=True)

    # ----- payload -----
    def build(self, config=None, live=True):
        c = config or self.config
        s = c["settings"]
        names = [a["n"] for a in self.agents]
        screens = []
        used = set()
        for sc in c["screens"]:
            out = []
            for w in sc["widgets"]:
                pw, ph = cell_px(w["w"], w["h"])
                item = {"t": w["type"], "x": w["x"], "y": w["y"], "w": w["w"], "h": w["h"]}
                if "card" in w:
                    item["card"] = bool(w["card"])
                for k in ("align", "size"):
                    if w.get(k):
                        item[k] = int(w[k])
                if hexcolor(w.get("color")):
                    item["color"] = hexcolor(w["color"])
                if w.get("title"):
                    item["title"] = str(w["title"])[:30]
                if w["type"] == "text":
                    item["text"] = str(w.get("text", ""))[:600]
                if w["type"] == "agent":
                    acc = w.get("acc")
                    item["i"] = names.index(acc) if acc in names else (int(w.get("i", 0)) if acc is None else 99)
                if w["type"] == "gif" and w.get("gif"):
                    aid = self.asset_for(w["gif"], pw, ph, w.get("fit") or "contain")
                    if aid:
                        item["a"] = aid
                        used.add(aid)
                out.append(item)
            screens.append(out)
        pings = []
        for t in c["ping"]:
            name = t["name"] or t["host"]
            ms = self.pings.get(name)
            pings.append([name[:22], -1 if ms is None else ms, self.ping_hist.get(name, [])[-24:][::1]])
            pings[-1][2] = [0] * (24 - len(pings[-1][2])) + pings[-1][2]
        now = int(time.time())
        payload = {
            "set": {"theme": int(s.get("theme", 2)), "nf": int(s.get("night_from", 22)), "nt": int(s.get("night_to", 8)),
                    "bd": int(s.get("bright_day", 255)), "bn": int(s.get("bright_night", 40)),
                    "home": int(s.get("home_after", 120)), "tz": posix_tz(s.get("tz") or "Europe/Moscow")},
            "screens": screens,
            "agents": self.agents,
            "wx": self.weather,
            "pc": self.host_stats() if live else {},
            "cal": [e for e in self.events if e[1] > now - 60][:16],
            "ping": pings,
            "nodes": self.nodes_payload(),
        }
        return payload, used

    def publish(self):
        with self.lock:
            payload, used = self.build()
            stable = json.dumps({k: v for k, v in payload.items() if k != "pc"}, ensure_ascii=False, sort_keys=True)
            version = hashlib.sha1(stable.encode()).hexdigest()[:10]
            payload["v"] = version
            payload["t"] = int(time.time())
            self.body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode()
            self.version = version
            self.cleanup_assets(used)

    def loop(self):
        if psutil:
            psutil.cpu_percent()
        while True:
            changed = False
            try:
                if self.due("agents", 60):
                    self.collect_agents()
                    changed = True
                s = self.config["settings"]
                wk = (s.get("lat"), s.get("lon"))
                if self.due("weather", 900) or wk != self.wx_key:
                    self.wx_key = wk
                    self.collect_weather()
                    changed = True
                if self.due("cal", 600):
                    self.collect_calendars()
                    changed = True
                if self.remna.configured and self.due("remna", 60):
                    self.collect_remna()
                    changed = True
                if self.config["ping"] and self.due("ping", 60):
                    self.collect_pings()
                    changed = True
                if changed or self.due("publish", 30):
                    self.publish()
            except Exception as e:
                log("loop", e)
            time.sleep(2)

    def save_config(self, new):
        with self.lock:
            old = self.config
            self.config = clean_config(new)
            save_json(self.cfg_path, self.config)
            if [c["url"] for c in old["calendars"]] != [c["url"] for c in self.config["calendars"]] or old["settings"].get("tz") != self.config["settings"].get("tz"):
                self.timers["cal"] = 0
            if [p["host"] for p in old["ping"]] != [p["host"] for p in self.config["ping"]]:
                self.timers["ping"] = 0
            if [a["name"] for a in old["accounts"]] != [a["name"] for a in self.config["accounts"]]:
                self.timers["agents"] = 0
            self.publish()

    def preview(self, config, screen, dark):
        if not self.render_bin.exists():
            raise RuntimeError("renderer is not built")
        payload, _ = self.build(clean_config(config))
        payload["t"] = int(time.time())
        with tempfile.TemporaryDirectory() as tmp:
            j = Path(tmp) / "p.json"
            out = Path(tmp) / "p.bmp"
            j.write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")
            subprocess.run([str(self.render_bin), str(j), str(out), "--screen", str(screen), "--dark", "1" if dark else "0",
                            "--assets", str(self.data / "assets"), "--ms", str(int(time.time() * 1000) % 100000)],
                           check=True, timeout=10, capture_output=True)
            return out.read_bytes()

    def state(self):
        tz = self.tz()
        return {
            "agents": [{"name": a["n"], "ok": a["ok"], "plan": a.get("plan"), "s": (a.get("s") or [None])[0],
                        "w": (a.get("w") or [None])[0]} for a in self.agents],
            "accounts_found": [{"name": a["name"], "found": (Claude if a["kind"] == "claude" else Codex)(a.get("dir") or "~").exists()}
                               for a in self.config["accounts"]],
            "events": [{"start": dt.datetime.fromtimestamp(e[0], tz).strftime("%a %d %H:%M"), "title": e[2]} for e in self.events[:10]],
            "calendars": self.cal_status,
            "pings": self.pings,
            "renderer": self.render_bin.exists(),
            "gif_support": Image is not None,
            "version": self.version,
            "remna": {"configured": self.remna.configured, "nodes": len(self.rn), "error": self.rn_err,
                      "ago": int(time.time() - self.rn_ok) if self.rn_ok else None,
                      "fields": {"cpu": bool(self.remna.f_cpu), "ram": bool(self.remna.f_ram)}},
        }


def make_asset(src, out, w, h, fit):
    im = Image.open(src)
    frames, delays = [], []
    for f in ImageSequence.Iterator(im):
        rgba = f.convert("RGBA")
        if fit == "cover":
            scale = max(w / rgba.width, h / rgba.height)
        else:
            scale = min(w / rgba.width, h / rgba.height)
        nw, nh = max(1, round(rgba.width * scale)), max(1, round(rgba.height * scale))
        rgba = rgba.resize((nw, nh), Image.LANCZOS)
        if fit == "cover":
            l, t = (nw - w) // 2, (nh - h) // 2
            rgba = rgba.crop((l, t, l + w, t + h))
        frames.append(rgba)
        delays.append(max(20, int(f.info.get("duration", 100) or 100)))
    fw, fh = frames[0].size
    limit = max(1, GIF_BUDGET // (fw * fh))
    if len(frames) > limit:
        step = len(frames) / limit
        idx = [int(i * step) for i in range(limit)]
        nd = []
        for k, i in enumerate(idx):
            j = idx[k + 1] if k + 1 < len(idx) else len(frames)
            nd.append(sum(delays[i:j]))
        frames = [frames[i] for i in idx]
        delays = nd
    sheet = Image.new("RGBA", (fw, fh * len(frames)))
    for i, f in enumerate(frames):
        sheet.paste(f, (0, i * fh))
    alpha = sheet.getchannel("A")
    rgb = Image.new("RGB", sheet.size, (0, 0, 0))
    rgb.paste(sheet, mask=alpha)
    q = rgb.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    pal = q.getpalette()[:765]
    pal += [0] * (765 - len(pal))
    idx = bytearray(q.tobytes())
    a = alpha.tobytes()
    for i in range(len(idx)):
        if a[i] < 128:
            idx[i] = 255
    head = b"CYDG" + fw.to_bytes(2, "little") + fh.to_bytes(2, "little") + len(frames).to_bytes(2, "little")
    head += b"".join(min(d, 65535).to_bytes(2, "little") for d in delays)
    p565 = bytearray()
    for i in range(256):
        r, g, b = (pal[i * 3:i * 3 + 3] if i < 255 else [0, 0, 0])
        v = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3
        p565 += v.to_bytes(2, "little")
    out.write_bytes(head + bytes(p565) + bytes(idx))


# ---------------- auth ----------------

def auth_file(data):
    return Path(data) / "auth.json"


def set_password(data, pw):
    salt = secrets.token_bytes(16)
    h = hashlib.pbkdf2_hmac("sha256", pw.encode(), salt, 200_000)
    save_json(auth_file(data), {"salt": salt.hex(), "hash": h.hex()})
    os.chmod(auth_file(data), 0o600)


def load_auth(data):
    try:
        a = json.loads(auth_file(data).read_text())
        return bytes.fromhex(a["salt"]), bytes.fromhex(a["hash"])
    except Exception:
        return None


def check_password(data, pw):
    a = load_auth(data)
    if not a:
        return False
    return hmac.compare_digest(hashlib.pbkdf2_hmac("sha256", pw.encode(), a[0], 200_000), a[1])


def session_token(hub):
    a = load_auth(hub.data)
    if not a:
        return None
    return hmac.new(hub.key.encode(), a[1], hashlib.sha256).hexdigest()


LOGIN_PAGE = """<!doctype html><html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>Desk panel</title>
<style>
:root{--bg:#F1F1EF;--card:#fff;--line:#E3E3E0;--ink:#151515;--muted:#8A8A86;--warn:#E5533D}
@media (prefers-color-scheme:dark){:root{--bg:#0D0D0D;--card:#171717;--line:#262626;--ink:#ECECEC;--muted:#7A7A7A}}
*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;background:var(--bg);color:var(--ink);font:14px/1.45 Inter,system-ui,sans-serif}
form{width:min(340px,calc(100vw - 32px));background:var(--card);border:1px solid var(--line);border-radius:16px;padding:22px}
h1{font-size:18px;margin:0 0 4px}p{color:var(--muted);margin:0 0 16px;font-size:13px}
input{width:100%;font:inherit;color:inherit;border:1px solid var(--line);border-radius:10px;padding:9px 11px;background:var(--bg);outline:none;margin-bottom:12px}
input:focus{border-color:var(--ink)}button{width:100%;font:inherit;font-weight:600;border:0;border-radius:10px;padding:9px;background:var(--ink);color:var(--bg);cursor:pointer}
.err{color:var(--warn);margin:-4px 0 12px;font-size:13px}code{font-size:12px}
</style></head><body>__BODY__</body></html>"""

LOGIN_FORM = """<form method="post" action="/login"><h1>Desk panel</h1><p>Конструктор панели</p>
<input type="password" name="password" placeholder="Пароль" autofocus autocomplete="current-password">__ERR__
<button>Войти</button></form>"""

NO_PASSWORD = """<form onsubmit="return false"><h1>Пароль не задан</h1>
<p>Задай его на сервере командой:</p><code>python3 /opt/ai-panel/server/hub.py --set-password</code></form>"""


# ---------------- http ----------------

def make_handler(hub):
    web = ROOT / "web" / "index.html"

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *a):
            pass

        def has_key(self, q):
            return bool(hub.key) and hmac.compare_digest((q.get("key") or [""])[0], hub.key)

        def logged_in(self):
            tok = session_token(hub)
            ck = http.cookies.SimpleCookie(self.headers.get("Cookie", ""))
            return bool(tok) and "s" in ck and hmac.compare_digest(ck["s"].value, tok)

        def login_page(self, error=""):
            err = f'<div class="err">{html.escape(error)}</div>' if error else ""
            inner = LOGIN_FORM.replace("__ERR__", err) if load_auth(hub.data) else NO_PASSWORD
            return self.send(200, LOGIN_PAGE.replace("__BODY__", inner), "text/html; charset=utf-8")

        def send(self, code, body=b"", ctype="application/json", extra=None):
            if isinstance(body, str):
                body = body.encode()
            if isinstance(body, (dict, list)):
                body = json.dumps(body, ensure_ascii=False).encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(body)

        def body(self, limit=8_000_000):
            n = int(self.headers.get("Content-Length") or 0)
            if n > limit:
                raise ValueError("too big")
            return self.rfile.read(n)

        def do_GET(self):
            u = urllib.parse.urlparse(self.path)
            q = urllib.parse.parse_qs(u.query)
            p = u.path
            if p in ("/panel.json", "/v") or (p.startswith("/a/") and p.endswith(".bin")):
                if not self.has_key(q):
                    return self.send(404, b"not found", "text/plain")
                if p == "/panel.json":
                    return self.send(200, hub.body)
                if p == "/v":
                    return self.send(200, hub.version, "text/plain")
                f = hub.data / "assets" / Path(p).name
                return self.send(200, f.read_bytes(), "application/octet-stream") if f.exists() else self.send(404, b"", "text/plain")
            if p == "/logout":
                return self.send(302, b"", "text/plain", {"Location": "/", "Set-Cookie": "s=; Path=/; Max-Age=0"})
            if not self.logged_in():
                if p in ("/", "/login"):
                    return self.login_page()
                return self.send(401, {"error": "login required"})
            if p in ("/", "/login"):
                return self.send(200, web.read_bytes(), "text/html; charset=utf-8")
            if p == "/api/config":
                return self.send(200, hub.config)
            if p == "/api/state":
                return self.send(200, hub.state())
            if p.startswith("/gif/"):
                f = hub.data / "gifs" / Path(p).name
                return self.send(200, f.read_bytes(), "image/gif") if f.exists() else self.send(404, b"", "text/plain")
            if p == "/api/geocode":
                name = (q.get("q") or [""])[0]
                try:
                    d = http_json("https://geocoding-api.open-meteo.com/v1/search?count=5&language=ru&name=" + urllib.parse.quote(name))
                    res = [{"name": r["name"], "country": r.get("country", ""), "lat": r["latitude"], "lon": r["longitude"],
                            "tz": r.get("timezone", "")} for r in d.get("results", [])]
                except Exception as e:
                    return self.send(502, {"error": str(e)})
                return self.send(200, res)
            return self.send(404, b"not found", "text/plain")

        def do_POST(self):
            u = urllib.parse.urlparse(self.path)
            q = urllib.parse.parse_qs(u.query)
            p = u.path
            if p == "/login":
                form = urllib.parse.parse_qs(self.body(10_000).decode("utf-8", "replace"))
                if check_password(hub.data, (form.get("password") or [""])[0]):
                    cookie = f"s={session_token(hub)}; Path=/; HttpOnly; SameSite=Lax; Max-Age=31536000"
                    return self.send(302, b"", "text/plain", {"Location": "/", "Set-Cookie": cookie})
                time.sleep(1.5)
                return self.login_page("Неверный пароль")
            if not self.logged_in():
                return self.send(401, {"error": "login required"})
            try:
                if p == "/api/config":
                    hub.save_config(json.loads(self.body()))
                    return self.send(200, {"ok": True, "version": hub.version})
                if p == "/api/preview":
                    d = json.loads(self.body())
                    img = hub.preview(d["config"], int(d.get("screen", 0)), bool(d.get("dark")))
                    return self.send(200, img, "image/bmp")
                if p == "/api/gif":
                    data = self.body()
                    if Image is None:
                        return self.send(500, {"error": "Pillow is not installed on the server"})
                    gid = hashlib.sha1(data).hexdigest()[:10]
                    (hub.data / "gifs" / f"{gid}.gif").write_bytes(data)
                    Image.open(hub.data / "gifs" / f"{gid}.gif").verify()
                    name = (q.get("name") or ["gif"])[0][:40]
                    return self.send(200, {"id": gid, "name": name})
                if p == "/api/caltest":
                    d = json.loads(self.body())
                    tz = hub.tz()
                    now = dt.datetime.now(tz)
                    evs = hub.fetch_calendar(d, tz, now - dt.timedelta(hours=12), now + dt.timedelta(days=7))
                    return self.send(200, {"ok": True, "count": len(evs),
                                           "next": [{"start": dt.datetime.fromtimestamp(e[0], tz).strftime("%a %d %H:%M"), "title": e[2]} for e in evs[:5]]})
                if p == "/api/pingtest":
                    d = json.loads(self.body())
                    return self.send(200, {"ms": hub.ping_one(d)})
            except Exception as e:
                return self.send(400, {"error": str(e)})
            return self.send(404, b"not found", "text/plain")

    return Handler


def main():
    ap = argparse.ArgumentParser(description="Hub for the CYD desk panel")
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--key", default="")
    ap.add_argument("--data", default=str(ROOT.parent / "data"))
    ap.add_argument("--render", default=str(ROOT.parent / "render"))
    ap.add_argument("--dry", action="store_true", help="collect once, print the payload and exit")
    ap.add_argument("--set-password", action="store_true", help="set the editor password and exit")
    ap.add_argument("--remna-dump", action="store_true", help="print what the Remnawave API returns and exit")
    ap.add_argument("--env", default=str(ROOT.parent / ".env"))
    a = ap.parse_args()
    remna.load_env(a.env)
    if a.remna_dump:
        remna.dump()
        return
    if a.set_password:
        Path(a.data).mkdir(parents=True, exist_ok=True)
        while True:
            pw = getpass.getpass("Новый пароль для конструктора: ")
            if len(pw) < 6:
                print("Минимум 6 символов")
                continue
            if getpass.getpass("Ещё раз: ") == pw:
                break
            print("Не совпало, ещё раз")
        set_password(a.data, pw)
        print("Готово. Все старые входы сброшены, перезапуск не нужен.")
        return
    if not a.key:
        ap.error("--key is required")
    hub = Hub(a.data, a.key, a.render)
    if a.dry:
        hub.collect_agents()
        hub.collect_weather()
        hub.collect_calendars()
        hub.collect_pings()
        hub.publish()
        print(json.dumps(json.loads(hub.body), ensure_ascii=False, indent=1))
        return
    threading.Thread(target=hub.loop, daemon=True).start()
    log(f"hub on :{a.port}")
    ThreadingHTTPServer(("0.0.0.0", a.port), make_handler(hub)).serve_forever()


if __name__ == "__main__":
    main()
