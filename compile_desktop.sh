#!/usr/bin/env bash
set -Eeuo pipefail

export CC=/usr/bin/gcc-8
export CXX=/usr/bin/g++-8
export ASTRA_DEPS_PREFIX="/home/YaremenkoIA/.local/opt/satdump-astra/deps"
export INSTALL_PREFIX="/home/YaremenkoIA/.local/opt/satdump-1.2.2"
export BUILD_DIR="/home/YaremenkoIA/SatDump/build/astra-1.7-desktop"

export CMAKE_PREFIX_PATH="${ASTRA_DEPS_PREFIX}:${CMAKE_PREFIX_PATH:-}"
export PKG_CONFIG_PATH="${ASTRA_DEPS_PREFIX}/lib/pkgconfig:${ASTRA_DEPS_PREFIX}/lib64/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="${ASTRA_DEPS_PREFIX}/lib:${ASTRA_DEPS_PREFIX}/lib64:${LD_LIBRARY_PATH:-}"

echo "=== Configuring SatDump Native Desktop Profile ==="
cmake -S /home/YaremenkoIA/SatDump -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}" \
    -DCMAKE_BUILD_RPATH="${ASTRA_DEPS_PREFIX}/lib;${ASTRA_DEPS_PREFIX}/lib64" \
    -DCMAKE_INSTALL_RPATH="${ASTRA_DEPS_PREFIX}/lib;${ASTRA_DEPS_PREFIX}/lib64" \
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
    -DPLUGIN_PORTAUDIO_SINK=ON \
    -DPLUGIN_RTAUDIO_SINK=OFF \
    -DPLUGIN_AAUDIO_SINK=OFF

echo "=== Building SatDump Native Desktop Profile ==="
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

echo "=== Running smoke test if present ==="
if [ -x "${BUILD_DIR}/satdump-presentation-test" ]; then
    TEST_OUTPUT="${BUILD_DIR}/presentation-test-output"
    LD_LIBRARY_PATH="${BUILD_DIR}:${BUILD_DIR}/plugins:${LD_LIBRARY_PATH}" \
        "${BUILD_DIR}/satdump-presentation-test" \
        "/home/YaremenkoIA/SatDump/resources/fonts/Roboto-Medium.ttf" \
        "${TEST_OUTPUT}"
    echo "Smoke test passed. Output: ${TEST_OUTPUT}"
fi

echo "=== Installing SatDump ==="
cmake --install "${BUILD_DIR}"

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

echo "=== Desktop Build and Install Completed Successfully ==="
