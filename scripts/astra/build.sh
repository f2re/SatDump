#!/usr/bin/env bash

set -Eeuo pipefail

ASTRA_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_MODE="${SATDUMP_BUILD_MODE:-native}"
FORWARDED_ARGS=()

usage() {
    cat <<'EOF2'
Использование: bash scripts/astra/build.sh [--mode MODE] [параметры профиля]

Режимы:
  native                 Обычная сборка непосредственно на текущей Astra Linux.
                         Поддерживает headless, desktop, full и SDR-профили.

  portable-astra17       Release-профиль для Astra Linux 1.7 x86_64.
  astra17-full           Алиас portable-astra17.
                         Сборка ОБЯЗАТЕЛЬНО выполняется непосредственно в
                         фактической Astra Linux 1.7/glibc 2.28. Готовый архив
                         содержит полный runtime closure, включая glibc/loader,
                         libstdc++, GUI/audio/RTL-SDR зависимости.

  portable-glibc224      Legacy-профиль Debian Stretch/glibc 2.24.
                         Не используется для новых Astra 1.7 releases.

Примеры:
  bash scripts/astra/build.sh --mode native --profile desktop --sdr rtl --install
  bash scripts/astra/build.sh --mode portable-astra17 --profile desktop --prepare-build-env
  bash scripts/astra/build.sh --mode portable-astra17 --profile headless
  bash scripts/astra/build.sh --mode portable-glibc224 --profile reference

Для GitHub Release применяется portable-astra17/desktop внутри официальной
Astra Linux 1.7. Целевая Astra не устанавливает дополнительные runtime-пакеты.
EOF2
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
    portable-astra17|astra17-full|astra17)
        exec bash "${ASTRA_SCRIPT_DIR}/astra17/build.sh" "${FORWARDED_ARGS[@]}"
        ;;
    portable|portable-glibc224|astra-reference)
        exec bash "${ASTRA_SCRIPT_DIR}/portable/build.sh" "${FORWARDED_ARGS[@]}"
        ;;
    *)
        printf 'Неизвестный режим сборки: %s\n' "${BUILD_MODE}" >&2
        printf 'Допустимы native, portable-astra17 и portable-glibc224.\n' >&2
        exit 2
        ;;
esac
