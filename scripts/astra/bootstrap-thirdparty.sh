#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

COMPONENT="all"
PREFIX="${ASTRA_DEPS_PREFIX}"
ARCHIVE_DIR=""
JOBS="$(jobs_count)"
FORCE=0
TEMP_DIRS=()

NNG_VERSION="1.5.2"
NNG_ARCHIVE="nng-${NNG_VERSION}.tar.gz"
NNG_URL="https://github.com/nanomsg/nng/archive/refs/tags/v${NNG_VERSION}.tar.gz"
NNG_SHA256="f8b25ab86738864b1f2e3128e8badab581510fa8085ff5ca9bb980d317334c46"

VOLK_VERSION="2.5.2"
VOLK_ARCHIVE="volk-${VOLK_VERSION}.tar.gz"
VOLK_URL="https://www.libvolk.org/releases/${VOLK_ARCHIVE}"
# Для архивов этой версии upstream публикует MD5 и detached signature.
VOLK_MD5="7872b4dde4415dab100710e3583b0af9"

FFTW_VERSION="3.3.10"
FFTW_ARCHIVE="fftw-${FFTW_VERSION}.tar.gz"
FFTW_URL="http://www.fftw.org/${FFTW_ARCHIVE}"

CURL_VERSION="7.88.1"
CURL_ARCHIVE="curl-${CURL_VERSION}.tar.gz"
CURL_URL="https://curl.se/download/${CURL_ARCHIVE}"

TIFF_VERSION="4.5.1"
TIFF_ARCHIVE="tiff-${TIFF_VERSION}.tar.gz"
TIFF_URL="https://download.osgeo.org/libtiff/${TIFF_ARCHIVE}"

JEMALLOC_VERSION="5.3.0"
JEMALLOC_ARCHIVE="jemalloc-${JEMALLOC_VERSION}.tar.bz2"
JEMALLOC_URL="https://github.com/jemalloc/jemalloc/releases/download/${JEMALLOC_VERSION}/${JEMALLOC_ARCHIVE}"

cleanup() {
    local directory
    for directory in "${TEMP_DIRS[@]}"; do
        [[ -n "${directory}" ]] && rm -rf "${directory}"
    done
}
trap cleanup EXIT

usage() {
    cat <<EOF
Использование: bash scripts/astra/bootstrap-thirdparty.sh [параметры]

Параметры:
  --component all|nng|volk|fftw|curl|tiff|jemalloc  Что собирать (по умолчанию: all)
  --prefix PATH                                      Локальный префикс (по умолчанию: ${PREFIX})
  --archive-dir PATH                                 Каталог с офлайн-архивами
  --jobs N                                           Число параллельных задач
  --force                                            Пересобрать установленные компоненты
  -h, --help                                         Показать справку

Архивы для офлайн-сборки:
  ${NNG_ARCHIVE}
  ${VOLK_ARCHIVE}
  ${FFTW_ARCHIVE}
  ${CURL_ARCHIVE}
  ${TIFF_ARCHIVE}
  ${JEMALLOC_ARCHIVE}
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --component) COMPONENT="$2"; shift 2 ;;
        --prefix) PREFIX="$2"; shift 2 ;;
        --archive-dir) ARCHIVE_DIR="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --force) FORCE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "Неизвестный параметр: $1" ;;
    esac
done

case "${COMPONENT}" in
    all|nng|volk|fftw|curl|tiff|jemalloc) ;;
    *) die "Допустимые компоненты: all, nng, volk, fftw, curl, tiff, jemalloc" ;;
esac
[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || die "--jobs должен быть положительным целым числом."

select_compiler
CMAKE_EXECUTABLE="$(find_cmake 3.18.0 2>/dev/null || true)"
[[ -n "${CMAKE_EXECUTABLE}" ]] || die "Требуется CMake 3.18+. Сначала запустите bash scripts/astra/bootstrap-cmake.sh."

ensure_directory "${ASTRA_CACHE_DIR}"
ensure_directory "${PREFIX}"

export CMAKE_PREFIX_PATH="${PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
export PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig:${PREFIX}/lib64/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
export LD_LIBRARY_PATH="${PREFIX}/lib:${PREFIX}/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

fetch_archive() {
    local archive="$1"
    local url="$2"
    local output=""

    if [[ -n "${ARCHIVE_DIR}" ]]; then
        output="${ARCHIVE_DIR}/${archive}"
        [[ -r "${output}" ]] || die "Офлайн-архив не найден: ${output}"
        readlink -f "${output}"
        return
    fi

    output="${ASTRA_CACHE_DIR}/${archive}"
    if [[ ! -s "${output}" ]]; then
        log_info "Загрузка ${url}" >&2
        if command_exists curl; then
            curl --fail --location --retry 3 --output "${output}.part" "${url}"
        elif command_exists wget; then
            wget --tries=3 --output-document="${output}.part" "${url}"
        else
            die "Для загрузки требуется curl или wget."
        fi
        mv "${output}.part" "${output}"
    fi
    printf '%s\n' "${output}"
}

new_work_directory() {
    local template="$1"
    local directory
    directory="$(mktemp -d "${TMPDIR:-/tmp}/${template}.XXXXXX")"
    TEMP_DIRS+=("${directory}")
    printf '%s\n' "${directory}"
}

build_nng() {
    if [[ -f "${PREFIX}/include/nng/nng.h" && "${FORCE}" == "0" ]]; then
        log_ok "NNG уже установлен в ${PREFIX}"
        return
    fi

    local archive actual work source_directory build_directory
    archive="$(fetch_archive "${NNG_ARCHIVE}" "${NNG_URL}")"
    actual="$(sha256sum "${archive}" | awk '{print $1}')"
    [[ "${actual}" == "${NNG_SHA256}" ]] || die "SHA-256 NNG не совпал: ${actual}"
    log_ok "SHA-256 NNG подтверждён"

    work="$(new_work_directory satdump-nng)"
    tar -xzf "${archive}" -C "${work}"
    source_directory="$(find "${work}" -mindepth 1 -maxdepth 1 -type d | head -n1)"
    [[ -n "${source_directory}" ]] || die "В архиве NNG не найден каталог исходников."
    build_directory="${work}/build"

    log_info "Сборка NNG ${NNG_VERSION}"
    "${CMAKE_EXECUTABLE}" -S "${source_directory}" -B "${build_directory}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_SHARED_LIBS=ON \
        -DNNG_TESTS=OFF \
        -DNNG_TOOLS=OFF \
        -DNNG_ENABLE_TLS=OFF
    "${CMAKE_EXECUTABLE}" --build "${build_directory}" --parallel "${JOBS}"
    "${CMAKE_EXECUTABLE}" --install "${build_directory}"

    [[ -f "${PREFIX}/include/nng/nng.h" ]] || die "NNG не установлен."
    if ! compgen -G "${PREFIX}/lib/libnng.so*" >/dev/null && [[ ! -f "${PREFIX}/lib/libnng.a" ]]; then
        die "Библиотека NNG не найдена в ${PREFIX}/lib."
    fi
    log_ok "NNG ${NNG_VERSION} установлен в ${PREFIX}"
}

build_volk() {
    if pkg-config --exists volk 2>/dev/null && [[ "${FORCE}" == "0" ]]; then
        log_ok "VOLK уже доступен: $(pkg-config --modversion volk)"
        return
    fi

    command_exists python3 || die "Для сборки VOLK требуется Python 3."
    python3 -c 'import mako' >/dev/null 2>&1 \
        || die "Для сборки VOLK требуется модуль python3-mako."

    local archive actual work source_directory build_directory
    archive="$(fetch_archive "${VOLK_ARCHIVE}" "${VOLK_URL}")"
    actual="$(md5sum "${archive}" | awk '{print $1}')"
    [[ "${actual}" == "${VOLK_MD5}" ]] || die "MD5 официального архива VOLK не совпал: ${actual}"
    log_ok "Контрольная сумма официального архива VOLK подтверждена"

    work="$(new_work_directory satdump-volk)"
    tar -xzf "${archive}" -C "${work}"
    source_directory="$(find "${work}" -mindepth 1 -maxdepth 1 -type d | head -n1)"
    [[ -n "${source_directory}" ]] || die "В архиве VOLK не найден каталог исходников."
    build_directory="${work}/build"

    log_info "Сборка VOLK ${VOLK_VERSION}"
    "${CMAKE_EXECUTABLE}" -S "${source_directory}" -B "${build_directory}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DENABLE_TESTING=OFF
    "${CMAKE_EXECUTABLE}" --build "${build_directory}" --parallel "${JOBS}"
    "${CMAKE_EXECUTABLE}" --install "${build_directory}"

    export PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH}"
    pkg-config --exists volk || die "VOLK установлен, но volk.pc не найден в ${PKG_CONFIG_PATH}."
    log_ok "VOLK $(pkg-config --modversion volk) установлен в ${PREFIX}"
}

build_fftw() {
    if pkg-config --exists fftw3f 2>/dev/null && [[ "${FORCE}" == "0" ]]; then
        log_ok "FFTW3f уже доступен: $(pkg-config --modversion fftw3f)"
        return
    fi

    local archive work source_directory build_directory
    archive="$(fetch_archive "${FFTW_ARCHIVE}" "${FFTW_URL}")"

    work="$(new_work_directory satdump-fftw)"
    tar -xzf "${archive}" -C "${work}"
    source_directory="$(find "${work}" -mindepth 1 -maxdepth 1 -type d | head -n1)"
    [[ -n "${source_directory}" ]] || die "В архиве FFTW не найден каталог исходников."

    log_info "Сборка FFTW ${FFTW_VERSION} (single precision)"
    (
        cd "${source_directory}"
        ./configure --prefix="${PREFIX}" --enable-single --enable-shared --disable-fortran CC="${CC}"
        make -j"${JOBS}"
        make install
    )

    export PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH}"
    pkg-config --exists fftw3f || die "FFTW установлен, но fftw3f.pc не найден в ${PKG_CONFIG_PATH}."
    log_ok "FFTW3f $(pkg-config --modversion fftw3f) установлен в ${PREFIX}"
}

build_curl() {
    if pkg-config --exists libcurl 2>/dev/null && [[ "${FORCE}" == "0" ]]; then
        log_ok "libcurl уже доступен: $(pkg-config --modversion libcurl)"
        return
    fi

    local archive work source_directory build_directory
    archive="$(fetch_archive "${CURL_ARCHIVE}" "${CURL_URL}")"

    work="$(new_work_directory satdump-curl)"
    tar -xzf "${archive}" -C "${work}"
    source_directory="$(find "${work}" -mindepth 1 -maxdepth 1 -type d | head -n1)"
    [[ -n "${source_directory}" ]] || die "В архиве cURL не найден каталог исходников."
    build_directory="${work}/build"

    log_info "Сборка cURL ${CURL_VERSION}"
    "${CMAKE_EXECUTABLE}" -S "${source_directory}" -B "${build_directory}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_SHARED_LIBS=ON \
        -DBUILD_CURL_EXE=OFF \
        -DBUILD_TESTING=OFF \
        -DCURL_DISABLE_TESTS=ON
    "${CMAKE_EXECUTABLE}" --build "${build_directory}" --parallel "${JOBS}"
    "${CMAKE_EXECUTABLE}" --install "${build_directory}"

    export PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH}"
    pkg-config --exists libcurl || die "cURL установлен, но libcurl.pc не найден в ${PKG_CONFIG_PATH}."
    log_ok "libcurl $(pkg-config --modversion libcurl) установлен в ${PREFIX}"
}

build_tiff() {
    if [[ -f "${PREFIX}/include/tiffio.h" && "${FORCE}" == "0" ]]; then
        log_ok "libtiff уже доступен в ${PREFIX}"
        return
    fi

    local archive work source_directory build_directory
    archive="$(fetch_archive "${TIFF_ARCHIVE}" "${TIFF_URL}")"

    work="$(new_work_directory satdump-tiff)"
    tar -xzf "${archive}" -C "${work}"
    source_directory="$(find "${work}" -mindepth 1 -maxdepth 1 -type d | head -n1)"
    [[ -n "${source_directory}" ]] || die "В архиве TIFF не найден каталог исходников."
    build_directory="${work}/build"

    log_info "Сборка TIFF ${TIFF_VERSION}"
    "${CMAKE_EXECUTABLE}" -S "${source_directory}" -B "${build_directory}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
        -DCMAKE_INSTALL_LIBDIR=lib
    "${CMAKE_EXECUTABLE}" --build "${build_directory}" --parallel "${JOBS}"
    "${CMAKE_EXECUTABLE}" --install "${build_directory}"

    [[ -f "${PREFIX}/include/tiffio.h" ]] || die "libtiff установлен, но tiffio.h не найден."
    log_ok "libtiff установлен в ${PREFIX}"
}

build_jemalloc() {
    if pkg-config --exists jemalloc 2>/dev/null || [[ -f "${PREFIX}/include/jemalloc/jemalloc.h" ]] && [[ "${FORCE}" == "0" ]]; then
        log_ok "jemalloc уже доступен в ${PREFIX}"
        return
    fi

    local archive work source_directory
    archive="$(fetch_archive "${JEMALLOC_ARCHIVE}" "${JEMALLOC_URL}")"

    work="$(new_work_directory satdump-jemalloc)"
    tar -xjf "${archive}" -C "${work}"
    source_directory="$(find "${work}" -mindepth 1 -maxdepth 1 -type d | head -n1)"
    [[ -n "${source_directory}" ]] || die "В архиве jemalloc не найден каталог исходников."

    log_info "Сборка jemalloc ${JEMALLOC_VERSION}"
    (
        cd "${source_directory}"
        ./configure --prefix="${PREFIX}" CC="${CC}"
        make -j"${JOBS}"
        make install
    )

    [[ -f "${PREFIX}/include/jemalloc/jemalloc.h" ]] || die "jemalloc установлен, но jemalloc.h не найден."
    log_ok "jemalloc установлен в ${PREFIX}"
}

case "${COMPONENT}" in
    all) build_nng; build_volk; build_fftw; build_curl; build_tiff; build_jemalloc ;;
    nng) build_nng ;;
    volk) build_volk ;;
    fftw) build_fftw ;;
    curl) build_curl ;;
    tiff) build_tiff ;;
    jemalloc) build_jemalloc ;;
esac

cat <<EOF

Локальные зависимости готовы.
Для ручной сборки экспортируйте:
  export CMAKE_PREFIX_PATH="${PREFIX}:\${CMAKE_PREFIX_PATH:-}"
  export PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig:${PREFIX}/lib64/pkgconfig:\${PKG_CONFIG_PATH:-}"
  export LD_LIBRARY_PATH="${PREFIX}/lib:${PREFIX}/lib64:\${LD_LIBRARY_PATH:-}"
EOF
