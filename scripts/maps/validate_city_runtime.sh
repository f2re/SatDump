#!/usr/bin/env bash
# Validate the installed/packaged library, catalogue and fonts without network.
set -Eeuo pipefail
[[ $# == 3 ]] || { echo "Usage: $0 <source-resources> <install-prefix> <output>" >&2; exit 2; }
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE="$(readlink -f "$1")"
PREFIX="$(readlink -f "$2")"
mkdir -p "$3"
OUTPUT="$(readlink -f "$3")"
TEST="${PREFIX}/bin/satdump-city-names-runtime-test"
[[ -x "${TEST}" ]] || { echo "Missing installed C++ city runtime test: ${TEST}" >&2; exit 1; }
python3 "${SCRIPT_DIR}/verify_installed_city_names.py" "${SOURCE}" "${PREFIX}/share/satdump/resources" > "${OUTPUT}/resource-integrity.json"
# Installed libraries take priority over the source build and system copies.
LD_LIBRARY_PATH="${PREFIX}/lib:${PREFIX}/lib64:${PREFIX}/lib/satdump/plugins:${LD_LIBRARY_PATH:-}" \
    "${TEST}" "${PREFIX}/share/satdump/resources" "${OUTPUT}"
