#!/usr/bin/env python3
import argparse
import base64
import datetime as dt
import json
import os
import platform
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

try:
    import psutil
except ImportError:
    psutil = None

HOME = Path.home()
STATE = HOME / ".ai-panel"
STATE.mkdir(exist_ok=True)
HIST = STATE / "history.json"
LOG = STATE / "feeder.log"

CLAUDE_CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
CLAUDE_TOKEN_URLS = ["https://platform.claude.com/v1/oauth/token",
                     "https://console.anthropic.com/v1/oauth/token"]
CODEX_CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann"


def log(*a):
    line = f"{dt.datetime.now():%H:%M:%S} " + " ".join(map(str, a))
    if sys.stdout and sys.stdout.isatty():
        print(line)
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def http_json(url, headers=None, data=None, timeout=15):
    req = urllib.request.Request(url, headers=headers or {}, data=data)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


def post_json(url, body, headers=None):
    h = {"Content-Type": "application/json", "Accept": "application/json", **(headers or {})}
    return http_json(url, h, json.dumps(body).encode())


def save_json(path, data):
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(data, indent=2), encoding="utf-8")
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


class Claude:
    def __init__(self, folder):
        self.file = Path(folder).expanduser() / ".credentials.json"

    def exists(self):
        return self.file.exists() or (platform.system() == "Darwin" and self.file.parent == HOME / ".claude")

    def creds(self):
        if self.file.exists():
            return json.loads(self.file.read_text(encoding="utf-8")).get("claudeAiOauth")
        r = subprocess.run(["security", "find-generic-password", "-s", "Claude Code-credentials", "-w"],
                           capture_output=True, text=True)
        return json.loads(r.stdout).get("claudeAiOauth") if r.returncode == 0 else None

    def refresh(self):
        if not self.file.exists():
            return None
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
            "Authorization": f"Bearer {tok}",
            "anthropic-beta": "oauth-2025-04-20",
            "User-Agent": "claude-code/2.0",
            "Accept": "application/json",
        })

    def fetch(self):
        c = self.creds()
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
            "Authorization": f"Bearer {t['access_token']}",
            "ChatGPT-Account-Id": t.get("account_id") or "",
            "User-Agent": "codex_cli_rs/0.50.0",
            "Accept": "application/json",
        })

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


def load_accounts(path):
    if path:
        items = json.loads(Path(path).read_text(encoding="utf-8"))
    else:
        items = [
            {"name": "Claude", "kind": "claude", "dir": os.environ.get("CLAUDE_CONFIG_DIR", "~/.claude")},
            {"name": "Codex", "kind": "codex", "dir": os.environ.get("CODEX_HOME", "~/.codex")},
        ]
    out = []
    for it in items:
        src = (Claude if it["kind"] == "claude" else Codex)(it["dir"])
        if src.exists():
            out.append({**it, "src": src})
        else:
            log("skip", it["name"], "- no login in", it["dir"])
    return out


def update_history(agents):
    try:
        h = json.loads(HIST.read_text())
    except Exception:
        h = []
    now = int(time.time())
    h.append([now, {a["n"]: a["w"][0] for a in agents if a.get("ok") and a.get("w")}])
    h = [r for r in h if len(r) == 2 and isinstance(r[1], dict) and r[0] > now - 26 * 3600]
    HIST.write_text(json.dumps(h))

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


_wx = {"t": 0, "data": None}


def fetch_weather(lat, lon, city):
    if _wx["data"] and time.time() - _wx["t"] < 900:
        return _wx["data"]
    url = ("https://api.open-meteo.com/v1/forecast"
           f"?latitude={lat}&longitude={lon}&timezone=auto&forecast_days=2"
           "&current=temperature_2m,weather_code,is_day"
           "&hourly=temperature_2m,weather_code"
           "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset")
    try:
        d = http_json(url)
    except Exception as e:
        log("weather", e)
        return _wx["data"]
    cur, hourly, daily = d["current"], d["hourly"], d["daily"]
    key = cur["time"][:13] + ":00"
    i0 = hourly["time"].index(key) if key in hourly["time"] else 0
    _wx["data"] = {
        "t": round(cur["temperature_2m"], 1), "c": cur["weather_code"], "d": cur["is_day"],
        "hi": round(daily["temperature_2m_max"][0]), "lo": round(daily["temperature_2m_min"][0]),
        "sr": daily["sunrise"][0][11:16], "ss": daily["sunset"][0][11:16], "city": city,
        "h": [[int(hourly["time"][i][11:13]), round(hourly["temperature_2m"][i]), hourly["weather_code"][i]]
              for i in range(i0, i0 + 6)],
    }
    _wx["t"] = time.time()
    return _wx["data"]


def host_stats():
    name = platform.node().split(".")[0][:23]
    if not psutil:
        return {"name": name}
    bat = psutil.sensors_battery()
    up = int(time.time() - psutil.boot_time())
    disk = psutil.disk_usage(os.environ.get("SystemDrive", "C:") + "\\" if os.name == "nt" else "/")
    return {
        "name": name, "cpu": round(psutil.cpu_percent()), "ram": round(psutil.virtual_memory().percent),
        "disk": round(disk.percent), "up": f"{up // 86400}d {up % 86400 // 3600}h",
        "bat": round(bat.percent) if bat else -1, "chg": bool(bat and bat.power_plugged),
    }


def collect(a, accounts, last):
    agents = []
    for acc in accounts:
        try:
            v = acc["src"].fetch()
        except Exception as e:
            log(acc["name"], e)
            v = {"ok": False, "plan": ""}
        if v is None:
            continue
        if v["ok"]:
            last[acc["name"]] = v
        elif acc["name"] in last:
            v = {**last[acc["name"]], "ok": False}
        item = {"n": acc["name"], "k": acc["kind"], **v}
        if acc.get("color"):
            item["c"] = acc["color"].lstrip("#")
        agents.append(item)
    update_history(agents)
    payload = {"t": int(time.time()), "agents": agents, "wx": fetch_weather(a.lat, a.lon, a.city)}
    try:
        payload["pc"] = host_stats()
    except Exception as e:
        log("host", e)
    return payload


def summary(p):
    def pct(w):
        return f"{w[0]:.0f}%" if w else "-"
    return "  ".join(f"{x['n']} {pct(x.get('s'))}/{pct(x.get('w'))}" for x in p["agents"])


def serve(a, accounts):
    import threading
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
    from urllib.parse import parse_qs, urlparse

    state = {"body": b"{}"}

    def loop():
        last = {}
        while True:
            try:
                p = collect(a, accounts, last)
                state["body"] = json.dumps(p, separators=(",", ":")).encode()
                log("updated", summary(p))
            except Exception as e:
                log("collect", e)
            time.sleep(a.interval)

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            u = urlparse(self.path)
            key = (parse_qs(u.query).get("key") or [""])[0]
            if u.path != "/panel.json" or (a.key and key != a.key):
                self.send_response(404)
                self.end_headers()
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(state["body"])))
            self.end_headers()
            self.wfile.write(state["body"])

        def log_message(self, *args):
            pass

    threading.Thread(target=loop, daemon=True).start()
    log(f"serving on :{a.serve}/panel.json")
    ThreadingHTTPServer(("0.0.0.0", a.serve), Handler).serve_forever()


_ip = {}


def push(panel, token, payload):
    headers = {"Content-Type": "application/json"}
    if token:
        headers["X-Token"] = token
    try:
        if panel not in _ip:
            _ip[panel] = socket.gethostbyname(panel)
        req = urllib.request.Request(f"http://{_ip[panel]}/update",
                                     data=json.dumps(payload, separators=(",", ":")).encode(), headers=headers)
        urllib.request.build_opener(urllib.request.ProxyHandler({})).open(req, timeout=5).read()
        return True
    except Exception as e:
        _ip.pop(panel, None)
        log("panel unreachable", e)
        return False


def main():
    ap = argparse.ArgumentParser(description="Collects Claude / Codex limits for the desk panel")
    ap.add_argument("--serve", type=int, default=0, help="run as a server on this port")
    ap.add_argument("--key", default="", help="secret for --serve")
    ap.add_argument("--panel", default="ai-panel.local", help="push to the panel at this address")
    ap.add_argument("--token", default="", help="PANEL_TOKEN for push mode")
    ap.add_argument("--accounts", default="", help="accounts.json, defaults to ~/.claude and ~/.codex")
    ap.add_argument("--lat", type=float, default=55.7558)
    ap.add_argument("--lon", type=float, default=37.6173)
    ap.add_argument("--city", default="Moscow")
    ap.add_argument("--interval", type=int, default=60)
    ap.add_argument("--dry", action="store_true", help="print once and exit")
    a = ap.parse_args()

    if psutil:
        psutil.cpu_percent()
    accounts = load_accounts(a.accounts)

    if a.serve:
        serve(a, accounts)
        return
    last = {}
    while True:
        payload = collect(a, accounts, last)
        if a.dry:
            print(json.dumps(payload, ensure_ascii=False, indent=1))
            return
        if push(a.panel, a.token, payload):
            log("sent", summary(payload))
        time.sleep(a.interval)


if __name__ == "__main__":
    main()
