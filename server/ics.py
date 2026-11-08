import datetime as dt
import re

try:
    from zoneinfo import ZoneInfo
except ImportError:
    ZoneInfo = None

WINDOWS_TZ = {
    "Russian Standard Time": "Europe/Moscow",
    "UTC": "UTC",
    "W. Europe Standard Time": "Europe/Berlin",
    "GMT Standard Time": "Europe/London",
    "FLE Standard Time": "Europe/Helsinki",
    "Central European Standard Time": "Europe/Warsaw",
    "Ekaterinburg Standard Time": "Asia/Yekaterinburg",
    "N. Central Asia Standard Time": "Asia/Novosibirsk",
    "Pacific Standard Time": "America/Los_Angeles",
    "Eastern Standard Time": "America/New_York",
}

DAYS = ["MO", "TU", "WE", "TH", "FR", "SA", "SU"]


def zone(name, fallback):
    if not name or ZoneInfo is None:
        return fallback
    name = WINDOWS_TZ.get(name.strip('"'), name.strip('"'))
    try:
        return ZoneInfo(name)
    except Exception:
        return fallback


def unfold(text):
    out = []
    for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        if line[:1] in (" ", "\t") and out:
            out[-1] += line[1:]
        elif line:
            out.append(line)
    return out


def split_prop(line):
    q = False
    for i, ch in enumerate(line):
        if ch == '"':
            q = not q
        elif ch == ":" and not q:
            head, value = line[:i], line[i + 1:]
            break
    else:
        return None, {}, ""
    parts = head.split(";")
    params = {}
    for p in parts[1:]:
        if "=" in p:
            k, v = p.split("=", 1)
            params[k.upper()] = v
    return parts[0].upper(), params, value


def text_value(v):
    return v.replace("\\n", " ").replace("\\N", " ").replace("\\,", ",").replace("\\;", ";").replace("\\\\", "\\").strip()


def parse_dt(value, params, tz):
    value = value.strip()
    if params.get("VALUE") == "DATE" or re.fullmatch(r"\d{8}", value):
        d = dt.datetime.strptime(value[:8], "%Y%m%d")
        return d.replace(tzinfo=tz), True
    if value.endswith("Z"):
        return dt.datetime.strptime(value[:15], "%Y%m%dT%H%M%S").replace(tzinfo=dt.timezone.utc), False
    return dt.datetime.strptime(value[:15], "%Y%m%dT%H%M%S").replace(tzinfo=zone(params.get("TZID"), tz)), False


def parse_duration(v):
    m = re.fullmatch(r"([+-])?P(?:(\d+)W)?(?:(\d+)D)?(?:T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?)?", v.strip())
    if not m:
        return dt.timedelta(hours=1)
    sign, w, d, h, mi, s = m.groups()
    td = dt.timedelta(weeks=int(w or 0), days=int(d or 0), hours=int(h or 0), minutes=int(mi or 0), seconds=int(s or 0))
    return -td if sign == "-" else td


def parse_rrule(v):
    r = {}
    for part in v.split(";"):
        if "=" in part:
            k, val = part.split("=", 1)
            r[k.upper()] = val
    return r


def add_months(d, n):
    m = d.month - 1 + n
    y = d.year + m // 12
    m = m % 12 + 1
    try:
        return d.replace(year=y, month=m)
    except ValueError:
        return None


def expand(start, rule, until_window, tz):
    freq = rule.get("FREQ", "DAILY")
    interval = int(rule.get("INTERVAL", "1") or 1)
    count = int(rule["COUNT"]) if "COUNT" in rule else None
    until = None
    if "UNTIL" in rule:
        u = rule["UNTIL"]
        until = parse_dt(u, {}, start.tzinfo or tz)[0]
        if len(u) == 8:
            until += dt.timedelta(days=1)
    byday = [d[-2:] for d in rule.get("BYDAY", "").split(",") if d]
    bymonthday = [int(x) for x in rule.get("BYMONTHDAY", "").split(",") if x]
    local = start.replace(tzinfo=None)
    zi = start.tzinfo
    n = 0
    step = 0
    while step < 3000:
        if freq == "WEEKLY":
            week0 = local - dt.timedelta(days=local.weekday()) + dt.timedelta(weeks=step * interval)
            days = [DAYS.index(d) for d in byday] if byday else [local.weekday()]
            cands = [week0 + dt.timedelta(days=i) for i in sorted(days)]
        elif freq == "DAILY":
            cands = [local + dt.timedelta(days=step * interval)]
        elif freq == "MONTHLY":
            base = add_months(local.replace(day=1), step * interval)
            if bymonthday:
                cands = []
                for md in bymonthday:
                    try:
                        cands.append(base.replace(day=md))
                    except ValueError:
                        pass
            else:
                c = add_months(local, step * interval)
                cands = [c] if c else []
        elif freq == "YEARLY":
            try:
                cands = [local.replace(year=local.year + step * interval)]
            except ValueError:
                cands = []
        else:
            return
        step += 1
        for c in cands:
            if c < local:
                continue
            occ = c.replace(tzinfo=zi)
            if until and occ > until:
                return
            if occ > until_window:
                return
            n += 1
            if count and n > count:
                return
            yield occ


def events(text, tz, start_window, end_window):
    lines = unfold(text)
    raw = []
    cur = None
    depth = 0
    for line in lines:
        name, params, value = split_prop(line)
        if name == "BEGIN":
            if value.upper() == "VEVENT":
                cur = {"EXDATE": []}
            elif cur is not None:
                depth += 1
            continue
        if name == "END":
            if value.upper() == "VEVENT" and cur is not None and not depth:
                raw.append(cur)
                cur = None
            elif depth:
                depth -= 1
            continue
        if cur is None or depth:
            continue
        if name == "EXDATE":
            for v in value.split(","):
                try:
                    cur["EXDATE"].append(parse_dt(v, params, tz)[0])
                except ValueError:
                    pass
        elif name in ("DTSTART", "DTEND", "RECURRENCE-ID"):
            try:
                cur[name] = parse_dt(value, params, tz)
            except ValueError:
                pass
        else:
            cur[name] = value

    overrides = set()
    for e in raw:
        if "RECURRENCE-ID" in e:
            overrides.add((e.get("UID"), int(e["RECURRENCE-ID"][0].timestamp())))

    out = []
    for e in raw:
        if "DTSTART" not in e or e.get("STATUS", "").upper() == "CANCELLED":
            continue
        start, allday = e["DTSTART"]
        if "DTEND" in e:
            length = e["DTEND"][0] - start
        elif "DURATION" in e:
            length = parse_duration(e["DURATION"])
        else:
            length = dt.timedelta(days=1) if allday else dt.timedelta(0)
        title = text_value(e.get("SUMMARY", "")) or "Busy"
        ex = {int(x.timestamp()) for x in e["EXDATE"]}
        uid = e.get("UID")
        if "RRULE" in e and "RECURRENCE-ID" not in e:
            occs = expand(start, parse_rrule(e["RRULE"]), end_window, tz)
        else:
            occs = [start]
        for occ in occs:
            ts = int(occ.timestamp())
            if ts in ex or ("RRULE" in e and (uid, ts) in overrides and "RECURRENCE-ID" not in e):
                continue
            end = occ + length
            if end.timestamp() <= start_window.timestamp() or occ > end_window:
                continue
            out.append((ts, int(end.timestamp()), title, allday))
    out.sort()
    return out
