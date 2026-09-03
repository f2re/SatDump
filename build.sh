#!/usr/bin/env bash

# Simple one-command native build for Astra Linux.

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROFILE="desktop"
INSTALL_DEPS=1
APT_UPDATE=1
DO_INSTALL=1
BUILD_ARGS=()

usage() {
    cat <<'EOF'
Использование: ./build.sh [параметры]

Без параметров сценарий сам устанавливает недостающие зависимости, собирает
desktop-версию с GUI и RTL-SDR и устанавливает её в ~/.local/opt/satdump-1.2.2.

Параметры:
  --profile PROFILE  Профиль: headless, desktop или full (по умолчанию: desktop)
  --skip-deps        Не устанавливать системные зависимости
  --no-apt-update    Не обновлять индекс пакетов перед установкой зависимостей
  --no-install       Только собрать, не устанавливать
  -h, --help         Показать эту справку

Остальные параметры передаются штатному native-сценарию сборки. Например:
  ./build.sh --profile headless --clean
  ./build.sh --skip-deps --jobs 4
  ./build.sh --profile desktop --sdr rtl --clean
EOF
}

die() {
    printf 'Ошибка: %s\n' "$*" >&2
    exit 2
}

while (( $# > 0 )); do
    case "$1" in
        --profile)
            (( $# >= 2 )) || die "после --profile требуется значение"
            PROFILE="$2"
            shift 2
            ;;
        --skip-deps)
            INSTALL_DEPS=0
            shift
            ;;
        --no-apt-update)
            APT_UPDATE=0
            shift
            ;;
        --no-install)
            DO_INSTALL=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --mode|--mode=*)
            die "режим выбирается расширенным сценарием scripts/astra/build.sh"
            ;;
        *)
            BUILD_ARGS+=("$1")
            shift
            ;;
    esac
done

case "${PROFILE}" in
    headless|desktop|full) ;;
    *) die "неизвестный профиль: ${PROFILE}" ;;
esac

if (( INSTALL_DEPS == 1 )); then
    DEP_ARGS=(--profile "${PROFILE}" --bootstrap-missing)
    if (( APT_UPDATE == 0 )); then
        DEP_ARGS+=(--no-update)
    fi

    printf '\n==> Подготовка зависимостей (%s)\n' "${PROFILE}"
    bash "${ROOT_DIR}/scripts/astra/install-deps.sh" "${DEP_ARGS[@]}"
fi

NATIVE_ARGS=(--mode native --profile "${PROFILE}")
if (( DO_INSTALL == 1 )); then
    NATIVE_ARGS+=(--install)
fi
NATIVE_ARGS+=("${BUILD_ARGS[@]}")

printf '\n==> Сборка SatDump (%s)\n' "${PROFILE}"
bash "${ROOT_DIR}/scripts/astra/build.sh" "${NATIVE_ARGS[@]}"

printf '\nГотово.\n'
if (( DO_INSTALL == 1 )); then
    if [[ "${PROFILE}" == "headless" ]]; then
        printf 'Проверка: bash scripts/astra/run.sh -- version\n'
    else
        printf 'Запуск: bash scripts/astra/run.sh --ui\n'
    fi
else
    printf 'Результат находится в каталоге build/.\n'
fi
