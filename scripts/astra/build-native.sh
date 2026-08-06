#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

PROFILE="headless"
BUILD_TYPE="Release"
BUILD_DIR=""
INSTALL_PREFIX="${SATDUMP_INSTALL_PREFIX:-${HOME}/.local/opt/satdump-1.2.2}"
JOBS="$(jobs_count)"
CLEAN=0
CLEAN_PREFIX=0
DO_INSTALL=0
RUN_TESTS=1
ENABLE_OPENCL=0
ENABLE_ZIQ=0
SDR_PROFILE="auto"
EXTRA_CMAKE_ARGS=()

usage() {
    cat <<EOF
Использование: bash scripts/astra/build-native.sh [параметры] [-- дополнительные CMake-параметры]

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
  --clean-prefix                  Перед install безопасно очистить SatDump-prefix
  --install                       Выполнить cmake --install после сборки
  -h, --help                      Показать справку

Примеры:
  bash scripts/astra/build-native.sh --profile headless
  bash scripts/astra/build-native.sh --profile desktop --sdr rtl --clean --clean-prefix --install
  bash scripts/astra/build-native.sh --profile headless -- -DPLUGIN_FY3=ON
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
        --clean-prefix) CLEAN_PREFIX=1; shift ;;
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
[[ "${CLEAN_PREFIX}" == "0" || "${DO_INSTALL}" == "1" ]] \
    || die "--clean-prefix применяется только вместе с --install."

ASTRA_VERSION="$(detect_astra_version)"
if [[ -z "${BUILD_DIR}" ]]; then
    BUILD_DIR="${SATDUMP_ROOT}/build/astra-${ASTRA_VERSION}-${PROFILE}"
fi

print_platform_summary
select_compiler
log_info "Режим сборки: native"
log_info "C: $(${CC} --version | head -n1)"
log_info "C++: $(${CXX} --version | head -n1)"

CMAKE_EXECUTABLE="$(find_cmake 3.18.0 2>/dev/null || true)"
[[ -n "${CMAKE_EXECUTABLE}" ]] || die "CMake 3.18+ не найден. Запустите bash scripts/astra/bootstrap-cmake.sh."
log_ok "CMake: ${CMAKE_EXECUTABLE} ($(cmake_version "${CMAKE_EXECUTABLE}"))"

export CMAKE_PREFIX_PATH="${ASTRA_DEPS_PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
export PKG_CONFIG_PATH="${ASTRA_DEPS_PREFIX}/lib/pkgconfig:${ASTRA_DEPS_PREFIX}/lib64/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
export LD_LIBRARY_PATH="${ASTRA_DEPS_PREFIX}/lib:${ASTRA_DEPS_PREFIX}/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export LIBRARY_PATH="${ASTRA_DEPS_PREFIX}/lib:${ASTRA_DEPS_PREFIX}/lib64${LIBRARY_PATH:+:${LIBRARY_PATH}}"

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
    "-DCMAKE_LIBRARY_PATH=${ASTRA_DEPS_PREFIX}/lib;${ASTRA_DEPS_PREFIX}/lib64"
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
env CC="${CC}" CXX="${CXX}" "${CMAKE_EXECUTABLE}" "${CMAKE_ARGS[@]}"

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
export SATDUMP_BUILD_MODE="native"
export SATDUMP_BUILD_DIR="${BUILD_DIR}"
export SATDUMP_INSTALL_PREFIX="${INSTALL_PREFIX}"
export ASTRA_DEPS_PREFIX="${ASTRA_DEPS_PREFIX}"
export CMAKE_PREFIX_PATH="${ASTRA_DEPS_PREFIX}:\${CMAKE_PREFIX_PATH:-}"
export PKG_CONFIG_PATH="${ASTRA_DEPS_PREFIX}/lib/pkgconfig:${ASTRA_DEPS_PREFIX}/lib64/pkgconfig:\${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="${BUILD_DIR}:${BUILD_DIR}/plugins:${ASTRA_DEPS_PREFIX}/lib:${ASTRA_DEPS_PREFIX}/lib64:\${LD_LIBRARY_PATH:-}"
EOF
chmod +x "${BUILD_DIR}/astra-env.sh"

install_command() {
    if [[ -d "${INSTALL_PREFIX}" && -w "${INSTALL_PREFIX}" ]]; then
        "$@"
    elif [[ ! -e "${INSTALL_PREFIX}" && -w "$(dirname "${INSTALL_PREFIX}")" ]]; then
        "$@"
    elif command_exists sudo; then
        sudo env LD_LIBRARY_PATH="${LD_LIBRARY_PATH}" "$@"
    else
        die "Нет прав на ${INSTALL_PREFIX}; выберите пользовательский --prefix или выполните установку от администратора."
    fi
}

validate_clean_prefix() {
    local resolved base nonempty=0
    resolved="$(readlink -m "${INSTALL_PREFIX}")"
    base="$(basename "${resolved}")"

    case "${resolved}" in
        /|/usr|/usr/local|/opt|/var|/home|/root|"${HOME}")
            die "Отказ очищать опасный install prefix: ${resolved}"
            ;;
    esac
    [[ "${base}" == satdump* ]] \
        || die "Для --clean-prefix basename должен начинаться с satdump: ${resolved}"
    [[ ! -L "${INSTALL_PREFIX}" ]] \
        || die "--clean-prefix запрещён для символьной ссылки: ${INSTALL_PREFIX}"

    if [[ -d "${resolved}" ]]; then
        [[ -z "$(find "${resolved}" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]] || nonempty=1
        if [[ "${nonempty}" == "1" && \
              ! -f "${resolved}/.satdump-install-root" && \
              ! -x "${resolved}/bin/satdump" && \
              ! -d "${resolved}/share/satdump" ]]; then
            die "Каталог не похож на SatDump-install и не будет очищен: ${resolved}"
        fi
    elif [[ -e "${resolved}" ]]; then
        die "Install prefix существует и не является каталогом: ${resolved}"
    fi

    printf '%s\n' "${resolved}"
}

clean_install_prefix() {
    local resolved
    resolved="$(validate_clean_prefix)"
    log_warn "Очистка versioned SatDump-prefix: ${resolved}"
    if [[ -d "${resolved}" ]]; then
        if [[ -w "${resolved}" ]]; then
            find "${resolved}" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
        elif command_exists sudo; then
            sudo find "${resolved}" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
        else
            die "Нет прав для очистки ${resolved}."
        fi
    fi
}

write_install_marker() {
    local temporary
    temporary="$(mktemp "${TMPDIR:-/tmp}/satdump-install-marker.XXXXXX")"
    cat > "${temporary}" <<EOF
profile=satdump-native
version=1.2.2
astra=${ASTRA_VERSION}
build_type=${BUILD_TYPE}
source=$(git -C "${SATDUMP_ROOT}" rev-parse HEAD 2>/dev/null || printf unknown)
EOF
    install_command install -m 0644 "${temporary}" "${INSTALL_PREFIX}/.satdump-install-root"
    rm -f "${temporary}"
}

if [[ "${DO_INSTALL}" == "1" ]]; then
    log_info "Установка в ${INSTALL_PREFIX}"

    if [[ "${CLEAN_PREFIX}" == "1" ]]; then
        clean_install_prefix
    fi

    if [[ "${INSTALL_PREFIX}" == "${HOME}"/* ]]; then
        mkdir -p "${INSTALL_PREFIX}"
    fi

    install_command "${CMAKE_EXECUTABLE}" --install "${BUILD_DIR}"
    write_install_marker
    log_ok "SatDump установлен в ${INSTALL_PREFIX}"
fi

log_ok "Native-сборка завершена: ${BUILD_DIR}"
cat <<EOF

Проверить установленное дерево и версию:
  bash scripts/astra/run.sh --prefix "${INSTALL_PREFIX}" -- version

Повторная чистая установка в versioned prefix:
  bash scripts/astra/build.sh --mode native --profile ${PROFILE} --build-dir "${BUILD_DIR}" --prefix "${INSTALL_PREFIX}" --clean-prefix --install

Переносимый бандл с контролируемой glibc:
  bash scripts/astra/build.sh --mode portable-glibc224 --profile reference
EOF
