#!/usr/bin/env bash

set -Eeuo pipefail

TARGET="${1:-}"
[[ -n "${TARGET}" ]] || { printf 'Использование: %s <bundle-dir|bundle.tar.gz>\n' "$0" >&2; exit 2; }

TEMP_DIR=""
PROBE_ROOT=""
cleanup() {
    [[ -n "${TEMP_DIR}" ]] && rm -rf "${TEMP_DIR}"
    [[ -n "${PROBE_ROOT}" ]] && rm -rf "${PROBE_ROOT}"
}
trap cleanup EXIT

if [[ -f "${TARGET}" ]]; then
    TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/satdump-bundle-check.XXXXXX")"
    tar -xzf "${TARGET}" -C "${TEMP_DIR}"
    BUNDLE="$(find "${TEMP_DIR}" -mindepth 1 -maxdepth 1 -type d -name 'satdump-*glibc224*' | head -n1)"
else
    BUNDLE="$(readlink -f "${TARGET}")"
fi

fail() { printf '✖ %s\n' "$*" >&2; exit 1; }
ok() { printf '✔ %s\n' "$*"; }
info() { printf 'ℹ %s\n' "$*"; }

[[ -d "${BUNDLE}" ]] || fail "Не найден каталог бандла."
[[ -x "${BUNDLE}/satdump" ]] || fail "Не найден wrapper ${BUNDLE}/satdump"
[[ -x "${BUNDLE}/bin/satdump" ]] || fail "Не найден bin/satdump"
[[ -d "${BUNDLE}/share/satdump/resources" ]] || fail "Не найдены resources"
[[ -d "${BUNDLE}/share/satdump/pipelines" ]] || fail "Не найдены pipelines"
[[ -f "${BUNDLE}/PORTABLE-MANIFEST.txt" ]] || fail "Нет PORTABLE-MANIFEST.txt"

REQUIRED_GLIBC="$(awk -F= '$1 == "glibc_required" { print $2 }' "${BUNDLE}/PORTABLE-MANIFEST.txt")"
SYSTEM_GLIBC="$(ldd --version 2>&1 | head -n1 | grep -o '[0-9][0-9.]*$' || true)"
[[ -n "${REQUIRED_GLIBC}" ]] || fail "В манифесте не указана glibc_required."
[[ -n "${SYSTEM_GLIBC}" ]] || fail "Не удалось определить системную glibc."
if ! dpkg --compare-versions "${SYSTEM_GLIBC}" ge "${REQUIRED_GLIBC}"; then
    fail "Системная glibc ${SYSTEM_GLIBC} старше требуемой ${REQUIRED_GLIBC}."
fi
ok "glibc ${SYSTEM_GLIBC} >= ${REQUIRED_GLIBC}"

failed=0
while IFS= read -r -d '' elf; do
    if ! readelf -h "${elf}" >/dev/null 2>&1; then
        continue
    fi
    output="$(LD_LIBRARY_PATH="${BUNDLE}/lib:${BUNDLE}/lib/satdump/plugins" ldd "${elf}" 2>&1 || true)"
    if grep -qi 'not found' <<<"${output}"; then
        printf '%s\n%s\n' "Не разрешены зависимости: ${elf}" "${output}" >&2
        failed=1
    fi
done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0)
[[ "${failed}" == "0" ]] || fail "Есть неразрешённые ELF-зависимости."
ok "Все ELF-зависимости разрешены"

# Пустой cwd гарантирует, что проверка не использует satdump_cfg.json, pipelines,
# settings.json или плагины из дерева исходников/предыдущей установки.
PROBE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/satdump-runtime-probe.XXXXXX")"
mkdir -p "${PROBE_ROOT}/home/.config/satdump" "${PROBE_ROOT}/output" "${PROBE_ROOT}/cwd"
cat > "${PROBE_ROOT}/home/.config/satdump/settings.json" <<'EOF'
{
  "satdump_general": {
    "tle_update_interval": { "value": "Never" },
    "log_level": { "value": "trace" },
    "log_to_file": { "value": false }
  }
}
EOF

VERSION_LOG="${PROBE_ROOT}/version.log"
(
    cd "${PROBE_ROOT}/cwd"
    HOME="${PROBE_ROOT}/home" "${BUNDLE}/satdump" version
) >"${VERSION_LOG}" 2>&1
if ! grep -q 'SatDump v1.2.2' "${VERSION_LOG}"; then
    cat "${VERSION_LOG}" >&2
    fail "Версия SatDump не подтверждена."
fi
ok "SatDump 1.2.2 запускается через relocatable wrapper"

PROBE_LOG="${PROBE_ROOT}/plugin-probe.log"
(
    cd "${PROBE_ROOT}/cwd"
    HOME="${PROBE_ROOT}/home" "${BUNDLE}/satdump" \
        __portable_plugin_probe__ raw /dev/null "${PROBE_ROOT}/output"
) >"${PROBE_LOG}" 2>&1 || true

if grep -Eqi 'Error loading .*undefined symbol|Error loading .*No such file|cannot open shared object file|not found' "${PROBE_LOG}"; then
    grep -Ei 'Error loading|undefined symbol|No such file|cannot open shared object file|not found' "${PROBE_LOG}" >&2 || true
    fail "Плагины содержат ABI/линковочную ошибку."
fi
ok "Плагины загружаются без undefined symbol"

PLUGIN_COUNT="$(find "${BUNDLE}/lib/satdump/plugins" -maxdepth 1 -type f -name '*.so' | wc -l | tr -d ' ')"
info "Плагинов: ${PLUGIN_COUNT}"
info "Манифест: ${BUNDLE}/PORTABLE-MANIFEST.txt"
ok "Portable-бандл пригоден для функционального прогона на этой системе"
