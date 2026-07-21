#!/usr/bin/env bash

set -Eeuo pipefail

SOURCE_DIR="${SATDUMP_SOURCE:-/build/source}"
WORK_DIR="${SATDUMP_WORK:-/build/work}"
OUTPUT_DIR="${SATDUMP_OUTPUT:-/build/output}"
CACHE_DIR="${SATDUMP_CACHE:-/build/cache}"
OFFLINE_DIR="${SATDUMP_OFFLINE:-}"
JOBS="${SATDUMP_JOBS:-2}"
PLUGIN_PROFILE="${SATDUMP_PORTABLE_PLUGIN_PROFILE:-reference}"
CLEAN_WORK="${SATDUMP_CLEAN_WORK:-1}"

PORTABLE_DIR="${SOURCE_DIR}/scripts/astra/portable"
# shellcheck source=lock.env
source "${PORTABLE_DIR}/lock.env"

log() { printf '[portable] %s\n' "$*"; }
fail() { printf '[portable] ERROR: %s\n' "$*" >&2; exit 1; }

[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || fail "SATDUMP_JOBS должен быть положительным целым числом."
[[ "${PLUGIN_PROFILE}" == "reference" || "${PLUGIN_PROFILE}" == "meteor" ]] \
    || fail "Поддерживаются plugin-профили reference и meteor."
[[ -f "${SOURCE_DIR}/CMakeLists.txt" ]] || fail "Исходное дерево SatDump не найдено: ${SOURCE_DIR}"
[[ -f "${PORTABLE_DIR}/make-bundle.sh" ]] || fail "Не найден make-bundle.sh"

if ! grep -Eq 'project\(SatDump VERSION "1\.2\.2"\)' "${SOURCE_DIR}/CMakeLists.txt"; then
    fail "Portable-профиль рассчитан только на дерево SatDump 1.2.2."
fi

# Защита от смешанного дерева 1.2.2/2.x, обнаруженного в старой ветке astra.
MIXED_TREE_MARKERS=(
    "src-core/products/image_product.cpp"
    "src-core/angelscript"
    "plugins/firstparty_support"
    "plugins/metopsg_support"
)
for marker in "${MIXED_TREE_MARKERS[@]}"; do
    [[ ! -e "${SOURCE_DIR}/${marker}" ]] || fail "Обнаружен маркер смешанного дерева 2.x: ${marker}"
done

[[ "$(dpkg --print-architecture)" == "amd64" ]] || fail "Внутренний rootfs должен иметь архитектуру amd64."
BUILD_GLIBC="$(ldd --version 2>&1 | head -n1 | grep -o '[0-9][0-9.]*$' || true)"
[[ "${BUILD_GLIBC}" == "${SATDUMP_PORTABLE_GLIBC_BASELINE}" ]] \
    || fail "Ожидалась glibc ${SATDUMP_PORTABLE_GLIBC_BASELINE}, обнаружена ${BUILD_GLIBC:-unknown}. Пересоздайте rootfs."

mkdir -p "${WORK_DIR}" "${OUTPUT_DIR}" "${CACHE_DIR}"
export DEBIAN_FRONTEND=noninteractive
export LC_ALL=C
export LANG=C

log "Установка зависимостей внутри изолированного Debian Stretch chroot"
apt-get update
apt-get install -y --no-install-recommends \
    build-essential flex bison git curl ca-certificates pkg-config \
    xz-utils tar gzip bzip2 patch file rsync binutils \
    libgmp-dev libmpfr-dev libmpc-dev libisl-dev \
    zlib1g-dev libfftw3-dev libpng-dev libtiff5-dev \
    libjemalloc-dev libcurl4-openssl-dev libvolk1-dev \
    libzstd-dev libhdf5-dev libsqlite3-dev

fetch_file() {
    local filename="$1"
    local url="$2"
    local target="${CACHE_DIR}/${filename}"

    if [[ -n "${OFFLINE_DIR}" && -f "${OFFLINE_DIR}/${filename}" ]]; then
        cp -f "${OFFLINE_DIR}/${filename}" "${target}"
    elif [[ ! -s "${target}" ]]; then
        log "Загрузка ${url}"
        curl --fail --location --retry 4 --retry-delay 2 \
            --output "${target}.part" "${url}"
        mv "${target}.part" "${target}"
    fi

    [[ -s "${target}" ]] || fail "Не получен файл ${filename}"
    printf '%s\n' "${target}"
}

verify_sha256() {
    local file="$1"
    local expected="$2"
    local actual
    actual="$(sha256sum "${file}" | awk '{print $1}')"
    [[ "${actual}" == "${expected}" ]] || fail "SHA-256 не совпал для ${file}: ${actual}"
}

install_cmake() {
    local prefix="/opt/cmake-${SATDUMP_PORTABLE_CMAKE_VERSION}"
    local actual=""
    if [[ -x "${prefix}/bin/cmake" ]]; then
        actual="$("${prefix}/bin/cmake" --version | awk 'NR == 1 { print $3 }')"
        if [[ "${actual}" == "${SATDUMP_PORTABLE_CMAKE_VERSION}" ]]; then
            log "CMake ${actual} уже подготовлен"
            return
        fi
        log "CMake ${actual:-unknown} не совпадает с lock.env; переустановка"
    fi

    local archive checksums expected
    archive="$(fetch_file "${SATDUMP_PORTABLE_CMAKE_ARCHIVE}" "${SATDUMP_PORTABLE_CMAKE_URL}")"
    checksums="$(fetch_file "${SATDUMP_PORTABLE_CMAKE_CHECKSUMS}" "${SATDUMP_PORTABLE_CMAKE_CHECKSUMS_URL}")"
    expected="$(awk -v name="${SATDUMP_PORTABLE_CMAKE_ARCHIVE}" '{ file=$2; sub(/^\*/, "", file); if (file == name) print $1 }' "${checksums}")"
    [[ "${expected}" =~ ^[0-9a-fA-F]{64}$ ]] \
        || fail "В официальном манифесте CMake не найден ${SATDUMP_PORTABLE_CMAKE_ARCHIVE}"
    verify_sha256 "${archive}" "${expected}"

    rm -rf "${prefix}"
    mkdir -p "${prefix}"
    tar -xzf "${archive}" --strip-components=1 -C "${prefix}"
    [[ -x "${prefix}/bin/cmake" ]] || fail "CMake не установлен в ${prefix}"
    actual="$("${prefix}/bin/cmake" --version | awk 'NR == 1 { print $3 }')"
    [[ "${actual}" == "${SATDUMP_PORTABLE_CMAKE_VERSION}" ]] \
        || fail "После установки получен CMake ${actual}, ожидался ${SATDUMP_PORTABLE_CMAKE_VERSION}."
}

build_gcc() {
    local prefix="/opt/gcc9"
    local actual=""
    if [[ -x "${prefix}/bin/g++" ]]; then
        actual="$("${prefix}/bin/g++" -dumpfullversion -dumpversion)"
        if [[ "${actual}" == "${SATDUMP_PORTABLE_GCC_VERSION}" ]]; then
            log "GCC ${actual} уже подготовлен"
            return
        fi
        log "GCC ${actual:-unknown} не совпадает с lock.env; пересборка"
    fi

    local archive source build
    archive="$(fetch_file "${SATDUMP_PORTABLE_GCC_ARCHIVE}" "${SATDUMP_PORTABLE_GCC_URL}")"
    verify_sha256 "${archive}" "${SATDUMP_PORTABLE_GCC_SHA256}"

    source="${WORK_DIR}/toolchain/gcc-${SATDUMP_PORTABLE_GCC_VERSION}"
    build="${WORK_DIR}/toolchain/gcc-build"
    rm -rf "${source}" "${build}" "${prefix}"
    mkdir -p "${WORK_DIR}/toolchain" "${build}"
    tar -xJf "${archive}" -C "${WORK_DIR}/toolchain"

    log "Сборка GCC ${SATDUMP_PORTABLE_GCC_VERSION} по проверенной конфигурации astra"
    (
        cd "${build}"
        "${source}/configure" \
            --prefix="${prefix}" \
            --enable-languages=c,c++ \
            --disable-multilib \
            --disable-bootstrap \
            --disable-libsanitizer \
            --disable-werror
        make -j"${JOBS}"
        make install
    )
    [[ -x "${prefix}/bin/g++" ]] || fail "GCC 9.5 не установлен."
    actual="$("${prefix}/bin/g++" -dumpfullversion -dumpversion)"
    [[ "${actual}" == "${SATDUMP_PORTABLE_GCC_VERSION}" ]] \
        || fail "После установки получен GCC ${actual}, ожидался ${SATDUMP_PORTABLE_GCC_VERSION}."
}

build_nng() {
    local prefix="/usr/local"
    local marker="${prefix}/share/satdump-portable/nng.lock"
    if [[ -f "${prefix}/include/nng/nng.h" ]] && \
       compgen -G "${prefix}/lib/libnng.so*" >/dev/null && \
       [[ -f "${marker}" ]] && \
       grep -Fxq "commit=${SATDUMP_PORTABLE_NNG_COMMIT}" "${marker}"; then
        log "NNG ${SATDUMP_PORTABLE_NNG_VERSION} уже подготовлен"
        return
    fi

    log "Очистка несовпадающей локальной NNG"
    rm -rf "${prefix}/include/nng"
    rm -f "${prefix}"/lib/libnng.so* "${prefix}/lib/pkgconfig/nng.pc"
    rm -f "${marker}"
    ldconfig

    local source="${WORK_DIR}/toolchain/nng-${SATDUMP_PORTABLE_NNG_VERSION}"
    local build="${WORK_DIR}/toolchain/nng-build"
    rm -rf "${source}" "${build}"
    mkdir -p "${build}"

    if [[ -n "${OFFLINE_DIR}" && -f "${OFFLINE_DIR}/${SATDUMP_PORTABLE_NNG_BUNDLE}" ]]; then
        git clone "${OFFLINE_DIR}/${SATDUMP_PORTABLE_NNG_BUNDLE}" "${source}"
    else
        git init "${source}"
        git -C "${source}" remote add origin "${SATDUMP_PORTABLE_NNG_REPOSITORY}"
        git -C "${source}" fetch --depth 1 origin "${SATDUMP_PORTABLE_NNG_COMMIT}"
    fi
    git -C "${source}" checkout --detach "${SATDUMP_PORTABLE_NNG_COMMIT}"
    [[ "$(git -C "${source}" rev-parse HEAD)" == "${SATDUMP_PORTABLE_NNG_COMMIT}" ]] \
        || fail "NNG checkout не совпал с lock.env"

    log "Сборка NNG ${SATDUMP_PORTABLE_NNG_VERSION} (${SATDUMP_PORTABLE_NNG_COMMIT})"
    /opt/cmake-${SATDUMP_PORTABLE_CMAKE_VERSION}/bin/cmake \
        -S "${source}" -B "${build}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${prefix}" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_SHARED_LIBS=ON \
        -DNNG_TESTS=OFF \
        -DNNG_TOOLS=OFF \
        -DNNG_ENABLE_TLS=OFF
    /opt/cmake-${SATDUMP_PORTABLE_CMAKE_VERSION}/bin/cmake --build "${build}" --parallel "${JOBS}"
    /opt/cmake-${SATDUMP_PORTABLE_CMAKE_VERSION}/bin/cmake --install "${build}"
    ldconfig
    [[ -f "${prefix}/include/nng/nng.h" ]] || fail "NNG не установлен."
    compgen -G "${prefix}/lib/libnng.so*" >/dev/null || fail "Библиотека NNG не установлена."

    mkdir -p "$(dirname "${marker}")"
    cat > "${marker}" <<EOF
version=${SATDUMP_PORTABLE_NNG_VERSION}
commit=${SATDUMP_PORTABLE_NNG_COMMIT}
profile_version=${SATDUMP_PORTABLE_PROFILE_VERSION}
EOF
}

install_cmake
build_gcc
build_nng

export PATH="/opt/gcc9/bin:/opt/cmake-${SATDUMP_PORTABLE_CMAKE_VERSION}/bin:${PATH}"
export LD_LIBRARY_PATH="/opt/gcc9/lib64:/usr/local/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export CMAKE_PREFIX_PATH="/usr/local"

cat > /etc/satdump-portable-toolchain <<EOF
profile_version=${SATDUMP_PORTABLE_PROFILE_VERSION}
glibc=${BUILD_GLIBC}
gcc=$(/opt/gcc9/bin/g++ -dumpfullversion -dumpversion)
cmake=$(cmake --version | awk 'NR == 1 { print $3 }')
nng_version=${SATDUMP_PORTABLE_NNG_VERSION}
nng_commit=${SATDUMP_PORTABLE_NNG_COMMIT}
EOF

BUILD_DIR="${WORK_DIR}/satdump-build-${PLUGIN_PROFILE}"
STAGE_ROOT="${WORK_DIR}/stage-${PLUGIN_PROFILE}"
TEST_OUTPUT="${WORK_DIR}/presentation-test-output-${PLUGIN_PROFILE}"

if [[ "${CLEAN_WORK}" == "1" ]]; then
    rm -rf "${BUILD_DIR}" "${STAGE_ROOT}" "${TEST_OUTPUT}"
fi
mkdir -p "${BUILD_DIR}" "${STAGE_ROOT}" "${TEST_OUTPUT}"

CMAKE_PROFILE_ARGS=()
if [[ "${PLUGIN_PROFILE}" == "reference" ]]; then
    # Повторяет рабочую сборку astra: все плагины 1.2.2, которые могут быть
    # собраны из доступных зависимостей Stretch. GUI и локальные SDR отключаются
    # автоматически при отсутствии библиотек.
    CMAKE_PROFILE_ARGS+=( -DPLUGINS_ALL=ON )
else
    CMAKE_PROFILE_ARGS+=(
        -DPLUGINS_ALL=OFF
        -DPLUGIN_METEOR=ON
        -DPLUGIN_NOAA_METOP=ON
        -DPLUGIN_ANALOG=ON
        -DPLUGIN_STANDARD_CPP_COMPOS=ON
        -DPLUGIN_SIMD_SSE41=OFF
        -DPLUGIN_SIMD_AVX2=OFF
        -DPLUGIN_SIMD_NEON=OFF
        -DPLUGIN_AIRSPY_SDR_SUPPORT=OFF
        -DPLUGIN_AIRSPYHF_SDR_SUPPORT=OFF
        -DPLUGIN_HACKRF_SDR_SUPPORT=OFF
        -DPLUGIN_LIMESDR_SDR_SUPPORT=OFF
        -DPLUGIN_SDRPLAY_SDR_SUPPORT=OFF
        -DPLUGIN_PLUTOSDR_SDR_SUPPORT=OFF
        -DPLUGIN_BLADERF_SDR_SUPPORT=OFF
        -DPLUGIN_USRP_SDR_SUPPORT=OFF
        -DPLUGIN_RTLSDR_SDR_SUPPORT=OFF
        -DPLUGIN_MIRISDR_SDR_SUPPORT=OFF
        -DPLUGIN_RFNM_SDR_SUPPORT=OFF
        -DPLUGIN_PORTAUDIO_SINK=OFF
    )
fi

log "Конфигурация SatDump 1.2.2 Presentation: plugin-profile=${PLUGIN_PROFILE}"
cmake -S "${SOURCE_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${SATDUMP_PORTABLE_INSTALL_PREFIX}" \
    -DCMAKE_PREFIX_PATH=/usr/local \
    -DCMAKE_C_COMPILER=/opt/gcc9/bin/gcc \
    -DCMAKE_CXX_COMPILER=/opt/gcc9/bin/g++ \
    -DCMAKE_C_FLAGS="-include stdint.h" \
    -DCMAKE_CXX_FLAGS="-include cstdint" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBUILD_GUI=OFF \
    -DBUILD_TOOLS=OFF \
    -DBUILD_TESTING=ON \
    -DBUILD_DOCS=OFF \
    -DBUILD_ZIQ=OFF \
    -DBUILD_ZIQ2=OFF \
    -DBUILD_OPENCL=OFF \
    -DBUILD_OPENMP=ON \
    -DENABLE_INSTALL=ON \
    "${CMAKE_PROFILE_ARGS[@]}"

log "Сборка SatDump (${JOBS} потоков)"
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

if [[ -x "${BUILD_DIR}/satdump-presentation-test" ]]; then
    log "Smoke-тест двух плашек, легенд и ориентации"
    LD_LIBRARY_PATH="${BUILD_DIR}:${BUILD_DIR}/plugins:/usr/local/lib:/opt/gcc9/lib64" \
        "${BUILD_DIR}/satdump-presentation-test" \
        "${SOURCE_DIR}/resources/fonts/Roboto-Medium.ttf" \
        "${TEST_OUTPUT}"
fi

# DESTDIR гарантирует чистое staging-дерево и исключает примесь .so от другой
# версии SatDump — именно это было дефектом старого Astra-бандла.
rm -rf "${STAGE_ROOT}"
mkdir -p "${STAGE_ROOT}"
DESTDIR="${STAGE_ROOT}" cmake --install "${BUILD_DIR}"
STAGED_PREFIX="${STAGE_ROOT}${SATDUMP_PORTABLE_INSTALL_PREFIX}"
[[ -x "${STAGED_PREFIX}/bin/satdump" ]] || fail "В staging не установлен satdump."

SOURCE_DATE_EPOCH="$(git -C "${SOURCE_DIR}" show -s --format=%ct HEAD 2>/dev/null || date +%s)"
export SOURCE_DATE_EPOCH

bash "${PORTABLE_DIR}/make-bundle.sh" \
    --stage "${STAGED_PREFIX}" \
    --build-dir "${BUILD_DIR}" \
    --source "${SOURCE_DIR}" \
    --output "${OUTPUT_DIR}" \
    --profile "${PLUGIN_PROFILE}" \
    --gcc-runtime /opt/gcc9/lib64 \
    --glibc-max "${SATDUMP_PORTABLE_GLIBC_BASELINE}" \
    --glibc-warn "${SATDUMP_PORTABLE_GLIBC_WARN_ABOVE}"

log "Portable-сборка завершена. Артефакты: ${OUTPUT_DIR}"
