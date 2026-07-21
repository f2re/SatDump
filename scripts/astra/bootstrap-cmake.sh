#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

CMAKE_VERSION="3.18.6"
CMAKE_ARCHIVE="cmake-${CMAKE_VERSION}.tar.gz"
CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/${CMAKE_ARCHIVE}"
CMAKE_SHA256="124f571ab70332da97a173cb794dfa09a5b20ccbb80a08e56570a500f47b6600"
PREFIX="${ASTRA_TOOLS_DIR}/cmake-${CMAKE_VERSION}"
SOURCE_ARCHIVE=""
JOBS="$(jobs_count)"
FORCE=0

usage() {
    cat <<EOF
Использование: $0 [параметры]

Параметры:
  --prefix PATH     Каталог установки (по умолчанию: ${PREFIX})
  --archive FILE    Использовать заранее загруженный архив (офлайн-режим)
  --jobs N          Число параллельных задач (по умолчанию: ${JOBS})
  --force           Пересобрать даже при наличии готового CMake
  -h, --help        Показать справку
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --prefix) PREFIX="$2"; shift 2 ;;
        --archive) SOURCE_ARCHIVE="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --force) FORCE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "Неизвестный параметр: $1" ;;
    esac
done

if [[ -x "${PREFIX}/bin/cmake" && "${FORCE}" == "0" ]]; then
    EXISTING_VERSION="$(cmake_version "${PREFIX}/bin/cmake")"
    if version_ge "${EXISTING_VERSION}" "${CMAKE_VERSION}"; then
        log_ok "CMake ${EXISTING_VERSION} уже установлен: ${PREFIX}/bin/cmake"
        printf '\nexport PATH="%s/bin:%s"\n' "${PREFIX}" "\$PATH"
        exit 0
    fi
fi

select_compiler
ensure_directory "${ASTRA_CACHE_DIR}"
ensure_directory "$(dirname "${PREFIX}")"

if [[ -n "${SOURCE_ARCHIVE}" ]]; then
    [[ -r "${SOURCE_ARCHIVE}" ]] || die "Архив недоступен: ${SOURCE_ARCHIVE}"
    ARCHIVE_PATH="$(readlink -f "${SOURCE_ARCHIVE}")"
else
    ARCHIVE_PATH="${ASTRA_CACHE_DIR}/${CMAKE_ARCHIVE}"
    if [[ ! -s "${ARCHIVE_PATH}" ]]; then
        log_info "Загрузка ${CMAKE_URL}"
        if command_exists curl; then
            curl --fail --location --retry 3 --output "${ARCHIVE_PATH}.part" "${CMAKE_URL}"
        elif command_exists wget; then
            wget --tries=3 --output-document="${ARCHIVE_PATH}.part" "${CMAKE_URL}"
        else
            die "Для загрузки требуется curl или wget. Либо передайте --archive FILE."
        fi
        mv "${ARCHIVE_PATH}.part" "${ARCHIVE_PATH}"
    fi
fi

ACTUAL_SHA256="$(sha256sum "${ARCHIVE_PATH}" | awk '{print $1}')"
if [[ "${ACTUAL_SHA256}" != "${CMAKE_SHA256}" ]]; then
    die "Контрольная сумма CMake не совпала. Ожидалось ${CMAKE_SHA256}, получено ${ACTUAL_SHA256}."
fi
log_ok "SHA-256 архива CMake подтверждён"

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/satdump-cmake-${CMAKE_VERSION}.XXXXXX")"
cleanup() {
    rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

tar -xzf "${ARCHIVE_PATH}" -C "${WORK_DIR}"
cd "${WORK_DIR}/cmake-${CMAKE_VERSION}"

log_info "Конфигурация CMake ${CMAKE_VERSION} → ${PREFIX}"
./bootstrap \
    --prefix="${PREFIX}" \
    --parallel="${JOBS}" \
    -- \
    -DCMAKE_USE_OPENSSL=OFF

log_info "Сборка CMake (${JOBS} потоков)"
make -j"${JOBS}"
make install

[[ -x "${PREFIX}/bin/cmake" ]] || die "Установка CMake завершилась без исполняемого файла."
log_ok "Установлен $("${PREFIX}/bin/cmake" --version | head -n1)"
cat <<EOF

Добавьте CMake в текущую сессию:
  export PATH="${PREFIX}/bin:\$PATH"

Или задайте его только для SatDump:
  export CMAKE_BIN="${PREFIX}/bin/cmake"
EOF
