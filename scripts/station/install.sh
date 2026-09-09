#!/usr/bin/env bash
# Self-contained offline installation. Never apt/pip/curl, never replace libc.
set -Eeuo pipefail
export PATH="/usr/local/sbin:/usr/sbin:/sbin:$PATH"
PACKAGE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX=/opt/satdump-station CONFIG=/etc/satdump-station DATA=/var/lib/satdump-station
HOST=127.0.0.1 PORT=8090 CONTROL_PORT=8091 BACKEND_PORT=8092 WEB_SERVER=builtin
SOURCE_PATH='' SOURCE_KIND=image INTERACTIVE=auto COLOR=auto ANIMATION=auto
DRY=0 START=1 ROLLBACK=0 COMPAT=0 YES=0
DATA_SET=0 HOST_SET=0 PORT_SET=0 WEB_SET=0 CONTROL_SET=0 BACKEND_SET=0
STAGE='' BACKUP='' MUTATED=0 COMMITTED=0 INSTALL_LOG=''
UNITS=(satdump-worker.service satdump-control.service satdump-board.service satdump-web.service)
# shellcheck source=scripts/station/terminal.sh
source "$PACKAGE/scripts/station/terminal.sh"
fail() { ui_error "$*"; exit 1; }
value() { [[ $# -ge 2 && -n $2 && $2 != --* ]] || { printf 'Нужно значение для %s\n' "$1" >&2; exit 2; }; }
while (( $# )); do
    case "$1" in
        --data-dir) value "$@"; DATA=$2; DATA_SET=1; shift 2 ;;
        --listen) value "$@"; HOST=$2; HOST_SET=1; shift 2 ;;
        --port) value "$@"; PORT=$2; PORT_SET=1; shift 2 ;;
        --control-port) value "$@"; CONTROL_PORT=$2; CONTROL_SET=1; shift 2 ;;
        --backend-port) value "$@"; BACKEND_PORT=$2; BACKEND_SET=1; shift 2 ;;
        --web-server) value "$@"; WEB_SERVER=$2; WEB_SET=1; shift 2 ;;
        --source-path) value "$@"; SOURCE_PATH=$2; shift 2 ;;
        --source-kind) value "$@"; SOURCE_KIND=$2; shift 2 ;;
        --interactive) INTERACTIVE=always; shift ;;
        --non-interactive) INTERACTIVE=never; shift ;;
        --yes|-y) YES=1; INTERACTIVE=never; shift ;;
        --no-color) COLOR=never; shift ;;
        --no-animation) ANIMATION=never; shift ;;
        --dry-run) DRY=1; shift ;;
        --no-start) START=0; shift ;;
        --rollback) ROLLBACK=1; INTERACTIVE=never; shift ;;
        --allow-compatible) COMPAT=1; shift ;;
        -h|--help)
            cat <<'EOF'
SatDump Station / BOARD — автономная установка
  sudo ./install.sh                         мастер в терминале
  sudo ./install.sh --yes                    значения по умолчанию без вопросов
  sudo ./install.sh --interactive            явно включить мастер
  sudo ./install.sh --non-interactive        режим сценария/SSH/CI
  ./install.sh --dry-run --allow-compatible  показать план на стенде

  --data-dir DIR       выделенный каталог данных (обновление сохраняет старый)
  --listen IPv4        WEB: 127.0.0.1 по умолчанию; 0.0.0.0 — доверенная ЛВС
  --port N            WEB-порт, по умолчанию 8090
  --web-server MODE   builtin (автономный) или nginx (уже установленный /usr/sbin/nginx)
  --control-port N    локальный авторизованный API, по умолчанию 8091
  --backend-port N    внутренний BOARD при nginx, по умолчанию 8092
  --source-path DIR   подключить источник receiver (готовые файлы с .ready)
  --source-kind TYPE  image или product; параметры IQ задаются через API/конфигурацию
  --no-start          подготовить на остановленной станции; не проверяет запуск
  --rollback          восстановить предыдущие код/настройки/службы, не данные
  --no-color          без ANSI-цвета; также поддерживается NO_COLOR
  --no-animation      без индикатора активности

В мастере: Enter — значение; b — назад; q — отменить. Автопроцентов и ETA нет.
Astra 1.5 не поддерживается этим пакетом. --allow-compatible — только стенд,
не подтверждение приёмки Astra. Рабочая ОС и библиотеки не обновляются.
EOF
            exit 0 ;;
        *) printf 'Неизвестный параметр: %s\n' "$1" >&2; exit 2 ;;
    esac
done
ui_init
if [[ -f $CONFIG/station.json ]]; then
    [[ -x $PREFIX/current/runtime/python ]] || fail 'Нет runtime действующей версии. Требуется восстановление пакета.'
    EXISTING_DATA=$("$PREFIX/current/runtime/python" -c 'import json;print(json.load(open("/etc/satdump-station/station.json"))["data_dir"])')
    (( DATA_SET == 0 )) || [[ $EXISTING_DATA == "$DATA" ]] || fail 'data_dir уже задан; перенос требует отдельной миграции.'
    DATA=$EXISTING_DATA
fi
if [[ -f $CONFIG/web.env ]]; then
    while IFS='=' read -r key setting; do
        case "$key" in
            SATDUMP_HOST) (( HOST_SET )) || HOST=$setting ;;
            SATDUMP_PORT) (( PORT_SET )) || PORT=$setting ;;
            SATDUMP_CONTROL_PORT) (( CONTROL_SET )) || CONTROL_PORT=$setting ;;
            SATDUMP_BACKEND_PORT) (( BACKEND_SET )) || BACKEND_PORT=$setting ;;
            SATDUMP_WEB_SERVER) (( WEB_SET )) || WEB_SERVER=$setting ;;
        esac
    done < "$CONFIG/web.env"
fi
valid_path() {
    [[ $1 =~ ^/[A-Za-z0-9_./-]+$ && $1 != *..* ]] || return 1
    case "$1" in /|/var|/opt|/home|/root|/tmp|/etc|/usr|/proc*|/sys*|/dev*|/run*) return 1 ;; esac
}
valid_port() { [[ $1 =~ ^[0-9]{4,5}$ ]] && (( 10#$1 >= 1024 && 10#$1 <= 65535 )); }
valid_ip() {
    local a b c d extra
    [[ $1 =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || return 1
    IFS=. read -r a b c d extra <<< "$1"
    local n
    for n in "$a" "$b" "$c" "$d"; do
        [[ $n == 0 || $n != 0* ]] && (( ${#n} <= 3 && 10#$n <= 255 )) || return 1
    done
}
if [[ $INTERACTIVE == always && ( ! -t 0 || ! -t 2 ) ]]; then fail '--interactive требует терминал'; fi
if [[ $INTERACTIVE == always || ( $INTERACTIVE == auto && -t 0 && -t 2 ) ]]; then
    ui_heading 'SatDump / BOARD   •   Мастер установки'
    ui_info 'Enter — принять, b — назад, q — отмена. До итогового подтверждения изменений нет.'
    STEP=0
    while (( STEP < 6 )); do
        rc=0
        case "$STEP" in
            0) ui_ask '1/6  Каталог данных' "$DATA" || rc=$? ;;
            1) ui_ask '2/6  IPv4 сайта (локально: 127.0.0.1; ЛВС: 0.0.0.0)' "$HOST" || rc=$? ;;
            2) ui_ask '3/6  Порт сайта' "$PORT" || rc=$? ;;
            3) ui_ask '4/6  Веб-сервер: builtin или nginx' "$WEB_SERVER" || rc=$? ;;
            4) ui_ask '5/6  Входная папка приёмника (- = оставить текущие входы)' "${SOURCE_PATH:--}" || rc=$? ;;
            5) ui_ask '6/6  Содержимое входа: image или product' "$SOURCE_KIND" || rc=$? ;;
        esac
        (( rc != 20 )) || { ui_info 'Отменено. Изменения не внесены.'; exit 0; }
        if (( rc == 10 )); then (( STEP == 0 )) || STEP=$((STEP - 1)); continue; fi
        case "$STEP" in
            0) valid_path "$UI_ANSWER" || { ui_error 'Нужен выделенный абсолютный путь'; continue; }
               [[ -z ${EXISTING_DATA:-} || $EXISTING_DATA == "$UI_ANSWER" ]] || { ui_error 'Установленные данные переносить этим мастером нельзя'; continue; }; DATA=$UI_ANSWER ;;
            1) valid_ip "$UI_ANSWER" || { ui_error 'Некорректный IPv4'; continue; }; HOST=$UI_ANSWER ;;
            2) valid_port "$UI_ANSWER" || { ui_error 'Порт: 1024..65535'; continue; }; PORT=$UI_ANSWER ;;
            3) [[ $UI_ANSWER == builtin || $UI_ANSWER == nginx ]] || { ui_error 'Выберите builtin или nginx'; continue; }; WEB_SERVER=$UI_ANSWER ;;
            4) if [[ $UI_ANSWER == - ]]; then SOURCE_PATH=''; else valid_path "$UI_ANSWER" || { ui_error 'Некорректный путь'; continue; }; SOURCE_PATH=$UI_ANSWER; fi ;;
            5) [[ $UI_ANSWER == image || $UI_ANSWER == product ]] || { ui_error 'Выберите image или product'; continue; }; SOURCE_KIND=$UI_ANSWER ;;
        esac
        STEP=$((STEP + 1))
    done
    YES=0
fi
valid_path "$DATA" || fail 'Некорректный каталог данных'
case "$DATA" in /etc/*|/usr/*|/bin/*|/sbin/*|/lib/*|/boot/*) fail 'Данные нельзя размещать в системных каталогах' ;; esac
valid_ip "$HOST" || fail 'Некорректный IPv4'
for port in "$PORT" "$CONTROL_PORT" "$BACKEND_PORT"; do valid_port "$port" || fail 'Порт должен быть 1024..65535'; done
[[ $PORT != "$CONTROL_PORT" && $PORT != "$BACKEND_PORT" && $CONTROL_PORT != "$BACKEND_PORT" ]] || fail 'Порты должны различаться'
[[ $WEB_SERVER == builtin || $WEB_SERVER == nginx ]] || fail 'Веб-сервер: builtin или nginx'
[[ $SOURCE_KIND == image || $SOURCE_KIND == product ]] || fail 'Вход: image или product'
[[ -z $SOURCE_PATH ]] || valid_path "$SOURCE_PATH" || fail 'Некорректный путь источника'
if [[ -r /etc/astra_version ]]; then
    grep -Eq '^1\.6([.[:space:]]|$)' /etc/astra_version || fail 'Этот пакет только для Astra 1.6, не Astra 1.5/1.7'
elif (( COMPAT == 0 )); then fail 'Целевая ОС — Astra 1.6. Стенд: --allow-compatible'; fi
ui_heading 'План установки'
ui_info "Код: $PREFIX/releases; конфигурация: $CONFIG"
ui_info "Данные и BOARD: $DATA; WEB: $HOST:$PORT ($WEB_SERVER)"
ui_info "API: 127.0.0.1:$CONTROL_PORT, отдельный токен и пользователь"
ui_info "Автозапуск WEB + обработчик + API: $START; внешний источник: ${SOURCE_PATH:-без изменения}"
ui_info 'Интернет/apt/pip не используются. Дизайн и новый веб-интерфейс не устанавливаются.'
if (( DRY )); then ui_info 'План без изменений; это не тест бинарного пакета или systemd.'; exit 0; fi
if [[ -t 0 && $INTERACTIVE != never && $YES == 0 ]]; then ui_confirm || { ui_info 'Отменено.'; exit 0; }; fi
(( EUID == 0 )) || fail 'Запустите через sudo'
[[ $(uname -m) == x86_64 ]] || fail 'Нужна архитектура x86_64'
SYSTEMD_LIVE=0
if command -v systemctl >/dev/null && [[ -d /run/systemd/system ]]; then SYSTEMD_LIVE=1; fi
(( START == 0 || SYSTEMD_LIVE == 1 )) || fail 'Для запуска нужен работающий systemd; для подготовки образа есть --no-start'
svc() {
    if (( SYSTEMD_LIVE )); then command systemctl "$@"; return $?; fi
    case $1 in is-*) return 1 ;; daemon-reload|disable|stop) return 0 ;; *) return 1 ;; esac
}
command -v flock >/dev/null || fail 'Не найден flock'
install -d -m 0755 /run/lock /etc/systemd/system /usr/local/bin "$PREFIX/releases"
exec 9>/run/lock/satdump-station-install.lock
flock -n 9 || fail 'Установщик уже работает'
INSTALL_LOG=$(mktemp /var/log/satdump-install.XXXXXXXX.log)
chmod 0600 "$INSTALL_LOG"

snapshot() {
    local destination=$1 unit
    mkdir -p "$destination/units"
    [[ ! -d $CONFIG ]] || cp -a "$CONFIG" "$destination/config"
    for unit in current previous; do
        : > "$destination/$unit"
        if [[ -L $PREFIX/$unit && -d $PREFIX/$unit ]]; then readlink -f "$PREFIX/$unit" > "$destination/$unit"; fi
    done
    # Managed API settings are outside /etc. Snapshot their atomic active pointer
    # too; otherwise a failed --source-path update survives configuration rollback.
    : > "$destination/control-snapshot"
    [[ ! -f "$DATA/control/active.json" ]] || cp -a "$DATA/control/active.json" "$destination/control-active.json"
    : > "$destination/active"; : > "$destination/enabled"
    for unit in "${UNITS[@]}"; do
        [[ ! -f /etc/systemd/system/$unit ]] || cp -a "/etc/systemd/system/$unit" "$destination/units/$unit"
        if svc is-active --quiet "$unit"; then printf '%s\n' "$unit" >> "$destination/active"; fi
        if svc is-enabled --quiet "$unit"; then printf '%s\n' "$unit" >> "$destination/enabled"; fi
    done
}
restore() {
    local source=$1 unit link target
    svc stop "${UNITS[@]}" >>"$INSTALL_LOG" 2>&1 || true
    svc disable "${UNITS[@]}" >>"$INSTALL_LOG" 2>&1 || true
    rm -rf -- "$CONFIG"
    [[ ! -d $source/config ]] || cp -a "$source/config" "$CONFIG"
    for unit in "${UNITS[@]}"; do
        rm -f "/etc/systemd/system/$unit"
        [[ ! -f $source/units/$unit ]] || cp -a "$source/units/$unit" "/etc/systemd/system/$unit"
    done
    for link in current previous; do
        target=$(cat "$source/$link")
        rm -f "$PREFIX/$link"
        if [[ -n $target ]]; then
            [[ $target == "$PREFIX/releases/"* && -d $target ]] || return 1
            ln -s "$target" "$PREFIX/$link"
        fi
    done
    if [[ ! -L $PREFIX/current ]]; then rm -f /usr/local/bin/satdump-station; fi
    if [[ -f "$source/control-snapshot" && -d "$DATA/control" ]]; then
        if [[ -f "$source/control-active.json" ]]; then
            cp -a "$source/control-active.json" "$DATA/control/.restore-active.json"
            mv -Tf "$DATA/control/.restore-active.json" "$DATA/control/active.json"
        else
            rm -f "$DATA/control/active.json"
        fi
    fi
    svc daemon-reload
    while IFS= read -r unit; do [[ -z $unit ]] || svc enable "$unit"; done < "$source/enabled"
    while IFS= read -r unit; do [[ -z $unit ]] || svc start "$unit"; done < "$source/active"
}
cleanup() {
    local rc=$?
    trap - EXIT ERR INT TERM
    ui_cleanup
    if (( MUTATED && ! COMMITTED )); then
        ui_error 'Активация не завершена. Восстанавливаю предыдущие код, настройки и службы.'
        if ! restore "$BACKUP" >>"$INSTALL_LOG" 2>&1; then ui_error "Автооткат неполный. Снимок: $BACKUP; журнал: $INSTALL_LOG"; fi
    fi
    [[ -z $STAGE ]] || rm -rf -- "$STAGE"
    if (( ! MUTATED || COMMITTED )); then [[ -z $BACKUP ]] || rm -rf -- "$BACKUP"; fi
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
if (( ROLLBACK )); then
    SAVED="$PREFIX/rollback-state"
    [[ -s $SAVED/current ]] || fail 'Нет сохранённого состояния для отката'
    TARGET=$(cat "$SAVED/current")
    [[ $TARGET == "$PREFIX/releases/"* && -x $TARGET/runtime/python ]] || fail 'Некорректная предыдущая версия'
    ui_step 'Проверка предыдущего пакета' "$TARGET/runtime/python" "$TARGET/scripts/station/pack.py" --verify "$TARGET"
    BACKUP=$(mktemp -d "$PREFIX/.rollback.XXXXXXXX")
    snapshot "$BACKUP"
    MUTATED=1
    restore "$SAVED"
    rm -rf -- "$SAVED"
    mv "$BACKUP" "$SAVED"; BACKUP=''
    COMMITTED=1
    ui_heading 'Предыдущие код и службы восстановлены. Данные не откатывались.'
    exit 0
fi
[[ -x $PACKAGE/runtime/python && -x $PACKAGE/engine/satdump ]] || fail 'Нужен полный бинарный Station-пакет. В исходниках: ./station.sh build'
PYTHON="$PACKAGE/runtime/python"
ui_step 'Контрольные суммы и состав пакета' "$PYTHON" "$PACKAGE/scripts/station/pack.py" --verify "$PACKAGE"
ui_step 'Встроенный Python, Pillow и библиотеки' "$PYTHON" -c 'import sqlite3,ssl;from PIL import Image;print("runtime OK")'
ui_step 'Запуск движка SatDump' "$PACKAGE/engine/satdump" version
if [[ $WEB_SERVER == nginx ]]; then [[ -x /usr/sbin/nginx ]] || fail 'nginx не установлен. Выберите builtin; системные пакеты установщик не меняет.'; fi
if (( START == 0 )); then
    for unit in "${UNITS[@]}"; do
        if svc is-active --quiet "$unit"; then fail '--no-start допустим только на остановленной станции'; fi
    done
fi
VERSION=$("$PYTHON" -c 'import json,sys;print(json.load(open(sys.argv[1]))["release_id"])' "$PACKAGE/PACKAGE-MANIFEST.json")
[[ $VERSION =~ ^[A-Za-z0-9._-]+$ ]] || fail 'Некорректный release_id'
TARGET="$PREFIX/releases/$VERSION"
if [[ ! -d $TARGET ]]; then
    STAGE=$(mktemp -d "$PREFIX/releases/.install.XXXXXXXX")
    ui_step 'Размещение новой версии' cp -a "$PACKAGE/." "$STAGE/"
    chown -R root:root "$STAGE"; chmod -R a+rX,go-w "$STAGE"
    mv "$STAGE" "$TARGET"; STAGE=''
else
    cmp "$PACKAGE/SHA256SUMS" "$TARGET/SHA256SUMS" || fail 'Такая версия уже установлена с иным содержимым'
    ui_step 'Проверка установленной копии' "$PYTHON" "$TARGET/scripts/station/pack.py" --verify "$TARGET"
fi
# Use the verified installed copy from here on. A downloaded package may live
# under a private /root or /home directory inaccessible to service accounts.
PYTHON="$TARGET/runtime/python"
BACKUP=$(mktemp -d "$PREFIX/.transaction.XXXXXXXX")
snapshot "$BACKUP"
MUTATED=1
for account in satdump-station satdump-web satdump-control; do
    getent group "$account" >/dev/null || groupadd --system "$account"
    id "$account" >/dev/null 2>&1 || useradd --system --gid "$account" --no-create-home --home-dir /nonexistent --shell /usr/sbin/nologin "$account"
done
getent group satdump-config >/dev/null || groupadd --system satdump-config
usermod -a -G satdump-config satdump-station
usermod -a -G satdump-config satdump-control
# Stop before mutating control/settings; no live root/API configuration race.
if (( START )); then
    for unit in "${UNITS[@]}"; do
        if svc is-active --quiet "$unit"; then
            svc stop "$unit" >>"$INSTALL_LOG" 2>&1 || fail "Не удалось остановить $unit"
        fi
    done
fi
install -d -m 0755 -o root -g root "$CONFIG" "$DATA"
ARGS=(--package "$TARGET" --config "$CONFIG" --data "$DATA" --source-kind "$SOURCE_KIND")
[[ -z $SOURCE_PATH ]] || ARGS+=(--source-path "$SOURCE_PATH")
ui_step 'Конфигурация, источник и защищённый API' "$PYTHON" "$TARGET/scripts/station/configure.py" provision "${ARGS[@]}"
chown root:satdump-config "$CONFIG/station.json" "$CONFIG/processing.json" "$CONFIG/control-policy.json"
chmod 0640 "$CONFIG/station.json" "$CONFIG/processing.json" "$CONFIG/control-policy.json"
chown root:satdump-control "$CONFIG/control.token"; chmod 0640 "$CONFIG/control.token"
for folder in state work logs archive inbox inbox/images inbox/products inbox/meteor_iq; do
    install -d -m 0750 -o satdump-station -g satdump-station "$DATA/$folder"
done
install -d -m 0755 -o satdump-station -g satdump-station "$DATA/public" "$DATA/public/items"
chown -R satdump-control:satdump-station "$DATA/control"
find "$DATA/control" -type d -exec chmod 2750 '{}' +
find "$DATA/control" -type f -exec chmod 0640 '{}' +
printf 'SATDUMP_HOST=%s\nSATDUMP_PORT=%s\nSATDUMP_CONTROL_PORT=%s\nSATDUMP_BACKEND_PORT=%s\nSATDUMP_WEB_SERVER=%s\n' "$HOST" "$PORT" "$CONTROL_PORT" "$BACKEND_PORT" "$WEB_SERVER" > "$CONFIG/web.env"
chmod 0644 "$CONFIG/web.env"
ui_step 'Подготовка systemd и WEB' "$PYTHON" "$TARGET/scripts/station/configure.py" units \
    --prefix "$PREFIX" --config "$CONFIG" --data "$DATA" --host "$HOST" --port "$PORT" \
    --control-port "$CONTROL_PORT" --backend-port "$BACKEND_PORT" --web-server "$WEB_SERVER" --output "$BACKUP/new-units"
install -m 0644 "$BACKUP/new-units/"*.service /etc/systemd/system/
if [[ $WEB_SERVER == nginx ]]; then
    install -m 0644 "$BACKUP/new-units/nginx.conf" "$CONFIG/nginx.conf"
    install -d -m 0750 -o satdump-web -g satdump-web /run/satdump-web
    # nginx -t creates its PID and temporary files: use the service identity,
    # otherwise a root-owned PID prevents the first unprivileged start.
    ui_step 'Проверка собственной конфигурации nginx' runuser -u satdump-web -- /usr/sbin/nginx -t -c "$CONFIG/nginx.conf"
else
    svc disable satdump-board.service >>"$INSTALL_LOG" 2>&1 || true
    rm -f /etc/systemd/system/satdump-board.service
fi
ui_step 'Права сервисов и окружение' runuser -u satdump-station -- "$PYTHON" -c 'import sqlite3;from PIL import Image'
OLD=$(cat "$BACKUP/current")
ln -sfn "$TARGET" "$PREFIX/.next"; mv -Tf "$PREFIX/.next" "$PREFIX/current"
if [[ -n $OLD && $OLD != "$TARGET" ]]; then ln -sfn "$OLD" "$PREFIX/previous"; fi
ln -sfn "$PREFIX/current/station.sh" /usr/local/bin/satdump-station
svc daemon-reload
ACTIVE=(satdump-worker.service satdump-control.service satdump-web.service)
[[ $WEB_SERVER != nginx ]] || ACTIVE+=(satdump-board.service)
if (( START )); then
    ui_step 'Автозапуск WEB, обработчика и API' svc enable "${ACTIVE[@]}"
    ui_step 'Запуск служб' svc restart "${ACTIVE[@]}"
    ui_step 'Проверка WEB → BOARD → обработчик и авторизации API' "$PYTHON" "$TARGET/scripts/station/healthcheck.py" \
        --host "$HOST" --port "$PORT" --control-port "$CONTROL_PORT" --token-file "$CONFIG/control.token"
fi
# Commit only after health succeeds. Keep the previous configuration snapshot.
rm -rf -- "$PREFIX/rollback-state"
mv "$BACKUP" "$PREFIX/rollback-state"; BACKUP=''
COMMITTED=1
ui_heading 'Установка завершена'
ui_info "Версия: $VERSION; журнал: $INSTALL_LOG"
ui_info "BOARD: http://${HOST/0.0.0.0/127.0.0.1}:$PORT/api/v1/board"
ui_info "Настройки API: http://127.0.0.1:$CONTROL_PORT/api/v1/control/config"
ui_info "Токен хранится в $CONFIG/control.token и не выводится в журнал."
ui_info 'Диагностика: sudo satdump-station doctor; журналы: sudo satdump-station logs'
if (( ! START )); then ui_info 'Службы не запущены (--no-start). Работоспособность не подтверждена.'; fi
