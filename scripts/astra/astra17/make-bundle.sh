#!/usr/bin/env bash
set -Eeuo pipefail

STAGE=""
SOURCE_DIR=""
OUTPUT_DIR=""
PROFILE="desktop"
RUNTIME_PREFIX=""
REVISION="local"
GLIBC_MAX="2.28"

log() { printf '[astra17/bundle] %s\n' "$*"; }
fail() { printf '[astra17/bundle] ERROR: %s\n' "$*" >&2; exit 1; }

while (( $# > 0 )); do
    case "$1" in
        --stage) STAGE="$2"; shift 2 ;;
        --source) SOURCE_DIR="$2"; shift 2 ;;
        --output) OUTPUT_DIR="$2"; shift 2 ;;
        --profile) PROFILE="$2"; shift 2 ;;
        --runtime-prefix) RUNTIME_PREFIX="$2"; shift 2 ;;
        --revision) REVISION="$2"; shift 2 ;;
        *) fail "Неизвестный параметр: $1" ;;
    esac
done

[[ -x "${STAGE}/bin/satdump" ]] || fail "Нет ${STAGE}/bin/satdump"
[[ -d "${SOURCE_DIR}" ]] || fail "Нет source-dir"
[[ "${PROFILE}" == "headless" || "${PROFILE}" == "desktop" ]] || fail "Некорректный профиль"
for command in patchelf readelf objdump ldd sha256sum tar gzip; do
    command -v "${command}" >/dev/null 2>&1 || fail "Нужна команда ${command}"
done

ASTRA_VERSION="unknown"
if [[ -r /etc/astra_version ]]; then
    ASTRA_VERSION="$(tr -d '\r\n' < /etc/astra_version)"
elif [[ -r /etc/os-release ]]; then
    ASTRA_VERSION="$(. /etc/os-release; printf '%s' "${PRETTY_NAME:-${VERSION_ID:-unknown}}")"
fi
GLIBC_BUILD="$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}' || true)"
[[ "${GLIBC_BUILD}" == "2.28" ]] || fail "Bundle должен формироваться на glibc 2.28, получено ${GLIBC_BUILD:-unknown}"

mkdir -p "${OUTPUT_DIR}"
BASENAME="satdump-1.2.2-astra17-${PROFILE}-full-x86_64"
BUNDLE="${OUTPUT_DIR}/${BASENAME}"
ARCHIVE="${OUTPUT_DIR}/${BASENAME}.tar.gz"
rm -rf "${BUNDLE}" "${ARCHIVE}" "${ARCHIVE}.sha256"
mkdir -p "${BUNDLE}"
cp -a "${STAGE}/." "${BUNDLE}/"
mkdir -p "${BUNDLE}/lib" "${BUNDLE}/lib/satdump/plugins" "${BUNDLE}/share/doc/satdump"

if [[ -d "${SOURCE_DIR}/docs/ru" ]]; then
    cp -a "${SOURCE_DIR}/docs/ru/." "${BUNDLE}/share/doc/satdump/"
fi

RUNTIME_RECORDS="${BUNDLE}/.runtime-libraries.unsorted"
: > "${RUNTIME_RECORDS}"

SEARCH_PATHS="${BUNDLE}/lib:${BUNDLE}/lib/satdump/plugins"
if [[ -n "${RUNTIME_PREFIX}" ]]; then
    SEARCH_PATHS+="${SEARCH_PATHS:+:}${RUNTIME_PREFIX}/lib:${RUNTIME_PREFIX}/lib64"
fi
SEARCH_PATHS+="/usr/local/lib:/usr/local/lib64:/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu:/usr/lib64:/lib64:/usr/lib:/lib"

is_elf() { readelf -h "$1" >/dev/null 2>&1; }
file_sha() { sha256sum "$1" | awk '{print $1}'; }

record_runtime() {
    local name="$1" source="$2" real="$3" package="unowned"
    if command -v dpkg-query >/dev/null 2>&1; then
        package="$(dpkg-query -S "${real}" 2>/dev/null | sed -n '1{s/:.*//;p}' || true)"
        package="${package:-unowned}"
    fi
    printf '%s\t%s\t%s\t%s\n' "${name}" "$(basename "${real}")" "${package}" "${source}" >> "${RUNTIME_RECORDS}"
}

# return 10: создан новый alias.
ensure_link() {
    local name="$1" target="$2" dst="${BUNDLE}/lib/${name}"
    [[ -n "${name}" && "${name}" != "${target}" ]] || return 0
    if [[ -L "${dst}" ]]; then
        [[ "$(readlink "${dst}")" == "${target}" ]] \
            || fail "Конфликт symlink ${name}: $(readlink "${dst}") vs ${target}"
        return 0
    fi
    if [[ -e "${dst}" ]]; then
        [[ "$(file_sha "${dst}")" == "$(file_sha "${BUNDLE}/lib/${target}")" ]] \
            || fail "Конфликт runtime-файла ${name}"
        return 0
    fi
    ln -s "${target}" "${dst}"
    return 10
}

# Копируем РЕАЛЬНЫЙ файл и восстанавливаем как DT_NEEDED-имя, так и SONAME.
# Ничего из glibc больше не исключается: release содержит libc, loader и NSS.
copy_dependency() {
    local needed="$1" resolved="$2"
    [[ -n "${needed}" && -e "${resolved}" ]] || return 0

    local real realname dest soname rc added=0
    real="$(readlink -f "${resolved}")"
    [[ -f "${real}" ]] || return 0
    realname="$(basename "${real}")"
    dest="${BUNDLE}/lib/${realname}"

    if [[ -e "${dest}" && ! -L "${dest}" ]]; then
        [[ "$(file_sha "${dest}")" == "$(file_sha "${real}")" ]] \
            || fail "Разные библиотеки претендуют на ${realname}: ${resolved}"
    else
        rm -f "${dest}"
        cp -p "${real}" "${dest}"
        record_runtime "${needed}" "${resolved}" "${real}"
        added=1
    fi

    if ensure_link "${needed}" "${realname}"; then rc=0; else rc=$?; fi
    [[ "${rc}" == "10" ]] && added=1
    [[ "${rc}" == "0" || "${rc}" == "10" ]] || return "${rc}"

    soname="$(readelf -d "${real}" 2>/dev/null | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p' | sed -n '1p')"
    if [[ -n "${soname}" ]]; then
        if ensure_link "${soname}" "${realname}"; then rc=0; else rc=$?; fi
        [[ "${rc}" == "10" ]] && added=1
        [[ "${rc}" == "0" || "${rc}" == "10" ]] || return "${rc}"
    fi

    (( added == 0 )) || return 10
    return 0
}

# return 1: closure расширился.
collect_from_elf() {
    local elf="$1" changed=0 needed resolved rc
    while IFS=$'\t' read -r needed resolved; do
        [[ -n "${needed}" && -n "${resolved}" ]] || continue
        if copy_dependency "${needed}" "${resolved}"; then rc=0; else rc=$?; fi
        [[ "${rc}" == "10" ]] && changed=1
        [[ "${rc}" == "0" || "${rc}" == "10" ]] || return "${rc}"
    done < <(
        LD_LIBRARY_PATH="${SEARCH_PATHS}" ldd "${elf}" 2>/dev/null \
            | awk '
                /=> \/.*/ { print $1 "\t" $3; next }
                /^[[:space:]]*\// {
                    path=$1; name=path; sub(/^.*\//, "", name);
                    print name "\t" path
                }
            '
    )
    return "${changed}"
}

log "Рекурсивное построение полного runtime closure на Astra Linux 1.7"
for pass in $(seq 1 16); do
    changed=0
    while IFS= read -r -d '' elf; do
        is_elf "${elf}" || continue
        if collect_from_elf "${elf}"; then rc=0; else rc=$?; fi
        [[ "${rc}" == "1" ]] && changed=1
        [[ "${rc}" == "0" || "${rc}" == "1" ]] || fail "Ошибка анализа ${elf}, status=${rc}"
    done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0)
    log "runtime closure: проход ${pass}, changed=${changed}"
    [[ "${changed}" == "1" ]] || break
    [[ "${pass}" != "16" ]] || fail "Dependency closure не сошёлся за 16 проходов"
done

# glibc загружает NSS динамически, поэтому эти модули не всегда видны через ldd.
for nss in \
    /lib/x86_64-linux-gnu/libnss_*.so.2 \
    /usr/lib/x86_64-linux-gnu/libnss_*.so.2 \
    /lib64/libnss_*.so.2; do
    [[ -e "${nss}" ]] || continue
    if copy_dependency "$(basename "${nss}")" "${nss}"; then :; else rc=$?; [[ "${rc}" == "10" ]] || exit "${rc}"; fi
done

# NSS мог добавить свои зависимости — один дополнительный closure-проход.
while IFS= read -r -d '' elf; do
    is_elf "${elf}" || continue
    if collect_from_elf "${elf}"; then :; else rc=$?; [[ "${rc}" == "1" ]] || exit "${rc}"; fi
done < <(find "${BUNDLE}/lib" -type f -print0)

for required_runtime in libc.so.6 ld-linux-x86-64.so.2 libstdc++.so.6 libgcc_s.so.1; do
    [[ -e "${BUNDLE}/lib/${required_runtime}" ]] \
        || fail "В полном release отсутствует обязательный runtime ${required_runtime}"
done

# RPATH меняется только у ELF SatDump. Системные runtime-библиотеки Astra не патчим.
while IFS= read -r -d '' elf; do
    is_elf "${elf}" || continue
    rel="${elf#${BUNDLE}/}"
    case "${rel}" in
        bin/*) rpath='$ORIGIN/../lib:$ORIGIN/../lib/satdump/plugins' ;;
        lib/libsatdump*.so*) rpath='$ORIGIN:$ORIGIN/satdump/plugins' ;;
        lib/satdump/plugins/*) rpath='$ORIGIN/../..:$ORIGIN' ;;
        *) continue ;;
    esac
    patchelf --set-rpath "${rpath}" "${elf}"
done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0)

cat > "${BUNDLE}/satdump" <<'EOF2'
#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
LOADER="${ROOT}/lib/ld-linux-x86-64.so.2"
[[ -x "${LOADER}" ]] || { printf 'Bundled loader not found: %s\n' "${LOADER}" >&2; exit 127; }
export SATDUMP_RESOURCES_PATH="${ROOT}/share/satdump/"
export SATDUMP_LIBRARIES_PATH="${ROOT}/lib/satdump/"
exec "${LOADER}" \
    --library-path "${ROOT}/lib:${ROOT}/lib/satdump/plugins" \
    "${ROOT}/bin/satdump" "$@"
EOF2
chmod +x "${BUNDLE}/satdump"

if [[ -x "${BUNDLE}/bin/satdump-ui" ]]; then
cat > "${BUNDLE}/satdump-ui" <<'EOF2'
#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
LOADER="${ROOT}/lib/ld-linux-x86-64.so.2"
[[ -x "${LOADER}" ]] || { printf 'Bundled loader not found: %s\n' "${LOADER}" >&2; exit 127; }
export SATDUMP_RESOURCES_PATH="${ROOT}/share/satdump/"
export SATDUMP_LIBRARIES_PATH="${ROOT}/lib/satdump/"
exec "${LOADER}" \
    --library-path "${ROOT}/lib:${ROOT}/lib/satdump/plugins" \
    "${ROOT}/bin/satdump-ui" "$@"
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
printf 'SatDump 1.2.2 установлен в %s\n' "${PREFIX}"
printf 'Дополнительные пакеты APT для SatDump не требуются.\n'
EOF2
chmod +x "${BUNDLE}/install.sh"

cat > "${BUNDLE}/verify.sh" <<'EOF2'
#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
for file in libc.so.6 ld-linux-x86-64.so.2 libstdc++.so.6 libgcc_s.so.1; do
    [[ -e "${ROOT}/lib/${file}" ]] || { echo "missing ${file}" >&2; exit 1; }
done
HOME_DIR="$(mktemp -d "${TMPDIR:-/tmp}/satdump-verify.XXXXXX")"
trap 'rm -rf "${HOME_DIR}"' EXIT
mkdir -p "${HOME_DIR}/.config/satdump"
printf '%s\n' '{"satdump_general":{"tle_update_interval":{"value":"Never"},"log_to_file":{"value":false}}}' \
    > "${HOME_DIR}/.config/satdump/settings.json"
HOME="${HOME_DIR}" "${ROOT}/satdump" version 2>&1 | grep -q 'SatDump v1.2.2'
failed=0
while IFS= read -r -d '' elf; do
    readelf -h "${elf}" >/dev/null 2>&1 || continue
    out="$(LD_LIBRARY_PATH="${ROOT}/lib:${ROOT}/lib/satdump/plugins" ldd -r "${elf}" 2>&1 || true)"
    if grep -Eqi 'not found|undefined symbol' <<<"${out}"; then
        printf 'ELF runtime failure: %s\n%s\n' "${elf}" "${out}" >&2
        failed=1
    fi
done < <(find "${ROOT}/bin" "${ROOT}/lib" -type f -print0)
test "${failed}" = 0
echo 'SatDump Astra 1.7 bundle: OK'
EOF2
chmod +x "${BUNDLE}/verify.sh"

cat > "${BUNDLE}/README-ASTRA17.txt" <<'EOF2'
SatDump 1.2.2 — полный пакет для Astra Linux 1.7 x86_64

Быстрый запуск без установки:
  ./verify.sh
  ./satdump version
  ./satdump-ui

Установка:
  ./install.sh

Пакет содержит runtime-библиотеки приложения, включая glibc/loader, C++ runtime,
GUI, аудио и RTL-SDR зависимости. Устанавливать дополнительные APT-пакеты для
SatDump не требуется. Системные kernel/device/graphics drivers предоставляет ОС.

Полная русская документация: share/doc/satdump/ASTRA17_COMPLETE_GUIDE.md
EOF2

max_symbol() {
    local prefix="$1"
    {
        while IFS= read -r -d '' elf; do
            is_elf "${elf}" || continue
            objdump -T "${elf}" 2>/dev/null || true
        done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0)
    } | grep -o "${prefix}_[0-9][0-9.]*" \
      | sed "s/^${prefix}_//" \
      | sort -Vu \
      | tail -n1 || true
}

GLIBC_REQUIRED="$(max_symbol GLIBC)"
GLIBCXX_REQUIRED="$(max_symbol GLIBCXX)"
GLIBC_REQUIRED="${GLIBC_REQUIRED:-0}"
GLIBCXX_REQUIRED="${GLIBCXX_REQUIRED:-0}"
dpkg --compare-versions "${GLIBC_REQUIRED}" le "${GLIBC_MAX}" \
    || fail "Release требует GLIBC_${GLIBC_REQUIRED}, максимум Astra 1.7 — GLIBC_${GLIBC_MAX}"

# Проверяем, что все DT_NEEDED, кроме виртуального vdso и PT_INTERP, закрываются
# файлами внутри release-пакета.
failed=0
while IFS= read -r -d '' elf; do
    is_elf "${elf}" || continue
    out="$(LD_LIBRARY_PATH="${BUNDLE}/lib:${BUNDLE}/lib/satdump/plugins:${SEARCH_PATHS}" ldd -r "${elf}" 2>&1 || true)"
    if grep -Eqi 'not found|undefined symbol' <<<"${out}"; then
        printf 'Неразрешённый ELF: %s\n%s\n' "${elf}" "${out}" >&2
        failed=1
    fi
    while IFS= read -r resolved; do
        [[ -n "${resolved}" ]] || continue
        case "${resolved}" in
            "${BUNDLE}/lib/"*) ;;
            *) printf 'Внешняя runtime-библиотека: %s -> %s\n' "${elf}" "${resolved}" >&2; failed=1 ;;
        esac
    done < <(awk '/=> \/.*/ {print $3}' <<<"${out}")
done < <(find "${BUNDLE}/bin" "${BUNDLE}/lib" -type f -print0)
[[ "${failed}" == "0" ]] || fail "Runtime closure не является самодостаточным"

sort -u "${RUNTIME_RECORDS}" > "${BUNDLE}/RUNTIME-LIBRARIES.txt"
rm -f "${RUNTIME_RECORDS}"
LIBRARY_COUNT="$(find "${BUNDLE}/lib" -maxdepth 1 \( -type f -o -type l \) -name '*.so*' | wc -l | tr -d ' ')"
PLUGIN_COUNT="$(find "${BUNDLE}/lib/satdump/plugins" -maxdepth 1 -type f -name '*.so*' | wc -l | tr -d ' ')"

{
    printf 'profile=astra17-native-full\n'
    printf 'satdump_version=1.2.2\n'
    printf 'release_revision=%s\n' "${REVISION}"
    printf 'bundle_profile=%s\n' "${PROFILE}"
    printf 'architecture=x86_64\n'
    printf 'build_os=%s\n' "${ASTRA_VERSION}"
    printf 'glibc_build=%s\n' "${GLIBC_BUILD}"
    printf 'glibc_bundled=yes\n'
    printf 'glibc_required=%s\n' "${GLIBC_REQUIRED}"
    printf 'glibc_allowed_max=%s\n' "${GLIBC_MAX}"
    printf 'glibcxx_required=%s\n' "${GLIBCXX_REQUIRED}"
    printf 'runtime_closure=complete\n'
    printf 'runtime_library_count=%s\n' "${LIBRARY_COUNT}"
    printf 'plugin_count=%s\n' "${PLUGIN_COUNT}"
    printf 'compiler=%s\n' "$(g++ --version | sed -n '1p')"
    printf 'source_commit=%s\n' "$(git -C "${SOURCE_DIR}" rev-parse HEAD 2>/dev/null || echo unknown)"
} > "${BUNDLE}/ASTRA17-MANIFEST.txt"

PROBE="$(mktemp -d "${TMPDIR:-/tmp}/satdump-astra17-probe.XXXXXX")"
trap 'rm -rf "${PROBE}"' EXIT
mkdir -p "${PROBE}/home/.config/satdump" "${PROBE}/cwd"
printf '%s\n' '{"satdump_general":{"tle_update_interval":{"value":"Never"},"log_to_file":{"value":false}}}' \
    > "${PROBE}/home/.config/satdump/settings.json"
(
    cd "${PROBE}/cwd"
    HOME="${PROBE}/home" "${BUNDLE}/satdump" version
) > "${BUNDLE}/astra17-version.log" 2>&1
grep -q 'SatDump v1.2.2' "${BUNDLE}/astra17-version.log" || {
    cat "${BUNDLE}/astra17-version.log" >&2
    fail "CLI smoke-test с bundled loader не прошёл"
}
rm -rf "${PROBE}"
trap - EXIT

(
    cd "${BUNDLE}"
    find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
)

EPOCH="${SOURCE_DATE_EPOCH:-$(date +%s)}"
tar --sort=name --mtime="@${EPOCH}" --owner=0 --group=0 --numeric-owner \
    -C "${OUTPUT_DIR}" -cf - "${BASENAME}" | gzip -n > "${ARCHIVE}"
(
    cd "${OUTPUT_DIR}"
    sha256sum "$(basename "${ARCHIVE}")" > "$(basename "${ARCHIVE}").sha256"
)

log "Готов полный Astra 1.7 release: ${ARCHIVE}"
log "runtime libs=${LIBRARY_COUNT}; plugins=${PLUGIN_COUNT}; GLIBC=${GLIBC_REQUIRED}; GLIBCXX=${GLIBCXX_REQUIRED}"
