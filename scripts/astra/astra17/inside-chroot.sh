#!/usr/bin/env bash
set -Eeuo pipefail

SOURCE_DIR="${SATDUMP_SOURCE:-/build/source}"
WORK_DIR="${SATDUMP_WORK:-/build/work}"
OUTPUT_DIR="${SATDUMP_OUTPUT:-/build/output}"
PROFILE="${SATDUMP_PROFILE:-desktop}"
JOBS="${SATDUMP_JOBS:-2}"
KEEP_WORK="${SATDUMP_KEEP_WORK:-0}"

log() { printf '[astra17/chroot] %s\n' "$*" >&2; }
fail() { printf '[astra17/chroot] ERROR: %s\n' "$*" >&2; exit 1; }

[[ "${PROFILE}" == "headless" || "${PROFILE}" == "desktop" ]] || fail "Некорректный профиль: ${PROFILE}"
[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || fail "Некорректное SATDUMP_JOBS"
[[ -f "${SOURCE_DIR}/CMakeLists.txt" ]] || fail "Нет исходного дерева SatDump"

glibc="$(ldd --version 2>&1 | head -n1 | grep -o '[0-9][0-9.]*$' || true)"
[[ "${glibc}" == "2.28" ]] || fail "Сборка должна идти на glibc 2.28, обнаружена ${glibc:-unknown}"

export DEBIAN_FRONTEND=noninteractive
export LC_ALL=C
export LANG=C

log "Установка build-зависимостей Debian 10"
apt-get update
apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates curl pkg-config \
    binutils file patch rsync patchelf xz-utils tar gzip bzip2 \
    zlib1g-dev libfftw3-dev libpng-dev libtiff5-dev libjemalloc-dev \
    libcurl4-openssl-dev libvolk1-dev libzstd-dev libhdf5-dev libsqlite3-dev \
    libarmadillo-dev libglfw3-dev libgl1-mesa-dev portaudio19-dev librtlsdr-dev

# NNG отсутствует в базовом Buster в подходящем виде. Фиксируем ровно ту же
# версию/commit, которую уже использует portable-профиль ветки release/1.2.2.
NNG_VERSION="1.8.0"
NNG_COMMIT="29b73962b939a6fbbf6ea8d5d7680bb06d0eeb99"
NNG_SRC="${WORK_DIR}/thirdparty/nng-${NNG_VERSION}"
NNG_BUILD="${WORK_DIR}/thirdparty/nng-build"
NNG_MARKER="/usr/local/share/satdump-astra17/nng.lock"
if [[ ! -f "${NNG_MARKER}" ]] || ! grep -Fxq "commit=${NNG_COMMIT}" "${NNG_MARKER}"; then
    rm -rf "${NNG_SRC}" "${NNG_BUILD}"
    mkdir -p "$(dirname "${NNG_SRC}")" "${NNG_BUILD}"
    git init "${NNG_SRC}"
    git -C "${NNG_SRC}" remote add origin https://github.com/nanomsg/nng.git
    git -C "${NNG_SRC}" fetch --depth 1 origin "${NNG_COMMIT}"
    git -C "${NNG_SRC}" checkout --detach "${NNG_COMMIT}"
    cmake -S "${NNG_SRC}" -B "${NNG_BUILD}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_SHARED_LIBS=ON \
        -DNNG_TESTS=OFF -DNNG_TOOLS=OFF -DNNG_ENABLE_TLS=OFF
    cmake --build "${NNG_BUILD}" --parallel "${JOBS}"
    # Debian 10 ships CMake 3.13: `cmake --install` only appeared in 3.15.
    # Use the portable install target so the shared library is really installed.
    cmake --build "${NNG_BUILD}" --target install --parallel "${JOBS}"
    ldconfig
    mkdir -p "$(dirname "${NNG_MARKER}")"
    printf 'version=%s\ncommit=%s\n' "${NNG_VERSION}" "${NNG_COMMIT}" > "${NNG_MARKER}"
fi

NNG_LIBRARY_PATH=""
for candidate in /usr/local/lib/libnng.so /usr/local/lib64/libnng.so; do
    if [[ -e "${candidate}" ]]; then
        NNG_LIBRARY_PATH="$(readlink -f "${candidate}")"
        break
    fi
done
[[ -n "${NNG_LIBRARY_PATH}" && -f "${NNG_LIBRARY_PATH}" ]] \
    || fail "NNG install target не создал /usr/local/lib*/libnng.so"
log "NNG runtime: ${NNG_LIBRARY_PATH}"

BUILD_DIR="${WORK_DIR}/satdump-${PROFILE}"
STAGE_ROOT="${WORK_DIR}/stage-${PROFILE}"
if [[ "${KEEP_WORK}" != "1" ]]; then
    rm -rf "${BUILD_DIR}" "${STAGE_ROOT}"
fi
mkdir -p "${BUILD_DIR}" "${STAGE_ROOT}" "${OUTPUT_DIR}"

GUI=OFF
AUDIO=OFF
RTL=OFF
if [[ "${PROFILE}" == "desktop" ]]; then
    GUI=ON
    AUDIO=ON
    RTL=ON
fi

log "CMake configure: profile=${PROFILE}; gcc=$(g++ -dumpfullversion -dumpversion); cmake=$(cmake --version | awk 'NR==1{print $3}')"
cmake -S "${SOURCE_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/opt/satdump \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_PREFIX_PATH=/usr/local \
    -DCMAKE_LIBRARY_PATH=/usr/local/lib \
    -DNNG_LIBRARY="${NNG_LIBRARY_PATH}" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_SKIP_RPATH=OFF \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=OFF \
    -DCMAKE_INSTALL_RPATH= \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF \
    -DBUILD_GUI="${GUI}" \
    -DBUILD_TESTING=OFF \
    -DBUILD_TOOLS=OFF \
    -DBUILD_DOCS=OFF \
    -DBUILD_OPENCL=OFF \
    -DBUILD_ZIQ=OFF \
    -DBUILD_ZIQ2=OFF \
    -DBUILD_OPENMP=ON \
    -DENABLE_INSTALL=ON \
    -DPLUGINS_ALL=OFF \
    -DPLUGIN_METEOR=ON \
    -DPLUGIN_NOAA_METOP=ON \
    -DPLUGIN_ANALOG=ON \
    -DPLUGIN_STANDARD_CPP_COMPOS=ON \
    -DPLUGIN_SIMD_SSE41=OFF \
    -DPLUGIN_SIMD_AVX2=OFF \
    -DPLUGIN_SIMD_NEON=OFF \
    -DPLUGIN_RTLSDR_SDR_SUPPORT="${RTL}" \
    -DPLUGIN_AIRSPY_SDR_SUPPORT=OFF \
    -DPLUGIN_AIRSPYHF_SDR_SUPPORT=OFF \
    -DPLUGIN_HACKRF_SDR_SUPPORT=OFF \
    -DPLUGIN_LIMESDR_SDR_SUPPORT=OFF \
    -DPLUGIN_SDRPLAY_SDR_SUPPORT=OFF \
    -DPLUGIN_PLUTOSDR_SDR_SUPPORT=OFF \
    -DPLUGIN_BLADERF_SDR_SUPPORT=OFF \
    -DPLUGIN_USRP_SDR_SUPPORT=OFF \
    -DPLUGIN_MIRISDR_SDR_SUPPORT=OFF \
    -DPLUGIN_SOAPY_SDR_SUPPORT=OFF \
    -DPLUGIN_SPYSERVER_SUPPORT=OFF \
    -DPLUGIN_RTLTCP_SUPPORT=OFF \
    -DPLUGIN_SDRPP_SERVER_SUPPORT=OFF \
    -DPLUGIN_RFNM_SDR_SUPPORT=OFF \
    -DPLUGIN_NET_SOURCE_SDR_SUPPORT=OFF \
    -DPLUGIN_REMOTE_SDR_SUPPORT=OFF \
    -DPLUGIN_SDDC_SDR_SUPPORT=OFF \
    -DPLUGIN_AARONIA_SDR_SUPPORT=OFF \
    -DPLUGIN_RTAUDIO_SDR_SUPPORT=OFF \
    -DPLUGIN_PORTAUDIO_SINK="${AUDIO}" \
    -DPLUGIN_RTAUDIO_SINK=OFF \
    -DPLUGIN_AAUDIO_SINK=OFF

log "Сборка"
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

rm -rf "${STAGE_ROOT}"
mkdir -p "${STAGE_ROOT}"
# Same CMake 3.13 compatibility rule as for NNG: install via the target.
DESTDIR="${STAGE_ROOT}" cmake --build "${BUILD_DIR}" --target install --parallel "${JOBS}"
STAGE="${STAGE_ROOT}/opt/satdump"
[[ -x "${STAGE}/bin/satdump" ]] || fail "install target не создал bin/satdump"
if [[ "${PROFILE}" == "desktop" ]]; then
    [[ -x "${STAGE}/bin/satdump-ui" ]] || fail "desktop-профиль не создал bin/satdump-ui"
fi

SOURCE_DATE_EPOCH="$(git -C "${SOURCE_DIR}" show -s --format=%ct HEAD 2>/dev/null || date +%s)"
export SOURCE_DATE_EPOCH
bash "${SOURCE_DIR}/scripts/astra/astra17/make-bundle.sh" \
    --stage "${STAGE}" \
    --source "${SOURCE_DIR}" \
    --output "${OUTPUT_DIR}" \
    --profile "${PROFILE}"
