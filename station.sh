#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export PATH="/usr/local/sbin:/usr/sbin:/sbin:$PATH"
PYTHON=python3
[[ ! -x $ROOT/runtime/python ]] || PYTHON="$ROOT/runtime/python"
command=${1:-help}
(( $# == 0 )) || shift
case "$command" in
    help|-h|--help)
        cat <<'EOF'
SatDump Station / BOARD — Astra 1.6, автономная обработка и серверные API

Исходники:
  ./station.sh build --jobs 2          полный офлайн-пакет: движок + WEB + API
  ./station.sh pack --engine DIR --runtime DIR --output DIR --revision SHA
  ./station.sh test                    прежние и новые тесты станции

Установка и эксплуатация:
  sudo ./install.sh                    интерактивный мастер в терминале
  sudo ./install.sh --yes              установка без вопросов
  ./install.sh --help                  режимы WEB, пути и параметры мастера
  sudo ./station.sh ui-deploy DIR      подключить будущие готовые файлы интерфейса
  sudo ./station.sh doctor             read-only диагностика прав и конфигурации
  sudo ./station.sh status             последние задания
  sudo ./station.sh logs               журналы всех служб
  sudo ./station.sh restart            управляемый перезапуск
  sudo ./station.sh rollback           прежние код/настройки/службы, не данные
  sudo ./station.sh retry --job ID     повтор неуспешного задания
  ./station.sh deploy ARCHIVE USER@HOST -- --port 8090 --web-server builtin

WEB: 127.0.0.1:8090/api/v1/board — изображения/паспорта, без нового интерфейса.
API: 127.0.0.1:8091/api/v1/control/config — токен + ревизии If-Match.
Прямые команды: worker, once, serve, control, check; параметры: --help.
EOF
        ;;
    ui-deploy) exec bash "$ROOT/scripts/station/ui-deploy.sh" "$@" ;;
    build) exec bash "$ROOT/scripts/station/build.sh" "$@" ;;
    pack) exec "$PYTHON" "$ROOT/scripts/station/pack.py" "$@" ;;
    install|deploy) exec bash "$ROOT/scripts/station/$command.sh" "$@" ;;
    test) exec "$PYTHON" -m unittest discover -s "$ROOT/tests/station" -v ;;
    logs) exec journalctl -u satdump-worker.service -u satdump-web.service -u satdump-control.service -u satdump-board.service -n 100 -f ;;
    restart)
        units=(satdump-worker.service satdump-control.service satdump-web.service)
        [[ ! -f /etc/systemd/system/satdump-board.service ]] || units+=(satdump-board.service)
        exec systemctl restart "${units[@]}" ;;
    rollback) exec bash "$ROOT/scripts/station/install.sh" --rollback "$@" ;;
    doctor|status|retry)
        if (( EUID == 0 )) && id satdump-station >/dev/null 2>&1; then
            exec runuser -u satdump-station -- "$ROOT/station.sh" "$command" "$@"
        fi
        if [[ $command == doctor ]]; then exec "$PYTHON" "$ROOT/scripts/station/configure.py" doctor "$@"; fi
        exec "$PYTHON" "$ROOT/services/station/station.py" "$command" "$@" ;;
    worker|once|serve) exec "$PYTHON" "$ROOT/services/station/board.py" "$command" "$@" ;;
    control) exec "$PYTHON" "$ROOT/services/station/control.py" "$@" ;;
    check) exec "$PYTHON" "$ROOT/services/station/station.py" check "$@" ;;
    *) printf 'Неизвестная команда: %s. Выполните ./station.sh help\n' "$command" >&2; exit 2 ;;
esac
