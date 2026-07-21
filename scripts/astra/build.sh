#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

PROFILE="headless"
BUILD_TYPE="Release"
BUILD_DIR=""
INSTALL_PREFIX="${SATDUMP_INSTALL_PREFIX:-${HOME}/.local/opt/satdump-1.2.2}"
JOBS="$(jobs_count)"
CLEAN=0
DO_INSTALL=0
RUN_TESTS=1
ENABLE_OPENCL=0
ENABLE_ZIQ=0
SDR_PROFILE="auto"
EXTRA_CMAKE_ARGS=()

usage() {
    cat <<EOF
Использование: bash scripts/astra/build.sh [параметры] [-- дополнительные CMake-параметры]

Параметры:
  --profile headless|desktop|full  Профиль сборки (по умолчанию: headless)
  --build-type TYPE               Release, RelWithDebInfo или Debug
  --build-dir PATH                Каталог сборки
  --prefix PATH                   Префикс установки (по умолчанию: ${INSTALL_PREFIX})
  --jobs N                        Параллельные задачи (по умолчанию: ${JOBS})
  --sdr none|rtl|common|all       Набор SDR-плагинов
  --with-opencl                   Включить OpenCL
  --with-ziq                      Включить ZIQ/libzstd
  --without-tests                 Не собирать smoke-тесты
  --clean                         Удалить каталог сборки перед конфигурацией
  --install                       Выполнить cmake --install после сборки
  -h, --help                      Показать справку

Примеры:
  bash scripts/astra/build.sh --profile headless
  bash scripts/astra/build.sh --profile desktop --sdr rtl --install
  bash scripts/astra/build.sh --profile headless -- -DPLUGIN_FY3=ON
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --profile) PROFILE="$2"; shift 2 ;;
        --build-type) BUILD_TYPE="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --prefix) INSTALL_PREFIX="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --sdr) SDR_PROFILE="$2"; shift 2 ;;
        --with-opencl) ENABLE_OPENCL=1; shift ;;
        --with-ziq) ENABLE_ZIQ=1; shift ;;
        --without-tests) RUN_TESTS=0; shift ;;
        --clean) CLEAN=1; shift ;;
        --install) DO_INSTALL=1; shift ;;
        --) shift; EXTRA_CMAKE_ARGS+=("$@"); break ;;
        -h|--help) usage; exit 0 ;;
        *) die "Неизвестный параметр: $1" ;;
    esac
done

case "${PROFILE}" in
    headless|desktop|full) ;;
    *) die "Неизвестный профиль: ${PROFILE}" ;;
esac
case "${SDR_PROFILE}" in
    auto|none|rtl|common|all) ;;
    *) die "Неизвестный SDR-профиль: ${SDR_PROFILE}" ;;
esac
case "${BUILD_TYPE}" in
    Release|RelWithDebInfo|Debug) ;;
    *) die "Допустимые типы сборки: Release, RelWithDebInfo, Debug" ;;
esac
[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || die "--jobs должен быть положительным целым числом."

ASTRA_VERSION="$(detect_astra_version)"
if [[ -z "${BUILD_DIR}" ]]; then
    BUILD_DIR="${SATDUMP_ROOT}/build/astra-${ASTRA_VERSION}-${PROFILE}"
fi

print_platform_summary
select_compiler
log_info "C: $(${CC} --version | head -n1)"
log_info "C++: $(${CXX} --version | head -n1)"

CMAKE_EXECUTABLE="$(find_cmake 3.18.0 2>/dev/null || true)"
[[ -n "${CMAKE_EXECUTABLE}" ]] || die "CMake 3.18+ не найден. Запустите bash scripts/astra/bootstrap-cmake.sh."
log_ok "CMake: ${CMAKE_EXECUTABLE} ($(cmake_version "${CMAKE_EXECUTABLE}"))"

export CMAKE_PREFIX_PATH="${ASTRA_DEPS_PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
export PKG_CONFIG_PATH="${ASTRA_DEPS_PREFIX}/lib/pkgconfig:${ASTRA_DEPS_PREFIX}/lib64/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
export LD_LIBRARY_PATH="${ASTRA_DEPS_PREFIX}/lib:${ASTRA_DEPS_PREFIX}/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

if [[ "${CLEAN}" == "1" ]]; then
    log_info "Очистка ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi
ensure_directory "${BUILD_DIR}"

if [[ "${SDR_PROFILE}" == "auto" ]]; then
    case "${PROFILE}" in
        headless) SDR_PROFILE="none" ;;
        desktop) SDR_PROFILE="rtl" ;;
        full) SDR_PROFILE="common" ;;
    esac
fi

GUI=OFF
PLUGINS_ALL=OFF
AUDIO=OFF
case "${PROFILE}" in
    headless)
        GUI=OFF
        ;;
    desktop)
        GUI=ON
        AUDIO=ON
        ;;
    full)
        GUI=ON
        AUDIO=ON
        PLUGINS_ALL=ON
        log_warn "Профиль full требует расширенного набора библиотек и не является минимальным сертификационным профилем."
        ;;
esac

OPENCL=OFF
ZIQ=OFF
[[ "${ENABLE_OPENCL}" == "1" ]] && OPENCL=ON
[[ "${ENABLE_ZIQ}" == "1" ]] && ZIQ=ON

TESTS=OFF
[[ "${RUN_TESTS}" == "1" ]] && TESTS=ON

RTLSDR=OFF
AIRSPY=OFF
AIRSPYHF=OFF
HACKRF=OFF
LIMESDR=OFF
SDRPLAY=OFF
PLUTOSDR=OFF
BLADERF=OFF
USRP=OFF
MIRISDR=OFF
SOAPY=OFF

case "${SDR_PROFILE}" in
    none) ;;
    rtl) RTLSDR=ON ;;
    common)
        RTLSDR=ON
        AIRSPY=ON
        AIRSPYHF=ON
        HACKRF=ON
        ;;
    all)
        RTLSDR=ON
        AIRSPY=ON
        AIRSPYHF=ON
        HACKRF=ON
        LIMESDR=ON
        SDRPLAY=ON
        PLUTOSDR=ON
        BLADERF=ON
        USRP=ON
        MIRISDR=ON
        SOAPY=ON
        ;;
esac

CMAKE_ARGS=(
    -S "${SATDUMP_ROOT}"
    -B "${BUILD_DIR}"
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    "-DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX}"
    "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DCMAKE_BUILD_RPATH=${ASTRA_DEPS_PREFIX}/lib;${ASTRA_DEPS_PREFIX}/lib64"
    "-DCMAKE_INSTALL_RPATH=${ASTRA_DEPS_PREFIX}/lib;${ASTRA_DEPS_PREFIX}/lib64"
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    "-DBUILD_GUI=${GUI}"
    "-DBUILD_TESTING=${TESTS}"
    -DBUILD_TOOLS=OFF
    "-DBUILD_OPENCL=${OPENCL}"
    "-DBUILD_ZIQ=${ZIQ}"
    -DBUILD_ZIQ2=OFF
    -DBUILD_OPENMP=ON
    -DENABLE_INSTALL=ON
    "-DPLUGINS_ALL=${PLUGINS_ALL}"

    # Основные метеорологические протоколы этого форка.
    -DPLUGIN_METEOR=ON
    -DPLUGIN_NOAA_METOP=ON
    -DPLUGIN_ANALOG=ON
    -DPLUGIN_STANDARD_CPP_COMPOS=ON

    # Переносимость между рабочими станциями: оптимизацию выполняет VOLK.
    -DPLUGIN_SIMD_SSE41=OFF
    -DPLUGIN_SIMD_AVX2=OFF
    -DPLUGIN_SIMD_NEON=OFF

    "-DPLUGIN_RTLSDR_SDR_SUPPORT=${RTLSDR}"
    "-DPLUGIN_AIRSPY_SDR_SUPPORT=${AIRSPY}"
    "-DPLUGIN_AIRSPYHF_SDR_SUPPORT=${AIRSPYHF}"
    "-DPLUGIN_HACKRF_SDR_SUPPORT=${HACKRF}"
    "-DPLUGIN_LIMESDR_SDR_SUPPORT=${LIMESDR}"
    "-DPLUGIN_SDRPLAY_SDR_SUPPORT=${SDRPLAY}"
    "-DPLUGIN_PLUTOSDR_SDR_SUPPORT=${PLUTOSDR}"
    "-DPLUGIN_BLADERF_SDR_SUPPORT=${BLADERF}"
    "-DPLUGIN_USRP_SDR_SUPPORT=${USRP}"
    "-DPLUGIN_MIRISDR_SDR_SUPPORT=${MIRISDR}"
    "-DPLUGIN_SOAPY_SDR_SUPPORT=${SOAPY}"

    # Сетевые и экспериментальные источники отключены в базовых профилях.
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

if (( ${#EXTRA_CMAKE_ARGS[@]} > 0 )); then
    CMAKE_ARGS+=("${EXTRA_CMAKE_ARGS[@]}")
fi

log_info "Конфигурация профиля ${PROFILE}; SDR=${SDR_PROFILE}; build=${BUILD_DIR}"
# В исходном SatDump 1.2.2 переменная CI отключает автоматическое -march=native.
# Это необходимо для переносимости бинарников между машинами Astra Linux.
env CI=astra-linux CC="${CC}" CXX="${CXX}" "${CMAKE_EXECUTABLE}" "${CMAKE_ARGS[@]}"

log_info "Сборка (${JOBS} потоков)"
"${CMAKE_EXECUTABLE}" --build "${BUILD_DIR}" --parallel "${JOBS}"

if [[ "${RUN_TESTS}" == "1" && -x "${BUILD_DIR}/satdump-presentation-test" ]]; then
    TEST_OUTPUT="${BUILD_DIR}/presentation-test-output"
    log_info "Smoke-тест плашек и легенд"
    LD_LIBRARY_PATH="${BUILD_DIR}:${BUILD_DIR}/plugins:${LD_LIBRARY_PATH}" \
        "${BUILD_DIR}/satdump-presentation-test" \
        "${SATDUMP_ROOT}/resources/fonts/Roboto-Medium.ttf" \
        "${TEST_OUTPUT}"
    log_ok "Тестовые изображения: ${TEST_OUTPUT}"
fi

cat > "${BUILD_DIR}/astra-env.sh" <<EOF
#!/usr/bin/env bash
export SATDUMP_BUILD_DIR="${BUILD_DIR}"
export SATDUMP_INSTALL_PREFIX="${INSTALL_PREFIX}"
export ASTRA_DEPS_PREFIX="${ASTRA_DEPS_PREFIX}"
export CMAKE_PREFIX_PATH="${ASTRA_DEPS_PREFIX}:\${CMAKE_PREFIX_PATH:-}"
export PKG_CONFIG_PATH="${ASTRA_DEPS_PREFIX}/lib/pkgconfig:${ASTRA_DEPS_PREFIX}/lib64/pkgconfig:\${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="${BUILD_DIR}:${BUILD_DIR}/plugins:${ASTRA_DEPS_PREFIX}/lib:${ASTRA_DEPS_PREFIX}/lib64:\${LD_LIBRARY_PATH:-}"
EOF
chmod +x "${BUILD_DIR}/astra-env.sh"

if [[ "${DO_INSTALL}" == "1" ]]; then
    log_info "Установка в ${INSTALL_PREFIX}"

    if [[ "${INSTALL_PREFIX}" == "${HOME}"/* ]]; then
        mkdir -p "${INSTALL_PREFIX}"
    fi

    if [[ -d "${INSTALL_PREFIX}" && -w "${INSTALL_PREFIX}" ]]; then
        "${CMAKE_EXECUTABLE}" --install "${BUILD_DIR}"
    elif command_exists sudo; then
        sudo env LD_LIBRARY_PATH="${LD_LIBRARY_PATH}" "${CMAKE_EXECUTABLE}" --install "${BUILD_DIR}"
    else
        die "Нет прав на ${INSTALL_PREFIX}; выберите пользовательский --prefix или выполните установку от администратора."
    fi
    log_ok "SatDump установлен в ${INSTALL_PREFIX}"
fi

log_ok "Сборка завершена: ${BUILD_DIR}"
cat <<EOF

Подготовить пользовательское установленное дерево и проверить версию:
  bash scripts/astra/run.sh --build-dir "${BUILD_DIR}" --prefix "${INSTALL_PREFIX}" --prepare -- version

Повторить установку из существующего build-каталога:
  bash scripts/astra/build.sh --profile ${PROFILE} --build-dir "${BUILD_DIR}" --prefix "${INSTALL_PREFIX}" --install
EOF
