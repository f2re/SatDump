#!/usr/bin/env bash
set -Eeuo pipefail

TARGET="${1:-}"
[[ -n "${TARGET}" ]] || { printf 'Использование: %s <bundle-dir|tar.gz>\n' "$0" >&2; exit 2; }

fail() { printf '✖ %s\n' "$*" >&2; exit 1; }
ok() { printf '✔ %s\n' "$*"; }
info() { printf 'ℹ %s\n' "$*"; }

TMP=""
cleanup() { [[ -z "${TMP}" ]] || rm -rf "${TMP}"; }
trap cleanup EXIT

if [[ -f "${TARGET}" ]]; then
    TMP="$(mktemp -d "${TMPDIR:-/tmp}/satdump-astra17-check.XXXXXX")"
    tar -xzf "${TARGET}" -C "${TMP}"
    BUNDLE="$(find "${TMP}" -mindepth 1 -maxdepth 1 -type d -name 'satdump-*-astra17-*' | head -n1)"
else
    BUNDLE="$(readlink -f "${TARGET}")"
fi

[[ -d "${BUNDLE}" ]] || fail "Не найден каталог бандла"
[[ -x "${BUNDLE}/satdump" ]] || fail "Нет wrapper satdump"
[[ -x "${BUNDLE}/bin/satdump" ]] || fail "Нет bin/satdump"
[[ -f "${BUNDLE}/ASTRA17-MANIFEST.txt" ]] || fail "Нет ASTRA17-MANIFEST.txt"

required="$(awk -F= '$1=="glibc_required"{print $2}' "${BUNDLE}/ASTRA17-MANIFEST.txt")"
system="$(ldd --version 2>&1 | head -n1 | grep -o '[0-9][0-9.]*$' || true)"
[[ -n "${required}" && -n "${system}" ]] || fail "Не удалось определить glibc"
dpkg --compare-versions "${system}" ge "${required}" || fail "Системная glibc ${system} < требуемой ${required}"
ok "glibc ${system} >= ${required}"

failed=0
while IFS= read -r -d '' elf; do
    readelf -h "${elf}" >/dev/null 2>&1 || continue
    dyn="$(readelf -d "${elf}" 2>/dev/null || true)"
    if grep -E '(RPATH|RUNPATH).*(/build|/home|/opt|/usr/local)' <<<"${dyn}" >/dev/null; then
        printf 'Абсолютный RPATH/RUNPATH: %s\n' "${elf}" >&2
        failed=1
    fi
    out="$(LD_LIBRARY_PATH="${BUNDLE}/lib:${BUNDLE}/lib/satdump/plugins" ldd "${elf}" 2>&1 || true)"
    if grep -qi 'not found' <<<"${out}"; then
        printf 'Не разрешены зависимости: %s\n%s\n' "${elf}" "${out}" >&2
        failed=1
    fi
done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0)
[[ "${failed}" == "0" ]] || fail "ELF validation failed"
ok "Все ELF-зависимости разрешены, абсолютных RPATH нет"

PROBE="$(mktemp -d "${TMPDIR:-/tmp}/satdump-astra17-runtime.XXXXXX")"
mkdir -p "${PROBE}/home/.config/satdump" "${PROBE}/cwd"
cat > "${PROBE}/home/.config/satdump/settings.json" <<'EOF2'
{"satdump_general":{"tle_update_interval":{"value":"Never"},"log_to_file":{"value":false}}}
EOF2
(
    cd "${PROBE}/cwd"
    HOME="${PROBE}/home" "${BUNDLE}/satdump" version
) > "${PROBE}/version.log" 2>&1 || { cat "${PROBE}/version.log" >&2; rm -rf "${PROBE}"; fail "SatDump не запускается"; }
grep -q 'SatDump v1.2.2' "${PROBE}/version.log" || { cat "${PROBE}/version.log" >&2; rm -rf "${PROBE}"; fail "Не подтверждена версия SatDump"; }
rm -rf "${PROBE}"
ok "CLI SatDump 1.2.2 запускается"

if [[ -r /etc/astra_version ]]; then
    info "Целевая ОС: Astra Linux $(cat /etc/astra_version)"
elif [[ -r /etc/os-release ]]; then
    info "Целевая ОС: $(. /etc/os-release; printf '%s %s' "${PRETTY_NAME:-${NAME:-Linux}}" "${VERSION_ID:-}")"
fi
info "Манифест: ${BUNDLE}/ASTRA17-MANIFEST.txt"
