#!/usr/bin/env bash

set -Eeuo pipefail

STAGE=""
BUILD_DIR=""
SOURCE_DIR=""
OUTPUT_DIR=""
PROFILE="reference"
GCC_RUNTIME="/opt/gcc9/lib64"
GLIBC_MAX="2.24"
GLIBC_WARN="2.22"

usage() {
    cat <<'EOF'
Использование: make-bundle.sh --stage PATH --build-dir PATH --source PATH --output PATH [параметры]

Параметры:
  --profile reference|meteor
  --gcc-runtime PATH
  --glibc-max VERSION
  --glibc-warn VERSION
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --stage) STAGE="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --source) SOURCE_DIR="$2"; shift 2 ;;
        --output) OUTPUT_DIR="$2"; shift 2 ;;
        --profile) PROFILE="$2"; shift 2 ;;
        --gcc-runtime) GCC_RUNTIME="$2"; shift 2 ;;
        --glibc-max) GLIBC_MAX="$2"; shift 2 ;;
        --glibc-warn) GLIBC_WARN="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'Неизвестный параметр: %s\n' "$1" >&2; exit 2 ;;
    esac
done

fail() { printf '[bundle] ERROR: %s\n' "$*" >&2; exit 1; }
log() { printf '[bundle] %s\n' "$*"; }
warn() { printf '[bundle] WARNING: %s\n' "$*" >&2; }

[[ -x "${STAGE}/bin/satdump" ]] || fail "Не найден staged satdump: ${STAGE}/bin/satdump"
[[ -d "${STAGE}/share/satdump" ]] || fail "Не найден staged share/satdump"
[[ -d "${STAGE}/lib/satdump" ]] || fail "Не найден staged lib/satdump"
[[ -d "${SOURCE_DIR}" ]] || fail "Не найден source-dir"
[[ -d "${BUILD_DIR}" ]] || fail "Не найден build-dir"
[[ "${PROFILE}" == "reference" || "${PROFILE}" == "meteor" ]] || fail "Некорректный profile"

mkdir -p "${OUTPUT_DIR}"
BASENAME="satdump-1.2.2-presentation-${PROFILE}-glibc224-x86_64"
BUNDLE="${OUTPUT_DIR}/${BASENAME}"
ARCHIVE="${OUTPUT_DIR}/${BASENAME}.tar.gz"

case "${BUNDLE}" in
    /|/usr|/opt|/var|/home|/root) fail "Опасный путь бандла: ${BUNDLE}" ;;
esac
rm -rf "${BUNDLE}" "${ARCHIVE}" "${ARCHIVE}.sha256"
mkdir -p "${BUNDLE}"
cp -a "${STAGE}/." "${BUNDLE}/"
mkdir -p "${BUNDLE}/lib" "${BUNDLE}/lib/satdump/plugins"

KNOWN_FOREIGN_PLUGINS=(
    libaaronia_sdr_support.so
    libearthcare_support.so
    libinsat_support.so
    libbitview_app.so
    libexperimental_devices_support.so
    libkanopus_support.so
    libmetopsg_support.so
    libradiosonde_support.so
    libseawifs_support.so
    libuvsq_support.so
    libxrit_support.so
)
for plugin in "${KNOWN_FOREIGN_PLUGINS[@]}"; do
    [[ ! -e "${BUNDLE}/lib/satdump/plugins/${plugin}" ]] \
        || fail "В staging обнаружен чужой плагин 2.x: ${plugin}"
done

GLIBC_RE='^(ld-linux.*|libc\.|libm\.|libpthread\.|libdl\.|librt\.|libresolv\.|libnsl\.|libnss_|libutil\.|libcrypt\.|libanl\.|libBrokenLocale\.)'
RUNTIME_PATHS="${STAGE}/lib:${STAGE}/lib/satdump/plugins:/usr/local/lib:${GCC_RUNTIME}"

is_elf() {
    readelf -h "$1" >/dev/null 2>&1
}

copy_dependency() {
    local library="$1"
    local name
    [[ -f "${library}" ]] || return 0
    name="$(basename "${library}")"
    [[ ! "${name}" =~ ${GLIBC_RE} ]] || return 0
    [[ -f "${BUNDLE}/lib/${name}" ]] && return 0
    cp -L "${library}" "${BUNDLE}/lib/${name}"
    return 10
}

collect_from_elf() {
    local elf="$1"
    local changed=0
    local library
    while IFS= read -r library; do
        [[ -n "${library}" ]] || continue
        if copy_dependency "${library}"; then
            :
        else
            [[ "$?" == "10" ]] && changed=1 || true
        fi
    done < <(
        LD_LIBRARY_PATH="${BUNDLE}/lib:${BUNDLE}/lib/satdump/plugins:${RUNTIME_PATHS}" \
            ldd "${elf}" 2>/dev/null \
            | awk '
                /=> \/.*/ { print $3; next }
                /^[[:space:]]*\// { print $1 }
            '
    )
    return "${changed}"
}

log "Сбор runtime-зависимостей"
for _pass in 1 2 3 4 5 6; do
    changed=0
    while IFS= read -r -d '' elf; do
        is_elf "${elf}" || continue
        if collect_from_elf "${elf}"; then
            :
        else
            [[ "$?" == "1" ]] && changed=1 || true
        fi
    done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0)
    [[ "${changed}" == "1" ]] || break
done

# Runtime GCC фиксируется явно, даже если конкретная версия ldd не показала одну
# из библиотек при обходе плагинов.
for runtime in libstdc++.so.6 libgcc_s.so.1 libgomp.so.1; do
    if [[ -e "${GCC_RUNTIME}/${runtime}" ]]; then
        cp -L "${GCC_RUNTIME}/${runtime}" "${BUNDLE}/lib/${runtime}"
    fi
done

cat > "${BUNDLE}/satdump" <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export SATDUMP_RESOURCES_PATH="${ROOT}/share/satdump/"
export SATDUMP_LIBRARIES_PATH="${ROOT}/lib/satdump/"
export LD_LIBRARY_PATH="${ROOT}/lib:${ROOT}/lib/satdump/plugins${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${ROOT}/bin/satdump" "$@"
EOF
chmod +x "${BUNDLE}/satdump"

if [[ -x "${BUNDLE}/bin/satdump-ui" ]]; then
    cat > "${BUNDLE}/satdump-ui" <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export SATDUMP_RESOURCES_PATH="${ROOT}/share/satdump/"
export SATDUMP_LIBRARIES_PATH="${ROOT}/lib/satdump/"
export LD_LIBRARY_PATH="${ROOT}/lib:${ROOT}/lib/satdump/plugins${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${ROOT}/bin/satdump-ui" "$@"
EOF
    chmod +x "${BUNDLE}/satdump-ui"
fi

validate_ldd() {
    local failed=0
    local output
    while IFS= read -r -d '' elf; do
        is_elf "${elf}" || continue
        output="$(LD_LIBRARY_PATH="${BUNDLE}/lib:${BUNDLE}/lib/satdump/plugins" ldd "${elf}" 2>&1 || true)"
        if grep -qi 'not found' <<<"${output}"; then
            printf '%s\n%s\n' "Не разрешены зависимости: ${elf}" "${output}" >&2
            failed=1
        fi
    done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0)
    [[ "${failed}" == "0" ]] || fail "Бандл содержит неразрешённые зависимости."
}

max_symbol_version() {
    local prefix="$1"
    local versions
    versions="$(
        while IFS= read -r -d '' elf; do
            is_elf "${elf}" || continue
            objdump -T "${elf}" 2>/dev/null || true
        done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0) \
        | grep -o "${prefix}_[0-9][0-9.]*" \
        | sed "s/^${prefix}_//" \
        | sort -Vu
    )"
    if [[ -n "${versions}" ]]; then
        tail -n1 <<<"${versions}"
    else
        printf '0\n'
    fi
}

validate_ldd
GLIBC_REQUIRED="$(max_symbol_version GLIBC)"
GLIBCXX_REQUIRED="$(max_symbol_version GLIBCXX)"
if ! dpkg --compare-versions "${GLIBC_REQUIRED}" le "${GLIBC_MAX}"; then
    fail "Требуется GLIBC_${GLIBC_REQUIRED}, допустимый максимум GLIBC_${GLIBC_MAX}."
fi
if ! dpkg --compare-versions "${GLIBC_REQUIRED}" le "${GLIBC_WARN}"; then
    warn "Бандл требует GLIBC_${GLIBC_REQUIRED}; проверенный рабочий эталон требовал не выше GLIBC_${GLIBC_WARN}."
fi

# Все runtime-пробы запускаются из пустого каталога. Иначе локальные pipelines,
# settings.json или plugins из исходного дерева могут скрыть дефект relocatable-бандла.
PROBE_ROOT="$(mktemp -d /tmp/satdump-portable-probe.XXXXXX)"
cleanup_probe() { rm -rf "${PROBE_ROOT}"; }
trap cleanup_probe EXIT
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

VERSION_LOG="${BUNDLE}/portable-version.log"
(
    cd "${PROBE_ROOT}/cwd"
    HOME="${PROBE_ROOT}/home" "${BUNDLE}/satdump" version
) >"${VERSION_LOG}" 2>&1
if ! grep -q 'SatDump v1.2.2' "${VERSION_LOG}"; then
    cat "${VERSION_LOG}" >&2
    fail "Portable-бандл не подтвердил версию SatDump 1.2.2."
fi

PROBE_LOG="${BUNDLE}/portable-plugin-probe.log"
(
    cd "${PROBE_ROOT}/cwd"
    HOME="${PROBE_ROOT}/home" "${BUNDLE}/satdump" \
        __portable_plugin_probe__ raw /dev/null "${PROBE_ROOT}/output"
) >"${PROBE_LOG}" 2>&1 || true

if grep -Eqi 'Error loading .*undefined symbol|Error loading .*No such file|cannot open shared object file|not found' "${PROBE_LOG}"; then
    grep -Ei 'Error loading|undefined symbol|No such file|cannot open shared object file|not found' "${PROBE_LOG}" >&2 || true
    fail "Проверка загрузки плагинов обнаружила ABI/линковочную ошибку."
fi

PLUGIN_COUNT="$(find "${BUNDLE}/lib/satdump/plugins" -maxdepth 1 -type f -name '*.so' | wc -l | tr -d ' ')"
if [[ "${PROFILE}" == "reference" && "${PLUGIN_COUNT}" -lt 40 ]]; then
    fail "Reference-профиль собрал только ${PLUGIN_COUNT} плагинов; ожидается не менее 40."
fi
if [[ "${PROFILE}" == "meteor" && "${PLUGIN_COUNT}" -lt 4 ]]; then
    fail "Meteor-профиль собрал только ${PLUGIN_COUNT} плагинов."
fi

GIT_SHA="$(git -C "${SOURCE_DIR}" rev-parse HEAD 2>/dev/null || printf unknown)"
GIT_BRANCH="$(git -C "${SOURCE_DIR}" rev-parse --abbrev-ref HEAD 2>/dev/null || printf archive)"
SOURCE_EPOCH="${SOURCE_DATE_EPOCH:-$(date +%s)}"
NNG_VERSION="$(awk -F= '$1 == "version" { print $2 }' /usr/local/share/satdump-portable/nng.lock 2>/dev/null || true)"
NNG_COMMIT="$(awk -F= '$1 == "commit" { print $2 }' /usr/local/share/satdump-portable/nng.lock 2>/dev/null || true)"

{
    printf 'profile=satdump-portable-glibc224\n'
    printf 'plugin_profile=%s\n' "${PROFILE}"
    printf 'source_commit=%s\n' "${GIT_SHA}"
    printf 'source_branch=%s\n' "${GIT_BRANCH}"
    printf 'source_date_epoch=%s\n' "${SOURCE_EPOCH}"
    printf 'glibc_build=%s\n' "$(ldd --version 2>&1 | head -n1 | grep -o '[0-9][0-9.]*$' || printf unknown)"
    printf 'glibc_required=%s\n' "${GLIBC_REQUIRED}"
    printf 'glibc_reference_max=%s\n' "${GLIBC_WARN}"
    printf 'glibc_allowed_max=%s\n' "${GLIBC_MAX}"
    printf 'glibcxx_required=%s\n' "${GLIBCXX_REQUIRED}"
    printf 'plugin_count=%s\n' "${PLUGIN_COUNT}"
    printf 'compiler=%s\n' "$(/opt/gcc9/bin/g++ --version | head -n1)"
    printf 'cmake=%s\n' "$(cmake --version | head -n1)"
    printf 'nng_version=%s\n' "${NNG_VERSION:-unknown}"
    printf 'nng_commit=%s\n' "${NNG_COMMIT:-unknown}"
    printf '\n[plugins]\n'
    find "${BUNDLE}/lib/satdump/plugins" -maxdepth 1 -type f -name '*.so' -printf '%f\n' | sort
    printf '\n[libraries]\n'
    find "${BUNDLE}/lib" -maxdepth 1 -type f -name '*.so*' -printf '%f\n' | sort
} > "${BUNDLE}/PORTABLE-MANIFEST.txt"

(
    cd "${BUNDLE}"
    find . -type f ! -name 'SHA256SUMS' -print0 \
        | sort -z \
        | xargs -0 sha256sum > SHA256SUMS
)

log "Создание воспроизводимого архива ${ARCHIVE}"
tar --sort=name \
    --mtime="@${SOURCE_EPOCH}" \
    --owner=0 --group=0 --numeric-owner \
    -C "${OUTPUT_DIR}" -cf - "${BASENAME}" \
    | gzip -n > "${ARCHIVE}"
(
    cd "${OUTPUT_DIR}"
    sha256sum "${BASENAME}.tar.gz" > "${BASENAME}.tar.gz.sha256"
    sha256sum -c "${BASENAME}.tar.gz.sha256"
)

log "Готово: ${ARCHIVE}"
log "Требуемая glibc: ${GLIBC_REQUIRED}; GLIBCXX: ${GLIBCXX_REQUIRED}; плагины: ${PLUGIN_COUNT}"
