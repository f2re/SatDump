#!/usr/bin/env bash

# Top-level script to build SatDump and create an offline distribution bundle in one step.

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SATDUMP_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [[ -f "${SCRIPT_DIR}/astra/common.sh" ]]; then
    source "${SCRIPT_DIR}/astra/common.sh"
else
    log_info() { printf 'ℹ %s\n' "$*"; }
    log_ok() { printf '✔ %s\n' "$*"; }
    log_warn() { printf '⚠ %s\n' "$*" >&2; }
    log_error() { printf '✖ %s\n' "$*" >&2; }
    die() { log_error "$*"; exit 1; }
fi

PROFILE="desktop"
CLEAN=0
OUTPUT_DIR="${SATDUMP_ROOT}/dist"
SDR_PROFILE="auto"
BUILD_DIR=""
JOBS="$(jobs_count 2>/dev/null || printf '2')"

usage() {
    cat <<EOF
Использование: bash scripts/build-and-bundle.sh [параметры]

Интегрированный скрипт сборки SatDump и создания автономного офлайн-бандла.

Параметры:
  --profile PROFILE   Профиль сборки (headless, desktop, full; по умолчанию: desktop)
  --sdr SDR_PROFILE   Набор SDR-плагинов (none, rtl, common, all; по умолчанию: auto)
  --clean             Удалить каталог сборки перед началом конфигурации
  --output PATH       Каталог назначения для готового архива (по умолчанию: ${SATDUMP_ROOT}/dist)
  --build-dir PATH    Пользовательский каталог сборки
  --jobs N            Количество параллельных задач сборки (по умолчанию: ${JOBS})
  -h, --help          Показать эту справку

Примеры:
  bash scripts/build-and-bundle.sh --profile desktop --sdr rtl
  bash scripts/build-and-bundle.sh --profile headless --clean --output /tmp/dist
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --profile) PROFILE="$2"; shift 2 ;;
        --sdr) SDR_PROFILE="$2"; shift 2 ;;
        --clean) CLEAN=1; shift ;;
        --output|--output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "Неизвестный параметр: $1" ;;
    esac
done

log_info "Старт интегрального процесса сборки и создания оффлайн-бандла SatDump"
log_info "Профиль: ${PROFILE}; SDR: ${SDR_PROFILE}; Очистка: ${CLEAN}"

BUILD_NATIVE_SCRIPT="${SATDUMP_ROOT}/scripts/astra/build-native.sh"
BUNDLE_SCRIPT="${SATDUMP_ROOT}/scripts/astra/create-offline-bundle.sh"

[[ -f "${BUILD_NATIVE_SCRIPT}" ]] || die "Скрипт сборки не найден: ${BUILD_NATIVE_SCRIPT}"
[[ -f "${BUNDLE_SCRIPT}" ]] || die "Скрипт создания бандла не найден: ${BUNDLE_SCRIPT}"

# 1. Запуск сборки SatDump
BUILD_ARGS=(
    --profile "${PROFILE}"
    --sdr "${SDR_PROFILE}"
    --jobs "${JOBS}"
    --without-tests
)

if [[ "${CLEAN}" -eq 1 ]]; then
    BUILD_ARGS+=(--clean)
fi

if [[ -n "${BUILD_DIR}" ]]; then
    BUILD_ARGS+=(--build-dir "${BUILD_DIR}")
fi

log_info "=== Шаг 1: Сборка SatDump (build-native.sh) ==="
bash "${BUILD_NATIVE_SCRIPT}" "${BUILD_ARGS[@]}"

# 2. Определение каталога сборки, если не был передан явно
if [[ -z "${BUILD_DIR}" ]]; then
    ASTRA_VER="$(detect_astra_version 2>/dev/null || printf 'unknown')"
    BUILD_DIR="${SATDUMP_ROOT}/build/astra-${ASTRA_VER}-${PROFILE}"
fi

# 3. Запуск создания оффлайн-бандла
BUNDLE_ARGS=(
    --build-dir "${BUILD_DIR}"
    --source-dir "${SATDUMP_ROOT}"
    --output-dir "${OUTPUT_DIR}"
    --profile "${PROFILE}"
)

log_info "=== Шаг 2: Создание офлайн-бандла (create-offline-bundle.sh) ==="
bash "${BUNDLE_SCRIPT}" "${BUNDLE_ARGS[@]}"

log_ok "Процесс сборки и упаковки SatDump успешно завершён!"
log_ok "Архив и мафифест сохранены в: ${OUTPUT_DIR}"
