#!/usr/bin/env bash

# One-command native or portable build for Astra Linux.

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_MODE="native"
PROFILE=""
PROFILE_SET=0
INSTALL_DEPS=1
APT_UPDATE=1
DO_INSTALL=1
BUILD_ARGS=()

usage() {
    cat <<'EOF'
Использование: ./build.sh [параметры]

Без параметров сценарий сам устанавливает недостающие зависимости, собирает
native desktop-версию и устанавливает её в ~/.local/opt/satdump-1.2.2.

Параметры:
  --mode MODE        native (по умолчанию) или portable-glibc224
                     Алиас astra16-offline также включает portable-glibc224.
  --profile PROFILE  native: headless, desktop, full (по умолчанию desktop)
                     portable: reference, meteor (по умолчанию reference)
  --skip-deps        Не устанавливать системные зависимости
  --no-apt-update    Не обновлять индекс пакетов перед установкой зависимостей
  --no-install       Только собрать, не устанавливать
  -h, --help         Показать эту справку

Остальные параметры передаются в выбранный сценарий сборки. Например:
  ./build.sh --profile headless --clean
  ./build.sh --skip-deps --jobs 4
  ./build.sh --profile desktop --sdr rtl --clean
  ./build.sh --mode portable-glibc224 --profile reference
  ./build.sh --mode astra16-offline
EOF
}

die() {
    printf 'Ошибка: %s\n' "$*" >&2
    exit 2
}

while (( $# > 0 )); do
    case "$1" in
        --mode)
            (( $# >= 2 )) || die "после --mode требуется значение"
            BUILD_MODE="$2"
            shift 2
            ;;
        --mode=*)
            BUILD_MODE="${1#*=}"
            shift
            ;;
        --profile)
            (( $# >= 2 )) || die "после --profile требуется значение"
            PROFILE="$2"
            PROFILE_SET=1
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
        *)
            BUILD_ARGS+=("$1")
            shift
            ;;
    esac
done

case "${BUILD_MODE}" in
    native)
        [[ "${PROFILE_SET}" == "1" ]] || PROFILE="desktop"
        case "${PROFILE}" in
            headless|desktop|full) ;;
            *) die "для native допустимы профили headless, desktop, full: ${PROFILE}" ;;
        esac
        ;;
    portable|portable-glibc224|astra-reference|astra16-offline)
        BUILD_MODE="portable-glibc224"
        [[ "${PROFILE_SET}" == "1" ]] || PROFILE="reference"
        case "${PROFILE}" in
            reference|meteor) ;;
            *) die "для portable-glibc224 допустимы профили reference, meteor: ${PROFILE}" ;;
        esac
        ;;
    *) die "неизвестный режим: ${BUILD_MODE}" ;;
esac

if [[ "${BUILD_MODE}" == "portable-glibc224" ]]; then
    printf '\n==> Переносимый CLI-бандл Astra Linux 1.6 (glibc 2.24, %s)\n' "${PROFILE}"
    exec bash "${ROOT_DIR}/scripts/astra/build.sh" \
        --mode portable-glibc224 \
        --profile "${PROFILE}" \
        "${BUILD_ARGS[@]}"
fi

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
