import json
import os
import time
import urllib.error
import urllib.parse
import urllib.request

SECRET_HINTS = ("key", "secret", "token", "cert", "password", "address", "ip", "host", "port", "uuid")


def load_env(path):
    try:
        lines = open(path, encoding="utf-8").read().splitlines()
    except OSError:
        return
    vals = {}
    for line in lines:
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        v = v.strip().strip('"').strip("'")
        if v:
            vals[k.strip()] = v
    for k, v in vals.items():
        if not os.environ.get(k):
            os.environ[k] = v


def pick(obj, spec):
    if not spec:
        return None
    if "/" in spec:
        a, b = (pick(obj, s) for s in spec.split("/", 1))
        try:
            return float(a) / float(b) * 100 if float(b) else None
        except (TypeError, ValueError):
            return None
    cur = obj
    for part in spec.split("."):
        if isinstance(cur, dict) and part in cur:
            cur = cur[part]
        elif isinstance(cur, list) and part.isdigit() and int(part) < len(cur):
            cur = cur[int(part)]
        else:
            return None
    return cur


def unwrap(d):
    return d.get("response", d) if isinstance(d, dict) else d


class Remna:
    def __init__(self):
        e = os.environ.get
        self.url = e("REMNA_URL", "").rstrip("/")
        self.token = e("REMNA_TOKEN", "")
        self.key = e("REMNA_API_KEY", "")
        self.key_header = e("REMNA_KEY_HEADER", "X-API-Key")
        self.f_name = e("REMNA_NAME_FIELD", "name")
        self.f_up = e("REMNA_CONNECTED_FIELD", "isConnected")
        self.f_users = e("REMNA_ONLINE_FIELD", "usersOnline")
        self.f_cpu = e("REMNA_CPU_FIELD", "")
        self.f_ram = e("REMNA_RAM_FIELD", "")
        self.f_off = e("REMNA_DISABLED_FIELD", "isDisabled")

    @property
    def configured(self):
        return bool(self.url and self.token)

    def headers(self):
        h = {"Authorization": f"Bearer {self.token}", "Accept": "application/json", "User-Agent": "cyd-ai-panel"}
        if self.key:
            h[self.key_header] = self.key
        host = urllib.parse.urlparse(self.url).hostname or ""
        if host in ("127.0.0.1", "localhost"):
            h["X-Forwarded-For"] = "127.0.0.1"
            h["X-Forwarded-Proto"] = "https"
        return h

    def get(self, path, timeout=12):
        req = urllib.request.Request(self.url + path, headers=self.headers(), method="GET")
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode())

    def nodes(self):
        data = unwrap(self.get("/api/nodes"))
        if isinstance(data, dict):
            data = data.get("nodes", [])
        out = []
        for n in data or []:
            if self.f_off and pick(n, self.f_off) is True:
                continue
            num = lambda v: None if v is None else max(0, min(100, round(float(v))))
            try:
                cpu, ram = num(pick(n, self.f_cpu)), num(pick(n, self.f_ram))
            except (TypeError, ValueError):
                cpu = ram = None
            out.append({
                "name": str(pick(n, self.f_name) or "?"),
                "up": bool(pick(n, self.f_up)),
                "users": int(pick(n, self.f_users) or 0),
                "cpu": cpu,
                "ram": ram,
            })
        return out


def short_error(e):
    if isinstance(e, urllib.error.HTTPError):
        return f"HTTP {e.code}"
    if isinstance(e, urllib.error.URLError):
        return f"network: {e.reason}"[:80]
    return type(e).__name__


def redact(v, key=""):
    if isinstance(v, dict):
        return {k: redact(x, k) for k, x in v.items()}
    if isinstance(v, list):
        return [redact(x, key) for x in v[:3]] + (["…"] if len(v) > 3 else [])
    if isinstance(v, str) and any(h in key.lower() for h in SECRET_HINTS):
        return "<hidden>"
    return v


def flatten(v, prefix="", out=None):
    out = {} if out is None else out
    if isinstance(v, dict):
        for k, x in v.items():
            flatten(x, f"{prefix}.{k}" if prefix else k, out)
    elif isinstance(v, list) and v and isinstance(v[0], dict):
        flatten(v[0], prefix + ".0", out)
    else:
        out[prefix] = v
    return out


def dump():
    r = Remna()
    if not r.configured:
        print("REMNA_URL и REMNA_TOKEN не заданы в .env")
        return
    print(f"panel: {r.url}  key header: {r.key_header}{' (есть)' if r.key else ' (ключа нет)'}\n")
    for path in ("/api/nodes", "/api/system/stats", "/api/system/stats/nodes", "/api/nodes/usage/realtime"):
        try:
            d = unwrap(r.get(path))
        except Exception as e:
            print(f"GET {path}: {short_error(e)}\n")
            continue
        sample = d[0] if isinstance(d, list) and d else d
        print(f"GET {path}: ok, {'list of ' + str(len(d)) if isinstance(d, list) else 'object'}")
        for k, v in flatten(redact(sample)).items():
            s = json.dumps(v, ensure_ascii=False)
            print(f"  {k} = {s[:60]}")
        print()
    try:
        spec = r.get("/api/docs-json", timeout=20)
        paths = [p for p in spec.get("paths", {}) if "node" in p.lower() or "system" in p.lower()]
        print("openapi paths:")
        for p in sorted(paths):
            print("  " + p, ",".join(m.upper() for m in spec["paths"][p]))
    except Exception as e:
        print(f"openapi (/api/docs-json): {short_error(e)}")
    try:
        nodes = r.nodes()
        print("\nкак хаб видит ноды с текущими полями:")
        for n in nodes:
            print(f"  {'UP  ' if n['up'] else 'DOWN'} {n['name']:<16} users={n['users']} cpu={n['cpu']} ram={n['ram']}")
    except Exception as e:
        print("nodes:", short_error(e))
