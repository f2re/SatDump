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
    BUNDLE="$(find "${TMP}" -mindepth 1 -maxdepth 1 -type d -name 'satdump-*-astra17-*-full-*' -print -quit)"
else
    BUNDLE="$(readlink -f "${TARGET}")"
fi

[[ -d "${BUNDLE}" ]] || fail "Не найден каталог полного бандла"
for path in \
    satdump install.sh verify.sh ASTRA17-MANIFEST.txt RUNTIME-LIBRARIES.txt SHA256SUMS \
    lib/libc.so.6 lib/ld-linux-x86-64.so.2 lib/libstdc++.so.6 lib/libgcc_s.so.1; do
    [[ -e "${BUNDLE}/${path}" ]] || fail "В release отсутствует ${path}"
done
[[ -x "${BUNDLE}/bin/satdump" ]] || fail "Нет bin/satdump"
[[ -d "${BUNDLE}/share/satdump/resources" ]] || fail "Нет resources"
[[ -d "${BUNDLE}/share/satdump/pipelines" ]] || fail "Нет pipelines"
[[ -f "${BUNDLE}/share/doc/satdump/ASTRA17_COMPLETE_GUIDE.md" ]] || fail "Нет полной документации Astra 1.7"

manifest="${BUNDLE}/ASTRA17-MANIFEST.txt"
grep -Fxq 'profile=astra17-native-full' "${manifest}" || fail "Некорректный build profile"
grep -Fxq 'glibc_build=2.28' "${manifest}" || fail "Release собран не на glibc 2.28"
grep -Fxq 'glibc_bundled=yes' "${manifest}" || fail "glibc не помечена как bundled"
grep -Fxq 'runtime_closure=complete' "${manifest}" || fail "runtime closure не помечен как complete"

required="$(awk -F= '$1=="glibc_required"{print $2}' "${manifest}")"
[[ -n "${required}" ]] || fail "Нет glibc_required"
dpkg --compare-versions "${required}" le 2.28 || fail "Требуется GLIBC_${required} > 2.28"
ok "ABI: GLIBC_${required}, bundled glibc 2.28"

(
    cd "${BUNDLE}"
    sha256sum -c SHA256SUMS >/dev/null
)
ok "SHA256SUMS внутри пакета корректен"

failed=0
while IFS= read -r -d '' elf; do
    readelf -h "${elf}" >/dev/null 2>&1 || continue
    out="$(LD_LIBRARY_PATH="${BUNDLE}/lib:${BUNDLE}/lib/satdump/plugins" ldd -r "${elf}" 2>&1 || true)"
    if grep -Eqi 'not found|undefined symbol' <<<"${out}"; then
        printf 'ELF runtime failure: %s\n%s\n' "${elf}" "${out}" >&2
        failed=1
    fi
    while IFS= read -r resolved; do
        [[ -n "${resolved}" ]] || continue
        case "${resolved}" in
            "${BUNDLE}/lib/"*) ;;
            *) printf 'Внешняя DT_NEEDED библиотека: %s -> %s\n' "${elf}" "${resolved}" >&2; failed=1 ;;
        esac
    done < <(awk '/=> \/.*/ {print $3}' <<<"${out}")
done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0)
[[ "${failed}" == "0" ]] || fail "Release использует внешние runtime-библиотеки или имеет relocation error"
ok "Все DT_NEEDED runtime-библиотеки закрыты внутри release"

PROBE="$(mktemp -d "${TMPDIR:-/tmp}/satdump-astra17-runtime.XXXXXX")"
mkdir -p "${PROBE}/home/.config/satdump" "${PROBE}/cwd"
printf '%s\n' '{"satdump_general":{"tle_update_interval":{"value":"Never"},"log_to_file":{"value":false}}}' \
    > "${PROBE}/home/.config/satdump/settings.json"
(
    cd "${PROBE}/cwd"
    HOME="${PROBE}/home" "${BUNDLE}/satdump" version
) > "${PROBE}/version.log" 2>&1 || { cat "${PROBE}/version.log" >&2; rm -rf "${PROBE}"; fail "SatDump не запускается bundled loader-ом"; }
grep -q 'SatDump v1.2.2' "${PROBE}/version.log" || { cat "${PROBE}/version.log" >&2; rm -rf "${PROBE}"; fail "Не подтверждена версия SatDump"; }
rm -rf "${PROBE}"
ok "SatDump 1.2.2 запускается с bundled glibc/loader"

if [[ -x "${BUNDLE}/satdump-ui" ]]; then
    ok "GUI launcher присутствует"
fi
info "Манифест: ${BUNDLE}/ASTRA17-MANIFEST.txt"
info "Runtime inventory: ${BUNDLE}/RUNTIME-LIBRARIES.txt"
