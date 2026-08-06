#!/usr/bin/env bash
set -Eeuo pipefail

DEPS_DIR="${HOME}/.local/opt/satdump-astra/deps"
export CMAKE_PREFIX_PATH="${DEPS_DIR}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
export PKG_CONFIG_PATH="${DEPS_DIR}/lib/pkgconfig:${DEPS_DIR}/lib64/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
export LD_LIBRARY_PATH="${DEPS_DIR}/lib:${DEPS_DIR}/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export LIBRARY_PATH="${DEPS_DIR}/lib:${DEPS_DIR}/lib64${LIBRARY_PATH:+:${LIBRARY_PATH}}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SATDUMP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cd "${SATDUMP_ROOT}"
PROFILE="${1:-headless}"
shift || true
exec bash "${SCRIPT_DIR}/../build-and-bundle.sh" --profile "${PROFILE}" "$@"
