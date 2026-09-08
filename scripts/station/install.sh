#!/usr/bin/env bash
# Offline installation: never apt, pip, curl or system library replacement.
set -Eeuo pipefail
export PATH="/usr/local/sbin:/usr/sbin:/sbin:${PATH}"
PACKAGE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX=/opt/satdump-station
CONFIG=/etc/satdump-station
DATA=/var/lib/satdump-station
LISTEN=127.0.0.1
PORT=8090
START=1
DRY=0
ROLLBACK=0
ALLOW_COMPATIBLE=0
LISTEN_SET=0
PORT_SET=0
DATA_SET=0
fail() { printf 'Ошибка: %s\n' "$*" >&2; exit 1; }
while (( $# )); do
    case "$1" in
        --data-dir) DATA="${2:?нужен путь}"; DATA_SET=1; shift 2 ;;
        --listen) LISTEN="${2:?нужен IPv4}"; LISTEN_SET=1; shift 2 ;;
        --port) PORT="${2:?нужен порт}"; PORT_SET=1; shift 2 ;;
        --no-start) START=0; shift ;;
        --dry-run) DRY=1; shift ;;
        --rollback) ROLLBACK=1; shift ;;
        --allow-compatible) ALLOW_COMPATIBLE=1; shift ;;
        -h|--help)
            echo 'sudo ./install.sh [--data-dir /path] [--listen IPv4] [--port 8090] [--no-start] [--dry-run]'
            echo '--rollback: вернуть предыдущий код, не трогая конфигурацию и данные.'
            echo '--allow-compatible: только стенд Debian, не подтверждает работу в Astra.'
            exit 0 ;;
        *) fail "Неизвестный параметр $1" ;;
    esac
done
[[ "$PORT" =~ ^[0-9]+$ ]] && (( PORT >= 1024 && PORT <= 65535 )) || fail 'Порт: 1024..65535'
[[ "$LISTEN" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail 'Нужен IPv4-адрес'
[[ "$DATA" =~ ^/[A-Za-z0-9_./-]+$ && "$DATA" != *..* ]] || fail 'Некорректный каталог данных'
case "$DATA" in /|/var|/opt|/usr|/etc|/home|/root|/tmp) fail 'Нужен отдельный каталог данных' ;; esac
if [[ -r "$CONFIG/station.json" ]]; then
    existing="$("$PREFIX/current/runtime/python" -c 'import json;print(json.load(open("/etc/satdump-station/station.json"))["data_dir"])')"
    (( DATA_SET == 0 )) || [[ "$existing" == "$DATA" ]] || fail 'Перенос существующих данных выполняется отдельно'
    DATA="$existing"
fi
[[ "$DATA" =~ ^/[A-Za-z0-9_./-]+$ && "$DATA" != *..* ]] || fail 'Некорректный сохранённый каталог данных'
case "$DATA" in /|/var|/opt|/usr|/etc|/home|/root|/tmp) fail 'Нужен отдельный каталог данных' ;; esac
if (( ALLOW_COMPATIBLE == 0 )); then
    [[ -r /etc/astra_version ]] && grep -Eq '^1\.6([.[:space:]]|$)' /etc/astra_version || fail 'Целевая ОС — Astra Linux 1.6'
fi
if (( DRY == 1 )); then
    printf 'План: код %s; конфигурация %s; данные %s; сайт %s:%s; запуск=%s\n' "$PREFIX" "$CONFIG" "$DATA" "$LISTEN" "$PORT" "$START"
    exit 0
fi
(( EUID == 0 )) || fail 'Запустите установку через sudo'
install -d -m 0755 /run/lock /etc/systemd/system /usr/local/bin
exec 9>/run/lock/satdump-station-install.lock
flock -n 9 || fail 'Установщик уже работает'
if (( ROLLBACK == 1 )); then
    target="$(readlink -f "$PREFIX/previous")"
    [[ "$target" == "$PREFIX/releases/"* && -x "$target/station.sh" ]] || fail 'Нет предыдущей версии'
    "$target/runtime/python" "$target/scripts/station/pack.py" --verify "$target"
else
    [[ -x "$PACKAGE/runtime/python" && -x "$PACKAGE/engine/satdump" ]] || fail 'Это не готовый пакет. Сначала ./station.sh build'
    "$PACKAGE/runtime/python" "$PACKAGE/scripts/station/pack.py" --verify "$PACKAGE"
    "$PACKAGE/runtime/python" -c 'import sqlite3,ssl; from PIL import Image; print("Python runtime OK")'
    "$PACKAGE/engine/satdump" version
    version="$("$PACKAGE/runtime/python" -c 'import json,sys;print(json.load(open(sys.argv[1]))["release_id"])' "$PACKAGE/PACKAGE-MANIFEST.json")"
    [[ "$version" =~ ^[a-zA-Z0-9._-]+$ ]] || fail 'Некорректный release_id'
    target="$PREFIX/releases/$version"
    mkdir -p "$PREFIX/releases"
    if [[ ! -d "$target" ]]; then
        stage="$(mktemp -d "$PREFIX/releases/.install.XXXXXXXX")"
        trap '[[ -z "${stage:-}" ]] || rm -rf -- "$stage"' EXIT
        cp -a "$PACKAGE/." "$stage/"
        chown -R root:root "$stage"
        chmod -R a+rX,go-w "$stage"
        mv "$stage" "$target"
        stage=""
    else
        cmp "$PACKAGE/SHA256SUMS" "$target/SHA256SUMS" || fail 'Такая версия уже установлена с другим содержимым'
        "$PACKAGE/runtime/python" "$target/scripts/station/pack.py" --verify "$target"
    fi
fi
getent group satdump-station >/dev/null || groupadd --system satdump-station
id satdump-station >/dev/null 2>&1 || useradd --system --gid satdump-station --home-dir "$DATA" --shell /usr/sbin/nologin satdump-station
id satdump-web >/dev/null 2>&1 || useradd --system --user-group --no-create-home --home-dir /nonexistent --shell /usr/sbin/nologin satdump-web
install -d -m 0750 -o root -g satdump-station "$CONFIG"
install -d -m 0755 -o root -g root "$DATA"
for folder in state work logs archive inbox inbox/images inbox/products inbox/meteor_iq; do
    install -d -m 0750 -o satdump-station -g satdump-station "$DATA/$folder"
done
install -d -m 0755 -o satdump-station -g satdump-station "$DATA/public" "$DATA/public/items"
if [[ ! -f "$CONFIG/station.json" ]]; then
    "$target/runtime/python" - "$target/config/station/station.json" "$CONFIG/station.json" "$DATA" <<'PY'
import json,sys
cfg=json.load(open(sys.argv[1]))
old=cfg['data_dir'];cfg['data_dir']=sys.argv[3]
for source in cfg['sources']:
    source['path']=sys.argv[3]+source['path'][len(old):]
with open(sys.argv[2],'w') as out:json.dump(cfg,out,ensure_ascii=False,indent=2)
PY
    cp "$target/config/station/processing.json" "$CONFIG/processing.json"
fi
cp "$target/config/station/station.json" "$CONFIG/station.json.example"
cp "$target/config/station/processing.json" "$CONFIG/processing.json.example"
backup="$(mktemp -d /tmp/satdump-units.XXXXXXXX)"
[[ ! -f "$CONFIG/web.env" ]] || cp -a "$CONFIG/web.env" "$backup/web.env"
trap '[[ -z "${stage:-}" ]] || rm -rf -- "$stage"; [[ -z "${backup:-}" ]] || rm -rf -- "$backup"' EXIT
if [[ -f "$CONFIG/web.env" ]]; then
    # Parse data, not shell: preserve each setting unless explicitly changed.
    while IFS='=' read -r key value; do
        case "$key" in
            SATDUMP_HOST) if (( LISTEN_SET == 0 )); then LISTEN="$value"; fi ;;
            SATDUMP_PORT) if (( PORT_SET == 0 )); then PORT="$value"; fi ;;
        esac
    done < "$CONFIG/web.env"
fi
[[ "$PORT" =~ ^[0-9]+$ ]] && (( PORT >= 1024 && PORT <= 65535 )) || fail 'Некорректный сохранённый порт'
"$target/runtime/python" -c 'import ipaddress,sys; ipaddress.IPv4Address(sys.argv[1])' "$LISTEN"
if [[ ! -f "$CONFIG/web.env" ]] || (( LISTEN_SET || PORT_SET )); then
    printf 'SATDUMP_HOST=%s\nSATDUMP_PORT=%s\n' "$LISTEN" "$PORT" > "$CONFIG/web.env"
fi
chown root:satdump-station "$CONFIG"/*
chmod 0640 "$CONFIG"/*
"$target/runtime/python" - "$target" "$CONFIG/station.json" "$LISTEN" <<'PYCODE'
import importlib.util,sys,ipaddress
spec=importlib.util.spec_from_file_location('station',sys.argv[1]+'/services/station/station.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
cfg=module.load_config(sys.argv[2])
module.read_json(module.Path(cfg['_config_dir'])/cfg.get('processing_config','processing.json'))
ipaddress.IPv4Address(sys.argv[3])
PYCODE
for unit in satdump-worker.service satdump-web.service; do
    [[ ! -f "/etc/systemd/system/$unit" ]] || cp -a "/etc/systemd/system/$unit" "$backup/$unit"
done
restore_units() {
    local unit
    for unit in satdump-worker.service satdump-web.service; do
        if [[ -f "$backup/$unit" ]]; then cp -a "$backup/$unit" "/etc/systemd/system/$unit"; else rm -f "/etc/systemd/system/$unit"; fi
    done
    if [[ -f "$backup/web.env" ]]; then cp -a "$backup/web.env" "$CONFIG/web.env"; fi
    systemctl daemon-reload
}
trap '[[ -z "${stage:-}" ]] || rm -rf -- "$stage"; [[ -z "${backup:-}" ]] || rm -rf -- "$backup"' EXIT
# The web account never receives access to the queue, input data, processing config or logs.
cat > /etc/systemd/system/satdump-worker.service <<EOF
[Unit]
Description=SatDump acquisition processing queue
After=local-fs.target remote-fs.target
RequiresMountsFor=$DATA
[Service]
Type=simple
User=satdump-station
Group=satdump-station
ExecStart=$PREFIX/current/station.sh worker --config $CONFIG/station.json
WorkingDirectory=$DATA/work
Restart=on-failure
RestartSec=10
TimeoutStopSec=30
KillMode=control-group
UMask=0022
Nice=10
NoNewPrivileges=true
PrivateTmp=true
PrivateDevices=true
PrivateNetwork=true
ProtectSystem=full
ReadOnlyDirectories=/
ReadWriteDirectories=$DATA/state $DATA/work $DATA/logs $DATA/archive $DATA/public
[Install]
WantedBy=multi-user.target
EOF
cat > /etc/systemd/system/satdump-web.service <<EOF
[Unit]
Description=SatDump read-only satellite gallery
After=local-fs.target network.target
RequiresMountsFor=$DATA/public
[Service]
Type=simple
User=satdump-web
Group=satdump-web
EnvironmentFile=$CONFIG/web.env
ExecStart=$PREFIX/current/station.sh serve --public $DATA/public --host \${SATDUMP_HOST} --port \${SATDUMP_PORT}
Restart=on-failure
RestartSec=5
NoNewPrivileges=true
PrivateTmp=true
PrivateDevices=true
ProtectSystem=full
ProtectHome=true
ReadOnlyDirectories=/
[Install]
WantedBy=multi-user.target
EOF
# Test candidate before stopping any healthy installed version.
runuser -u satdump-station -- "$target/runtime/python" -c 'import sqlite3; from PIL import Image'
previous=""
if [[ -L "$PREFIX/current" && -d "$PREFIX/current" ]]; then previous="$(readlink -f "$PREFIX/current")"; fi
if (( START )); then systemctl stop satdump-worker.service satdump-web.service 2>/dev/null || true; fi
ln -sfn "$target" "$PREFIX/.next"
mv -Tf "$PREFIX/.next" "$PREFIX/current"
if [[ -n "$previous" && "$previous" != "$target" ]]; then ln -sfn "$previous" "$PREFIX/previous"; fi
ln -sfn "$PREFIX/current/station.sh" /usr/local/bin/satdump-station
if (( START )); then
    systemctl daemon-reload
    systemctl enable satdump-worker.service satdump-web.service
    healthy=0
    if systemctl restart satdump-worker.service satdump-web.service; then
        for attempt in 1 2 3 4 5; do
            sleep 2
            if systemctl is-active --quiet satdump-worker.service &&
                systemctl is-active --quiet satdump-web.service &&
                "$target/runtime/python" - "$CONFIG/web.env" <<'PYCHECK'
import json,sys
from urllib.request import urlopen
values=dict(line.strip().split('=',1) for line in open(sys.argv[1]) if '=' in line)
host=values['SATDUMP_HOST'];host='127.0.0.1' if host=='0.0.0.0' else host
with urlopen('http://'+host+':'+values['SATDUMP_PORT']+'/health.json',timeout=2) as response:
    status=json.loads(response.read().decode('utf-8'))
    if not (status.get('web_alive') and status.get('worker_alive')):raise SystemExit(1)
PYCHECK
            then healthy=1; break; fi
        done
    fi
    if (( healthy == 0 )); then
        systemctl stop satdump-worker.service satdump-web.service || true
        restore_units
        if [[ -n "$previous" ]]; then
            ln -sfn "$previous" "$PREFIX/.next"; mv -Tf "$PREFIX/.next" "$PREFIX/current"
            systemctl restart satdump-worker.service satdump-web.service || true
        fi
        fail 'Проверка запуска не пройдена; предыдущий код и службы восстановлены при наличии'
    fi
fi
printf 'Установлено: %s\nНастройки: %s\nДанные: %s\nСайт: порт из %s/web.env\n' "$target" "$CONFIG" "$DATA" "$CONFIG"
