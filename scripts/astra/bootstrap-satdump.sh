#!/usr/bin/env bash
set -Eeuo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

PROFILE="desktop"
BUILD_TYPE="Release"
INSTALL_PREFIX="${HOME}/.local/opt/satdump-1.2.2"
JOBS="$(jobs_count)"

select_compiler
CMAKE_EXECUTABLE="$(find_cmake 3.18.0 2>/dev/null || true)"
[[ -n "${CMAKE_EXECUTABLE}" ]] || die "CMake 3.18+ не найден."

export CMAKE_PREFIX_PATH="${ASTRA_DEPS_PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
export PKG_CONFIG_PATH="${ASTRA_DEPS_PREFIX}/lib/pkgconfig:${ASTRA_DEPS_PREFIX}/lib64/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
export LD_LIBRARY_PATH="${ASTRA_DEPS_PREFIX}/lib:${ASTRA_DEPS_PREFIX}/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

ASTRA_VERSION="$(detect_astra_version)"
BUILD_DIR="${SATDUMP_ROOT}/build/astra-${ASTRA_VERSION}-${PROFILE}"
ensure_directory "${BUILD_DIR}"

log_info "Конфигурация SatDump (${PROFILE})"
"${CMAKE_EXECUTABLE}" -S "${SATDUMP_ROOT}" -B "${BUILD_DIR}" \
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}" \
    "-DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX}" \
    "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}" \
    "-DCMAKE_BUILD_RPATH=${ASTRA_DEPS_PREFIX}/lib;${ASTRA_DEPS_PREFIX}/lib64" \
    "-DCMAKE_INSTALL_RPATH=${ASTRA_DEPS_PREFIX}/lib;${ASTRA_DEPS_PREFIX}/lib64" \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_GUI=ON \
    -DBUILD_TESTING=ON \
    -DBUILD_TOOLS=OFF \
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
    -DPLUGIN_RTLSDR_SDR_SUPPORT=ON \
    -DPLUGIN_PORTAUDIO_SINK=ON

log_info "Сборка SatDump"
"${CMAKE_EXECUTABLE}" --build "${BUILD_DIR}" --parallel "${JOBS}"

if [[ -x "${BUILD_DIR}/satdump-presentation-test" ]]; then
    TEST_OUTPUT="${BUILD_DIR}/presentation-test-output"
    log_info "Smoke-тест плашек и легенд"
    LD_LIBRARY_PATH="${BUILD_DIR}:${BUILD_DIR}/plugins:${LD_LIBRARY_PATH}" \
        "${BUILD_DIR}/satdump-presentation-test" \
        "${SATDUMP_ROOT}/resources/fonts/Roboto-Medium.ttf" \
        "${TEST_OUTPUT}"
    log_ok "Тестовые изображения: ${TEST_OUTPUT}"
fi

log_info "Установка SatDump в ${INSTALL_PREFIX}"
"${CMAKE_EXECUTABLE}" --install "${BUILD_DIR}"

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

log_ok "Сборка и установка завершены!"
