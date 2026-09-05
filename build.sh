#!/usr/bin/env bash

# One-command native or portable build for Astra Linux.

set -Eeuo pipefail

# Astra user sessions commonly omit administrative directories even when the
# portable-build utilities are installed there.
export PATH="/usr/local/sbin:/usr/sbin:/sbin:${PATH}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_MODE="astra16-offline"
PROFILE=""
PROFILE_SET=0
INSTALL_DEPS=1
APT_UPDATE=1
DO_INSTALL=1
BUILD_ARGS=()

usage() {
    cat <<'EOF'
Использование: ./build.sh [параметры]

Без параметров сценарий сам устанавливает недостающие хостовые зависимости и
собирает полный переносимый CLI-бандл для Astra Linux 1.6. GUI не собирается.

Параметры:
  --mode MODE        astra16-offline (по умолчанию), portable-glibc224 или native
                     astra16-offline — полный CLI-бандл для Astra Linux 1.6.
  --profile PROFILE  native: headless, desktop, full (по умолчанию desktop)
                     portable: reference, meteor (по умолчанию reference)
  --skip-deps        Не устанавливать системные зависимости
  --no-apt-update    Не обновлять индекс пакетов перед установкой зависимостей
  --no-install       Только собрать, не устанавливать
  -h, --help         Показать эту справку

Остальные параметры передаются в выбранный сценарий сборки. Например:
  ./build.sh
  ./build.sh --skip-deps --jobs 4
  ./build.sh --profile meteor
  ./build.sh --mode native --profile headless --clean
  ./build.sh --mode native --profile desktop --sdr rtl --clean
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

install_portable_host_deps() {
    local -a missing=()
    local command
    for command in debootstrap fakeroot chroot mount umount findmnt rsync tar gzip sha256sum readlink dpkg-deb; do
        command -v "${command}" >/dev/null 2>&1 || missing+=("${command}")
    done

    if (( ${#missing[@]} == 0 )); then
        return 0
    fi
    if (( INSTALL_DEPS == 0 )); then
        die "не найдены зависимости portable-сборки: ${missing[*]} (уберите --skip-deps для автоматической установки)"
    fi
    command -v apt-get >/dev/null 2>&1 \
        || die "не найдены зависимости portable-сборки: ${missing[*]}; apt-get недоступен"

    local -a elevate=()
    if (( EUID != 0 )); then
        command -v sudo >/dev/null 2>&1 || die "для установки зависимостей требуется root или sudo"
        elevate=(sudo)
    fi
    if (( APT_UPDATE == 1 )); then
        printf '\n==> Обновление индекса пакетов для portable-сборки\n'
        "${elevate[@]}" apt-get update
    fi
    printf '\n==> Установка зависимостей portable-сборки\n'
    "${elevate[@]}" apt-get install -y --no-install-recommends \
        debootstrap fakeroot rsync xz-utils binutils curl ca-certificates

    missing=()
    for command in debootstrap fakeroot chroot mount umount findmnt rsync tar gzip sha256sum readlink dpkg-deb; do
        command -v "${command}" >/dev/null 2>&1 || missing+=("${command}")
    done
    (( ${#missing[@]} == 0 )) \
        || die "после установки всё ещё не найдены зависимости: ${missing[*]}"
}

if [[ "${BUILD_MODE}" == "portable-glibc224" ]]; then
    install_portable_host_deps
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
