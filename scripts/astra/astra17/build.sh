#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASTRA_SCRIPT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
# shellcheck source=../common.sh
source "${ASTRA_SCRIPT_DIR}/common.sh"

PROFILE="desktop"
WORK_DIR="${SATDUMP_ROOT}/build/astra17-native"
OUTPUT_DIR="${SATDUMP_ROOT}/dist/astra17"
JOBS="$(jobs_count)"
KEEP_WORK=0
PREPARE_BUILD_ENV=0
RELEASE_REVISION="${SATDUMP_RELEASE_REVISION:-local}"

usage() {
    cat <<EOF2
Использование: bash scripts/astra/astra17/build.sh [параметры]

Собирает SatDump 1.2.2 НЕПОСРЕДСТВЕННО в Astra Linux 1.7 и формирует
самодостаточный runtime-бандл. Запуск на Debian/Ubuntu/Buster запрещён.

Параметры:
  --profile headless|desktop  Профиль; release использует desktop
  --work-dir PATH             Каталог CMake/staging (по умолчанию: ${WORK_DIR})
  --output-dir PATH           Каталог готовых пакетов (по умолчанию: ${OUTPUT_DIR})
  --jobs N                    Параллельные задачи (по умолчанию: ${JOBS})
  --keep-work                 Не очищать build/stage перед сборкой
  --prepare-build-env         Установить build-зависимости в СБОРОЧНУЮ Astra 1.7
  --revision VALUE            Ревизия Astra-пакета для манифеста
  -h, --help                  Справка

Важно: --prepare-build-env изменяет только сборочную систему. Готовый release-
пакет содержит runtime-библиотеки и не требует apt install на целевой Astra 1.7.
EOF2
}

while (( $# > 0 )); do
    case "$1" in
        --profile) PROFILE="$2"; shift 2 ;;
        --work-dir) WORK_DIR="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --keep-work) KEEP_WORK=1; shift ;;
        --prepare-build-env) PREPARE_BUILD_ENV=1; shift ;;
        --revision) RELEASE_REVISION="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "Неизвестный параметр Astra 1.7 build: $1" ;;
    esac
done

[[ "${PROFILE}" == "headless" || "${PROFILE}" == "desktop" ]] \
    || die "--profile: headless или desktop"
[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || die "--jobs должен быть положительным целым числом"
[[ "$(uname -m)" == "x86_64" ]] || die "Astra 1.7 release-профиль поддерживает x86_64"

# Release-сборку нельзя подделать через ASTRA_VERSION_OVERRIDE: проверяем только
# фактические файлы ОС внутри build host/container.
if [[ -n "${ASTRA_VERSION_OVERRIDE:-}" ]]; then
    die "Для release-сборки ASTRA_VERSION_OVERRIDE запрещён. Нужна реальная Astra Linux 1.7."
fi

ASTRA_VERSION="$(detect_astra_version)"
[[ "${ASTRA_VERSION}" == "1.7" ]] \
    || die "Сборка разрешена только на Astra Linux 1.7; обнаружено: $(astra_full_version)"

GLIBC_VERSION="$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}' || true)"
[[ "${GLIBC_VERSION}" == "2.28" ]] \
    || die "Astra 1.7 release ожидает glibc 2.28; обнаружена ${GLIBC_VERSION:-unknown}"

log_info "Build OS: $(astra_full_version)"
log_info "glibc: ${GLIBC_VERSION}; arch: $(uname -m); profile: ${PROFILE}"

if (( PREPARE_BUILD_ENV == 1 )); then
    log_info "Подготовка build-зависимостей непосредственно в Astra Linux 1.7"
    if (( EUID == 0 )); then
        apt-get update
        apt-get install -y --no-install-recommends \
            patchelf binutils rsync file coreutils findutils gzip tar
    elif command_exists sudo; then
        sudo apt-get update
        sudo apt-get install -y --no-install-recommends \
            patchelf binutils rsync file coreutils findutils gzip tar
    else
        die "--prepare-build-env требует root/sudo"
    fi
    bash "${ASTRA_SCRIPT_DIR}/install-deps.sh" \
        --profile "${PROFILE}" \
        --bootstrap-missing
fi

for command in g++ gcc pkg-config patchelf readelf objdump ldd rsync sha256sum tar gzip; do
    command_exists "${command}" || die "Build tool не найден: ${command}. Используйте --prepare-build-env."
done

select_compiler
CMAKE_EXECUTABLE="$(find_cmake 3.18.0 2>/dev/null || true)"
[[ -n "${CMAKE_EXECUTABLE}" ]] \
    || die "CMake 3.18+ не найден. Используйте --prepare-build-env."

WORK_DIR="$(readlink -m "${WORK_DIR}")"
OUTPUT_DIR="$(readlink -m "${OUTPUT_DIR}")"
case "${WORK_DIR}" in /|/usr|/etc|/var|/home|/root|/opt) die "Опасный work-dir: ${WORK_DIR}" ;; esac
case "${OUTPUT_DIR}" in /|/usr|/etc|/var|/home|/root|/opt) die "Опасный output-dir: ${OUTPUT_DIR}" ;; esac
mkdir -p "${WORK_DIR}" "${OUTPUT_DIR}"

BUILD_DIR="${WORK_DIR}/build-${PROFILE}"
STAGE_ROOT="${WORK_DIR}/stage-${PROFILE}"
TEST_OUTPUT="${WORK_DIR}/presentation-test-output-${PROFILE}"
if (( KEEP_WORK == 0 )); then
    rm -rf "${BUILD_DIR}" "${STAGE_ROOT}" "${TEST_OUTPUT}"
fi
mkdir -p "${BUILD_DIR}" "${STAGE_ROOT}" "${TEST_OUTPUT}"

export CMAKE_PREFIX_PATH="${ASTRA_DEPS_PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
export CMAKE_LIBRARY_PATH="${ASTRA_DEPS_PREFIX}/lib;${ASTRA_DEPS_PREFIX}/lib64"
export PKG_CONFIG_PATH="${ASTRA_DEPS_PREFIX}/lib/pkgconfig:${ASTRA_DEPS_PREFIX}/lib64/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
export LD_LIBRARY_PATH="${ASTRA_DEPS_PREFIX}/lib:${ASTRA_DEPS_PREFIX}/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

GUI=OFF
AUDIO=OFF
RTL=OFF
if [[ "${PROFILE}" == "desktop" ]]; then
    GUI=ON
    AUDIO=ON
    RTL=ON
fi

CMAKE_ARGS=(
    -S "${SATDUMP_ROOT}"
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=/opt/satdump
    -DCMAKE_INSTALL_LIBDIR=lib
    "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DCMAKE_LIBRARY_PATH=${CMAKE_LIBRARY_PATH}"
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_SKIP_RPATH=OFF
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=OFF
    -DCMAKE_INSTALL_RPATH=
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF
    "-DBUILD_GUI=${GUI}"
    -DBUILD_TESTING=ON
    -DBUILD_TOOLS=OFF
    -DBUILD_DOCS=OFF
    -DBUILD_OPENCL=OFF
    -DBUILD_ZIQ=OFF
    -DBUILD_ZIQ2=OFF
    -DBUILD_OPENMP=ON
    -DENABLE_INSTALL=ON
    -DPLUGINS_ALL=OFF
    -DPLUGIN_METEOR=ON
    -DPLUGIN_NOAA_METOP=ON
    -DPLUGIN_ANALOG=ON
    -DPLUGIN_STANDARD_CPP_COMPOS=ON
    -DPLUGIN_SIMD_SSE41=OFF
    -DPLUGIN_SIMD_AVX2=OFF
    -DPLUGIN_SIMD_NEON=OFF
    "-DPLUGIN_RTLSDR_SDR_SUPPORT=${RTL}"
    -DPLUGIN_AIRSPY_SDR_SUPPORT=OFF
    -DPLUGIN_AIRSPYHF_SDR_SUPPORT=OFF
    -DPLUGIN_HACKRF_SDR_SUPPORT=OFF
    -DPLUGIN_LIMESDR_SDR_SUPPORT=OFF
    -DPLUGIN_SDRPLAY_SDR_SUPPORT=OFF
    -DPLUGIN_PLUTOSDR_SDR_SUPPORT=OFF
    -DPLUGIN_BLADERF_SDR_SUPPORT=OFF
    -DPLUGIN_USRP_SDR_SUPPORT=OFF
    -DPLUGIN_MIRISDR_SDR_SUPPORT=OFF
    -DPLUGIN_SOAPY_SDR_SUPPORT=OFF
    -DPLUGIN_SPYSERVER_SUPPORT=OFF
    -DPLUGIN_RTLTCP_SUPPORT=OFF
    -DPLUGIN_SDRPP_SERVER_SUPPORT=OFF
    -DPLUGIN_RFNM_SDR_SUPPORT=OFF
    -DPLUGIN_NET_SOURCE_SDR_SUPPORT=OFF
    -DPLUGIN_REMOTE_SDR_SUPPORT=OFF
    -DPLUGIN_SDDC_SDR_SUPPORT=OFF
    -DPLUGIN_AARONIA_SDR_SUPPORT=OFF
    -DPLUGIN_RTAUDIO_SDR_SUPPORT=OFF
    "-DPLUGIN_PORTAUDIO_SINK=${AUDIO}"
    -DPLUGIN_RTAUDIO_SINK=OFF
    -DPLUGIN_AAUDIO_SINK=OFF
)

log_info "CMake configure на Astra Linux 1.7"
env CC="${CC}" CXX="${CXX}" "${CMAKE_EXECUTABLE}" "${CMAKE_ARGS[@]}"

log_info "Компиляция SatDump (${JOBS} потоков)"
"${CMAKE_EXECUTABLE}" --build "${BUILD_DIR}" --parallel "${JOBS}"

if [[ -x "${BUILD_DIR}/satdump-presentation-test" ]]; then
    log_info "Проверка presentation renderer: minimal/editorial, легенды и ориентация"
    LD_LIBRARY_PATH="${BUILD_DIR}:${BUILD_DIR}/plugins:${LD_LIBRARY_PATH}" \
        "${BUILD_DIR}/satdump-presentation-test" \
        "${SATDUMP_ROOT}/resources/fonts/Roboto-Medium.ttf" \
        "${TEST_OUTPUT}"
    test -s "${TEST_OUTPUT}/continuous_editorial_landscape.png"
    test -s "${TEST_OUTPUT}/continuous_minimal_portrait.png"
fi

log_info "Чистый staging через DESTDIR"
rm -rf "${STAGE_ROOT}"
mkdir -p "${STAGE_ROOT}"
DESTDIR="${STAGE_ROOT}" "${CMAKE_EXECUTABLE}" --build "${BUILD_DIR}" --target install --parallel "${JOBS}"
STAGE="${STAGE_ROOT}/opt/satdump"
[[ -x "${STAGE}/bin/satdump" ]] || die "install target не создал bin/satdump"
if [[ "${PROFILE}" == "desktop" ]]; then
    [[ -x "${STAGE}/bin/satdump-ui" ]] || die "desktop-профиль не создал bin/satdump-ui"
fi

SOURCE_DATE_EPOCH="$(git -C "${SATDUMP_ROOT}" show -s --format=%ct HEAD 2>/dev/null || date +%s)"
export SOURCE_DATE_EPOCH

bash "${SCRIPT_DIR}/make-bundle.sh" \
    --stage "${STAGE}" \
    --source "${SATDUMP_ROOT}" \
    --output "${OUTPUT_DIR}" \
    --profile "${PROFILE}" \
    --runtime-prefix "${ASTRA_DEPS_PREFIX}" \
    --revision "${RELEASE_REVISION}"

log_ok "Astra Linux 1.7 release-бандл готов: ${OUTPUT_DIR}"
