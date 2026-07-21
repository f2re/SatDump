#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

BUILD_DIR=""
OUTPUT=""

usage() {
    cat <<EOF
Использование: bash scripts/astra/collect-build-info.sh --build-dir PATH [--output FILE]

Параметры:
  --build-dir PATH  Каталог сконфигурированной/собранной версии SatDump
  --output FILE     Выходной файл (по умолчанию: PATH/astra-build-manifest.txt)
  -h, --help        Показать справку
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --output) OUTPUT="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "Неизвестный параметр: $1" ;;
    esac
done

[[ -n "${BUILD_DIR}" ]] || die "Укажите --build-dir."
[[ -d "${BUILD_DIR}" ]] || die "Каталог сборки не найден: ${BUILD_DIR}"
BUILD_DIR="$(readlink -f "${BUILD_DIR}")"
OUTPUT="${OUTPUT:-${BUILD_DIR}/astra-build-manifest.txt}"
ensure_directory "$(dirname "${OUTPUT}")"

selected_packages=(
    build-essential cmake gcc g++ make pkg-config
    libfftw3-dev libpng-dev libtiff5-dev libtiff-dev
    libjemalloc-dev libcurl4-openssl-dev libcurl4-gnutls-dev
    libnng-dev nng-dev libvolk2-dev libvolk1-dev libvolk-dev
    libglfw3-dev libgl1-mesa-dev portaudio19-dev
    librtlsdr-dev libhackrf-dev libairspy-dev libairspyhf-dev
    libzstd-dev libhdf5-dev ocl-icd-opencl-dev
)

{
    printf 'SatDump 1.2.2 Presentation — манифест сборки\n'
    printf '================================================\n\n'
    printf 'Сформирован UTC: %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf 'Корень исходников: %s\n' "${SATDUMP_ROOT}"
    printf 'Каталог сборки: %s\n' "${BUILD_DIR}"
    printf 'Astra profile: %s\n' "$(detect_astra_version)"
    printf 'Astra full version: %s\n' "$(astra_full_version)"
    printf 'Архитектура: %s\n' "$(uname -m)"
    printf 'Ядро: %s\n' "$(uname -srmo 2>/dev/null || uname -a)"

    printf '\n[Git]\n'
    if command_exists git && git -C "${SATDUMP_ROOT}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        printf 'Commit: %s\n' "$(git -C "${SATDUMP_ROOT}" rev-parse HEAD)"
        printf 'Branch: %s\n' "$(git -C "${SATDUMP_ROOT}" rev-parse --abbrev-ref HEAD)"
        if git -C "${SATDUMP_ROOT}" diff --quiet && git -C "${SATDUMP_ROOT}" diff --cached --quiet; then
            printf 'Working tree: clean\n'
        else
            printf 'Working tree: modified\n'
        fi
    else
        printf 'Git metadata: unavailable\n'
    fi

    printf '\n[OS release]\n'
    if [[ -r /etc/astra/build_version ]]; then
        printf '/etc/astra/build_version: %s\n' "$(tr -d '\r\n' < /etc/astra/build_version)"
    fi
    if [[ -r /etc/os-release ]]; then
        grep -E '^(ID|VERSION_ID|PRETTY_NAME)=' /etc/os-release || true
    fi
    if command_exists ldd; then
        ldd --version 2>&1 | head -n1 || true
    fi
    if command_exists pdp-id; then
        printf 'pdp-id: %s\n' "$(pdp-id 2>/dev/null || printf 'unavailable')"
    fi

    printf '\n[Toolchain]\n'
    for tool in "${CC:-cc}" "${CXX:-c++}" cmake make; do
        if command_exists "${tool}" || [[ -x "${tool}" ]]; then
            printf '\n$ %s --version\n' "${tool}"
            "${tool}" --version 2>&1 | head -n3 || true
        fi
    done

    printf '\n[pkg-config]\n'
    if command_exists pkg-config; then
        for module in fftw3f libcurl volk; do
            if pkg-config --exists "${module}" 2>/dev/null; then
                printf '%s=%s\n' "${module}" "$(pkg-config --modversion "${module}")"
            else
                printf '%s=not-found\n' "${module}"
            fi
        done
        printf 'PKG_CONFIG_PATH=%s\n' "${PKG_CONFIG_PATH:-}"
    fi

    printf '\n[Debian packages]\n'
    if command_exists dpkg-query; then
        for package in "${selected_packages[@]}"; do
            dpkg-query -W -f='${binary:Package}\t${Version}\n' "${package}" 2>/dev/null || true
        done | sort -u
    else
        printf 'dpkg-query unavailable\n'
    fi

    printf '\n[Local dependency prefix]\n'
    printf 'ASTRA_DEPS_PREFIX=%s\n' "${ASTRA_DEPS_PREFIX}"
    if [[ -d "${ASTRA_DEPS_PREFIX}" ]]; then
        find "${ASTRA_DEPS_PREFIX}" -maxdepth 3 -type f \
            \( -name '*.so*' -o -name '*.a' -o -name '*.pc' \) \
            -printf '%P\n' 2>/dev/null | sort || true
    fi

    printf '\n[CMake configuration]\n'
    if [[ -r "${BUILD_DIR}/CMakeCache.txt" ]]; then
        grep -E '^(CMAKE_(BUILD_TYPE|INSTALL_PREFIX|C_COMPILER|CXX_COMPILER)|BUILD_|PLUGIN_|SATDUMP_)' \
            "${BUILD_DIR}/CMakeCache.txt" | sort || true
    else
        printf 'CMakeCache.txt not found\n'
    fi

    printf '\n[Built executables]\n'
    for executable in \
        "${BUILD_DIR}/satdump" \
        "${BUILD_DIR}/satdump-ui" \
        "${BUILD_DIR}/satdump-presentation-test"; do
        if [[ -f "${executable}" ]]; then
            file "${executable}" 2>/dev/null || true
            sha256sum "${executable}" 2>/dev/null || true
            if command_exists ldd; then
                printf -- '-- ldd %s --\n' "${executable}"
                ldd "${executable}" 2>&1 || true
            fi
        fi
    done

    printf '\n[Presentation smoke-test artifacts]\n'
    if compgen -G "${BUILD_DIR}/presentation-test-output/*.png" >/dev/null; then
        sha256sum "${BUILD_DIR}"/presentation-test-output/*.png || true
    else
        printf 'No PNG artifacts found\n'
    fi
} > "${OUTPUT}"

log_ok "Манифест сборки сохранён: ${OUTPUT}"
