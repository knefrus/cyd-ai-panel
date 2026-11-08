#!/usr/bin/env bash
set -e

SRC=$(cd "$(dirname "$0")/.." && pwd)
APP=/opt/ai-panel
PORT=${PORT:-8787}
UNIT=/etc/systemd/system/ai-panel.service

echo "-> packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq python3 python3-psutil python3-pil g++ openssl curl iputils-ping >/dev/null

echo "-> files"
mkdir -p "$APP/data"
if [ "$SRC" != "$APP" ]; then
  rm -rf "$APP/server" "$APP/src" "$APP/tools"
  cp -r "$SRC/server" "$SRC/src" "$SRC/tools" "$APP/"
fi

echo "-> renderer"
g++ -O2 -std=c++17 -I"$APP/src" "$APP/tools/render.cpp" "$APP/src/ui.cpp" "$APP/src/model.cpp" "$APP/src/json.cpp" -o "$APP/render"

KEY=$(grep -oE -- '--key [^ ]+' "$UNIT" 2>/dev/null | head -1 | cut -d' ' -f2 || true)
[ -z "$KEY" ] && KEY=$(openssl rand -hex 8)

if [ ! -f "$APP/data/auth.json" ] && [ -t 0 ]; then
  echo "-> password for the editor"
  python3 "$APP/server/hub.py" --set-password --data "$APP/data"
fi

if [ ! -f "$APP/.env" ]; then
  cat > "$APP/.env" <<'ENV'
# Remnawave (только чтение). Пусто = выключено.
REMNA_URL=
REMNA_TOKEN=
REMNA_API_KEY=
REMNA_KEY_HEADER=X-API-Key
# поля ноды, сверяются по --remna-dump. a.b = вложенное поле, a/b = процент a от b
REMNA_NAME_FIELD=name
REMNA_CONNECTED_FIELD=isConnected
REMNA_ONLINE_FIELD=usersOnline
REMNA_CPU_FIELD=system.stats.loadAvg.0/system.info.cpus
REMNA_RAM_FIELD=system.stats.memoryUsed/system.info.memoryTotal
ENV
fi
chmod 600 "$APP/.env"

echo "-> service"
cat > "$UNIT" <<EOF
[Unit]
Description=Desk panel hub
After=network-online.target

[Service]
User=root
EnvironmentFile=-$APP/.env
ExecStart=/usr/bin/python3 $APP/server/hub.py --port $PORT --key $KEY --data $APP/data --render $APP/render
Restart=always
RestartSec=5
Nice=10
MemoryMax=200M

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable ai-panel >/dev/null 2>&1
systemctl restart ai-panel
if command -v ufw >/dev/null && ufw status | grep -q active; then ufw allow "$PORT"/tcp >/dev/null; fi
rm -f "$APP/feeder.py"

sleep 4
IP=$(curl -s -4 --max-time 5 ifconfig.me || hostname -I | cut -d' ' -f1)
if curl -s --max-time 5 "http://localhost:$PORT/v?key=$KEY" >/dev/null; then
  echo
  echo "ok, hub is running"
else
  echo
  echo "!! hub did not answer, see: journalctl -u ai-panel -n 30"
fi
echo
echo "editor:   http://$IP:$PORT/"
echo "DATA_URL: http://$IP:$PORT/panel.json?key=$KEY"
