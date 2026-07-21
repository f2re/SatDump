#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

BUILD_DIR=""
PREFIX="${SATDUMP_INSTALL_PREFIX:-${HOME}/.local/opt/satdump-1.2.2}"
PREFIX_EXPLICIT=0
USE_GUI=0
PREPARE=0
PROGRAM_ARGS=()

usage() {
    cat <<EOF
Использование: bash scripts/astra/run.sh [параметры] -- [аргументы SatDump]

Параметры:
  --build-dir PATH  Каталог сборки, созданный scripts/astra/build.sh
  --prefix PATH     Установочный префикс (по умолчанию: ${PREFIX})
  --ui              Запустить графический интерфейс satdump-ui
  --prepare         Выполнить cmake --install в пользовательский префикс
  -h, --help        Показать справку launcher

Примеры:
  bash scripts/astra/run.sh -- version
  bash scripts/astra/run.sh --ui
  bash scripts/astra/run.sh -- meteor_m2x_lrpt baseband input.cs16 output --samplerate 240000
  bash scripts/astra/run.sh --build-dir build/astra-1.7-headless --prepare -- version
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --prefix) PREFIX="$2"; PREFIX_EXPLICIT=1; shift 2 ;;
        --ui) USE_GUI=1; shift ;;
        --prepare) PREPARE=1; shift ;;
        --) shift; PROGRAM_ARGS+=("$@"); break ;;
        -h|--help) usage; exit 0 ;;
        *) PROGRAM_ARGS+=("$1"); shift ;;
    esac
done

if [[ -z "${BUILD_DIR}" ]]; then
    ASTRA_VERSION="$(detect_astra_version)"
    for candidate in \
        "${SATDUMP_ROOT}/build/astra-${ASTRA_VERSION}-desktop" \
        "${SATDUMP_ROOT}/build/astra-${ASTRA_VERSION}-headless" \
        "${SATDUMP_ROOT}/build/astra-${ASTRA_VERSION}-full"; do
        if [[ -d "${candidate}" ]]; then
            BUILD_DIR="${candidate}"
            break
        fi
    done
fi

if [[ -n "${BUILD_DIR}" && -r "${BUILD_DIR}/astra-env.sh" ]]; then
    SAVED_PREFIX="${PREFIX}"
    # shellcheck disable=SC1090
    source "${BUILD_DIR}/astra-env.sh"
    if (( PREFIX_EXPLICIT == 1 )); then
        PREFIX="${SAVED_PREFIX}"
        export SATDUMP_INSTALL_PREFIX="${PREFIX}"
    else
        PREFIX="${SATDUMP_INSTALL_PREFIX:-${PREFIX}}"
    fi
fi

if [[ "${PREPARE}" == "1" ]]; then
    [[ -n "${BUILD_DIR}" && -d "${BUILD_DIR}" ]] || die "Для --prepare укажите существующий --build-dir."
    CMAKE_EXECUTABLE="$(find_cmake 3.18.0 2>/dev/null || true)"
    [[ -n "${CMAKE_EXECUTABLE}" ]] || die "CMake 3.18+ не найден."

    if [[ "${PREFIX}" == "${HOME}"/* ]]; then
        mkdir -p "${PREFIX}"
    fi

    log_info "Установка дерева сборки в ${PREFIX}"
    if [[ -d "${PREFIX}" && -w "${PREFIX}" ]]; then
        "${CMAKE_EXECUTABLE}" --install "${BUILD_DIR}"
    else
        die "Префикс ${PREFIX} недоступен для записи. Выберите пользовательский --prefix либо установите через администратора."
    fi
fi

PROGRAM="satdump"
[[ "${USE_GUI}" == "1" ]] && PROGRAM="satdump-ui"
EXECUTABLE="${PREFIX}/bin/${PROGRAM}"

if [[ ! -x "${EXECUTABLE}" ]]; then
    die "${EXECUTABLE} не найден. Сначала выполните bash scripts/astra/build.sh --install или используйте --prepare."
fi

RESOURCES="${PREFIX}/share/satdump/resources"
PIPELINES="${PREFIX}/share/satdump/pipelines"
CONFIG="${PREFIX}/share/satdump/satdump_cfg.json"
[[ -d "${RESOURCES}" ]] || die "Не найден каталог ресурсов: ${RESOURCES}"
[[ -d "${PIPELINES}" ]] || die "Не найден каталог конвейеров: ${PIPELINES}"
[[ -f "${CONFIG}" ]] || die "Не найден основной конфигурационный файл: ${CONFIG}"

export LD_LIBRARY_PATH="${PREFIX}/lib:${PREFIX}/lib64:${ASTRA_DEPS_PREFIX}/lib:${ASTRA_DEPS_PREFIX}/lib64${BUILD_DIR:+:${BUILD_DIR}:${BUILD_DIR}/plugins}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

log_info "Запуск ${EXECUTABLE}"
exec "${EXECUTABLE}" "${PROGRAM_ARGS[@]}"
