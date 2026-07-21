#!/usr/bin/env bash

set -Eeuo pipefail

ASTRA_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_MODE="${SATDUMP_BUILD_MODE:-native}"
FORWARDED_ARGS=()

usage() {
    cat <<'EOF'
Использование: bash scripts/astra/build.sh [--mode MODE] [параметры профиля]

Режимы:
  native                 Сборка непосредственно на текущей Astra Linux.
                         Поддерживает headless, desktop, full и SDR-профили.

  portable-glibc224      Воспроизводимый CLI-бандл через изолированный Debian
                         Stretch chroot с контролируемой glibc, GCC, CMake и NNG.
                         Повторяет рабочую технологию ветки astra.

Примеры:
  bash scripts/astra/build.sh --mode native --profile headless
  bash scripts/astra/build.sh --mode native --profile desktop --sdr rtl --install
  bash scripts/astra/build.sh --mode portable-glibc224 --profile reference
  bash scripts/astra/build.sh --mode portable-glibc224 --profile meteor

Подробная справка:
  bash scripts/astra/build-native.sh --help
  bash scripts/astra/portable/build.sh --help

Без --mode используется native, поэтому существующие команды сохраняют поведение.
Переменная окружения SATDUMP_BUILD_MODE задаёт режим по умолчанию для автоматизации.
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --mode)
            [[ $# -ge 2 ]] || { printf 'После --mode требуется значение.\n' >&2; exit 2; }
            BUILD_MODE="$2"
            shift 2
            ;;
        --mode=*)
            BUILD_MODE="${1#*=}"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            FORWARDED_ARGS+=("$1")
            shift
            ;;
    esac
done

case "${BUILD_MODE}" in
    native)
        exec bash "${ASTRA_SCRIPT_DIR}/build-native.sh" "${FORWARDED_ARGS[@]}"
        ;;
    portable|portable-glibc224|astra-reference)
        exec bash "${ASTRA_SCRIPT_DIR}/portable/build.sh" "${FORWARDED_ARGS[@]}"
        ;;
    *)
        printf 'Неизвестный режим сборки: %s\n' "${BUILD_MODE}" >&2
        printf 'Допустимы native и portable-glibc224.\n' >&2
        exit 2
        ;;
esac
