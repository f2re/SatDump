#!/usr/bin/env bash
set -Eeuo pipefail

STAGE=""
SOURCE_DIR=""
OUTPUT_DIR=""
PROFILE="desktop"
GLIBC_MAX="2.28"

log() { printf '[astra17/bundle] %s\n' "$*"; }
fail() { printf '[astra17/bundle] ERROR: %s\n' "$*" >&2; exit 1; }

while (( $# > 0 )); do
    case "$1" in
        --stage) STAGE="$2"; shift 2 ;;
        --source) SOURCE_DIR="$2"; shift 2 ;;
        --output) OUTPUT_DIR="$2"; shift 2 ;;
        --profile) PROFILE="$2"; shift 2 ;;
        *) fail "Неизвестный параметр: $1" ;;
    esac
done

[[ -x "${STAGE}/bin/satdump" ]] || fail "Нет ${STAGE}/bin/satdump"
[[ -d "${SOURCE_DIR}" ]] || fail "Нет source-dir"
[[ "${PROFILE}" == "headless" || "${PROFILE}" == "desktop" ]] || fail "Некорректный профиль"
command -v patchelf >/dev/null 2>&1 || fail "Нужен patchelf"
command -v readelf >/dev/null 2>&1 || fail "Нужен readelf"

mkdir -p "${OUTPUT_DIR}"
BASENAME="satdump-1.2.2-astra17-${PROFILE}-glibc228-x86_64"
BUNDLE="${OUTPUT_DIR}/${BASENAME}"
ARCHIVE="${OUTPUT_DIR}/${BASENAME}.tar.gz"
rm -rf "${BUNDLE}" "${ARCHIVE}" "${ARCHIVE}.sha256"
mkdir -p "${BUNDLE}"
cp -a "${STAGE}/." "${BUNDLE}/"
mkdir -p "${BUNDLE}/lib" "${BUNDLE}/lib/satdump/plugins"

# Эти компоненты предоставляет целевая Astra 1.7. Перенос собственной glibc вместе
# с системным loader/NSS небезопасен и создаёт ABI-сбои с NSS, DNS и драйверами.
GLIBC_RE='^(ld-linux.*|libc\.so|libm\.so|libpthread\.so|libdl\.so|librt\.so|libresolv\.so|libnsl\.so|libnss_|libutil\.so|libcrypt\.so|libanl\.so|libBrokenLocale\.so)'
SEARCH_PATHS="${BUNDLE}/lib:${BUNDLE}/lib/satdump/plugins:/usr/local/lib:/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu:/usr/lib:/lib"

is_elf() { readelf -h "$1" >/dev/null 2>&1; }
sha() { sha256sum "$1" | awk '{print $1}'; }

# return 10 = в closure появился новый файл/alias. Этот код всегда вызывается
# из if-конструкции, поэтому `set -e` не превращает его в фатальную ошибку.
ensure_link() {
    local name="$1"
    local target="$2"
    local dst="${BUNDLE}/lib/${name}"
    [[ -n "${name}" && "${name}" != "${target}" ]] || return 0
    if [[ -L "${dst}" ]]; then
        [[ "$(readlink "${dst}")" == "${target}" ]] || fail "Конфликт symlink ${name}: $(readlink "${dst}") vs ${target}"
        return 0
    fi
    if [[ -e "${dst}" ]]; then
        [[ "$(sha "${dst}")" == "$(sha "${BUNDLE}/lib/${target}")" ]] || fail "Конфликт библиотеки ${name}"
        return 0
    fi
    ln -s "${target}" "${dst}"
    return 10
}

copy_dependency() {
    local needed="$1"
    local resolved="$2"
    [[ -n "${needed}" && -f "${resolved}" ]] || return 0
    [[ ! "${needed}" =~ ${GLIBC_RE} ]] || return 0

    local real realname dest soname rc added=0
    real="$(readlink -f "${resolved}")"
    realname="$(basename "${real}")"
    dest="${BUNDLE}/lib/${realname}"

    if [[ -e "${dest}" && ! -L "${dest}" ]]; then
        [[ "$(sha "${dest}")" == "$(sha "${real}")" ]] || fail "Две разные библиотеки претендуют на ${realname}: ${resolved}"
    else
        rm -f "${dest}"
        cp -p "${real}" "${dest}"
        added=1
    fi

    if ensure_link "${needed}" "${realname}"; then
        rc=0
    else
        rc=$?
    fi
    [[ "${rc}" == "10" ]] && added=1
    [[ "${rc}" == "0" || "${rc}" == "10" ]] || return "${rc}"

    soname="$(readelf -d "${real}" 2>/dev/null | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p' | head -n1)"
    if [[ -n "${soname}" ]]; then
        if ensure_link "${soname}" "${realname}"; then
            rc=0
        else
            rc=$?
        fi
        [[ "${rc}" == "10" ]] && added=1
        [[ "${rc}" == "0" || "${rc}" == "10" ]] || return "${rc}"
    fi

    if [[ "${added}" == "1" ]]; then
        return 10
    fi
    return 0
}

# return 1 = как минимум одна новая non-glibc runtime-зависимость добавлена.
collect_from_elf() {
    local elf="$1"
    local changed=0 needed resolved rc
    while IFS=$'\t' read -r needed resolved; do
        [[ -n "${needed}" && -n "${resolved}" ]] || continue
        if copy_dependency "${needed}" "${resolved}"; then
            rc=0
        else
            rc=$?
        fi
        [[ "${rc}" == "10" ]] && changed=1
        [[ "${rc}" == "0" || "${rc}" == "10" ]] || return "${rc}"
    done < <(
        LD_LIBRARY_PATH="${SEARCH_PATHS}" ldd "${elf}" 2>/dev/null \
            | awk '/=> \/.*/ {print $1 "\t" $3}'
    )
    return "${changed}"
}

log "Рекурсивное построение runtime closure"
for pass in $(seq 1 12); do
    changed=0
    while IFS= read -r -d '' elf; do
        is_elf "${elf}" || continue
        if collect_from_elf "${elf}"; then
            rc=0
        else
            rc=$?
        fi
        [[ "${rc}" == "1" ]] && changed=1
        [[ "${rc}" == "0" || "${rc}" == "1" ]] || fail "Ошибка анализа ${elf}, status=${rc}"
    done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0)
    log "runtime closure: проход ${pass}, changed=${changed}"
    [[ "${changed}" == "1" ]] || break
    [[ "${pass}" != "12" ]] || fail "Dependency closure не сошёлся за 12 проходов"
done

# Убираем абсолютные build/install RPATH и задаём только относительные пути.
while IFS= read -r -d '' elf; do
    is_elf "${elf}" || continue
    rel="${elf#${BUNDLE}/}"
    case "${rel}" in
        bin/*) rpath='$ORIGIN/../lib:$ORIGIN/../lib/satdump/plugins' ;;
        lib/satdump/plugins/*) rpath='$ORIGIN/../..:$ORIGIN' ;;
        lib/*) rpath='$ORIGIN:$ORIGIN/satdump/plugins' ;;
        *) continue ;;
    esac
    patchelf --set-rpath "${rpath}" "${elf}"
done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0)

cat > "${BUNDLE}/satdump" <<'EOF2'
#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export SATDUMP_RESOURCES_PATH="${ROOT}/share/satdump/"
export SATDUMP_LIBRARIES_PATH="${ROOT}/lib/satdump/"
export LD_LIBRARY_PATH="${ROOT}/lib:${ROOT}/lib/satdump/plugins${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${ROOT}/bin/satdump" "$@"
EOF2
chmod +x "${BUNDLE}/satdump"

if [[ -x "${BUNDLE}/bin/satdump-ui" ]]; then
cat > "${BUNDLE}/satdump-ui" <<'EOF2'
#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export SATDUMP_RESOURCES_PATH="${ROOT}/share/satdump/"
export SATDUMP_LIBRARIES_PATH="${ROOT}/lib/satdump/"
export LD_LIBRARY_PATH="${ROOT}/lib:${ROOT}/lib/satdump/plugins${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${ROOT}/bin/satdump-ui" "$@"
EOF2
chmod +x "${BUNDLE}/satdump-ui"
fi

cat > "${BUNDLE}/install.sh" <<'EOF2'
#!/usr/bin/env bash
set -Eeuo pipefail
SRC="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
if (( EUID == 0 )); then
    PREFIX="${SATDUMP_PREFIX:-/opt/satdump-1.2.2}"
    BINDIR="${SATDUMP_BINDIR:-/usr/local/bin}"
else
    PREFIX="${SATDUMP_PREFIX:-${HOME}/.local/opt/satdump-1.2.2}"
    BINDIR="${SATDUMP_BINDIR:-${HOME}/.local/bin}"
fi
mkdir -p "${PREFIX}" "${BINDIR}"
cp -a "${SRC}/." "${PREFIX}/"
ln -sfn "${PREFIX}/satdump" "${BINDIR}/satdump"
[[ ! -x "${PREFIX}/satdump-ui" ]] || ln -sfn "${PREFIX}/satdump-ui" "${BINDIR}/satdump-ui"
printf 'SatDump установлен в %s\n' "${PREFIX}"
EOF2
chmod +x "${BUNDLE}/install.sh"

max_symbol() {
    local prefix="$1"
    while IFS= read -r -d '' elf; do
        is_elf "${elf}" || continue
        objdump -T "${elf}" 2>/dev/null || true
    done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0) \
        | grep -o "${prefix}_[0-9][0-9.]*" \
        | sed "s/^${prefix}_//" \
        | sort -Vu | tail -n1
}

GLIBC_REQUIRED="$(max_symbol GLIBC || true)"
GLIBCXX_REQUIRED="$(max_symbol GLIBCXX || true)"
GLIBC_REQUIRED="${GLIBC_REQUIRED:-0}"
GLIBCXX_REQUIRED="${GLIBCXX_REQUIRED:-0}"
dpkg --compare-versions "${GLIBC_REQUIRED}" le "${GLIBC_MAX}" \
    || fail "Бандл требует GLIBC_${GLIBC_REQUIRED}, максимум для Astra 1.7 — GLIBC_${GLIBC_MAX}"

failed=0
while IFS= read -r -d '' elf; do
    is_elf "${elf}" || continue
    dyn="$(readelf -d "${elf}" 2>/dev/null || true)"
    if grep -E '(RPATH|RUNPATH).*(/build|/home|/opt|/usr/local)' <<<"${dyn}" >/dev/null; then
        printf 'Абсолютный RPATH/RUNPATH: %s\n%s\n' "${elf}" "${dyn}" >&2
        failed=1
    fi
    out="$(LD_LIBRARY_PATH="${BUNDLE}/lib:${BUNDLE}/lib/satdump/plugins" ldd "${elf}" 2>&1 || true)"
    if grep -qi 'not found' <<<"${out}"; then
        printf 'Не разрешены зависимости: %s\n%s\n' "${elf}" "${out}" >&2
        failed=1
    fi
done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0)
[[ "${failed}" == "0" ]] || fail "ELF validation failed"

PROBE="$(mktemp -d /tmp/satdump-astra17-probe.XXXXXX)"
trap 'rm -rf "${PROBE}"' EXIT
mkdir -p "${PROBE}/home/.config/satdump" "${PROBE}/cwd"
cat > "${PROBE}/home/.config/satdump/settings.json" <<'EOF2'
{"satdump_general":{"tle_update_interval":{"value":"Never"},"log_to_file":{"value":false}}}
EOF2
(
    cd "${PROBE}/cwd"
    HOME="${PROBE}/home" "${BUNDLE}/satdump" version
) > "${BUNDLE}/astra17-version.log" 2>&1
grep -q 'SatDump v1.2.2' "${BUNDLE}/astra17-version.log" || {
    cat "${BUNDLE}/astra17-version.log" >&2
    fail "CLI smoke-test не прошёл"
}
rm -rf "${PROBE}"
trap - EXIT

PLUGIN_COUNT="$(find "${BUNDLE}/lib/satdump/plugins" -maxdepth 1 -type f -name '*.so*' | wc -l | tr -d ' ')"
{
    printf 'profile=astra17-buster\n'
    printf 'satdump_version=1.2.2\n'
    printf 'bundle_profile=%s\n' "${PROFILE}"
    printf 'architecture=x86_64\n'
    printf 'glibc_build=%s\n' "$(ldd --version 2>&1 | head -n1 | grep -o '[0-9][0-9.]*$')"
    printf 'glibc_required=%s\n' "${GLIBC_REQUIRED}"
    printf 'glibc_allowed_max=%s\n' "${GLIBC_MAX}"
    printf 'glibcxx_required=%s\n' "${GLIBCXX_REQUIRED}"
    printf 'plugin_count=%s\n' "${PLUGIN_COUNT}"
    printf 'compiler=%s\n' "$(g++ --version | head -n1)"
    printf 'source_commit=%s\n' "$(git -C "${SOURCE_DIR}" rev-parse HEAD 2>/dev/null || echo unknown)"
    printf '\n[libraries]\n'
    find "${BUNDLE}/lib" -maxdepth 1 \( -type f -o -type l \) -name '*.so*' -printf '%f -> %l\n' | sort
} > "${BUNDLE}/ASTRA17-MANIFEST.txt"

(
    cd "${BUNDLE}"
    find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
)

EPOCH="${SOURCE_DATE_EPOCH:-$(date +%s)}"
tar --sort=name --mtime="@${EPOCH}" --owner=0 --group=0 --numeric-owner \
    -C "${OUTPUT_DIR}" -cf - "${BASENAME}" | gzip -n > "${ARCHIVE}"
sha256sum "${ARCHIVE}" > "${ARCHIVE}.sha256"
log "Готово: ${ARCHIVE}"
log "GLIBC required=${GLIBC_REQUIRED}; GLIBCXX=${GLIBCXX_REQUIRED}; plugins=${PLUGIN_COUNT}"
