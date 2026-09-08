#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export PATH="/usr/local/sbin:/usr/sbin:/sbin:${PATH}"
command="${1:-help}"
(( $# == 0 )) || shift
case "$command" in
    help|-h|--help)
        cat <<'EOF'
SatDump Station — Astra Linux 1.6, автономная обработка и сайт

Исходное дерево:
  ./station.sh build [--jobs N]        собрать движок glibc 2.24 и офлайн-пакет
  ./station.sh pack --engine DIR --runtime DIR --output DIR
  ./station.sh test                    проверить очередь, публикацию, HTTP

Готовый распакованный пакет:
  sudo ./install.sh                    установить и запустить две службы
  sudo ./install.sh --listen 0.0.0.0    открыть сайт для доверенной локальной сети
  sudo ./station.sh status             состояние очереди и последние ошибки
  sudo ./station.sh logs               журналы systemd
  sudo ./station.sh restart            перезапустить после изменения конфигурации
  sudo ./station.sh rollback           вернуть предыдущую версию приложения
  sudo ./station.sh retry --job ID     повторить неуспешное задание
  ./station.sh deploy ARCHIVE USER@HOST  передать и установить по SSH

Прямой запуск: worker, once, serve, check (параметры: --help).
Настройки: /etc/satdump-station/. Сайт: http://127.0.0.1:8090/.
Движок без сайта: прежняя команда ./build.sh в исходном дереве сохранена.
EOF
        ;;
    build) exec bash "$ROOT/scripts/station/build.sh" "$@" ;;
    pack) exec python3 "$ROOT/scripts/station/pack.py" "$@" ;;
    install) exec bash "$ROOT/scripts/station/install.sh" "$@" ;;
    deploy) exec bash "$ROOT/scripts/station/deploy.sh" "$@" ;;
    test)
        python=python3
        [[ ! -x "$ROOT/runtime/python" ]] || python="$ROOT/runtime/python"
        exec "$python" -m unittest discover -s "$ROOT/tests/station" -v ;;

    logs) exec journalctl -u satdump-worker.service -u satdump-web.service -n 100 -f ;;
    restart) exec systemctl restart satdump-worker.service satdump-web.service ;;
    rollback) exec bash "$ROOT/scripts/station/install.sh" --rollback "$@" ;;
    worker|once|serve|check|status|retry)
        if (( EUID == 0 )) && [[ "$command" == status || "$command" == retry ]] && id satdump-station >/dev/null 2>&1; then
            exec runuser -u satdump-station -- "$ROOT/station.sh" "$command" "$@"
        fi
        if [[ -x "$ROOT/runtime/python" ]]; then
            exec "$ROOT/runtime/python" "$ROOT/services/station/station.py" "$command" "$@"
        fi
        exec python3 "$ROOT/services/station/station.py" "$command" "$@"
        ;;
    *) printf 'Неизвестная команда: %s. Выполните ./station.sh help\n' "$command" >&2; exit 2 ;;
esac
