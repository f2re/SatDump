#!/usr/bin/env bash

# Script for creating a relocatable offline installation bundle of SatDump for Astra Linux.
# Bundles binaries, shared libraries (.so), plugins, resources, wrappers, and an embedded installer.

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SATDUMP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

if [[ -f "${SCRIPT_DIR}/common.sh" ]]; then
    source "${SCRIPT_DIR}/common.sh"
else
    log_info() { printf 'ℹ %s\n' "$*"; }
    log_ok() { printf '✔ %s\n' "$*"; }
    log_warn() { printf '⚠ %s\n' "$*" >&2; }
    log_error() { printf '✖ %s\n' "$*" >&2; }
    die() { log_error "$*"; exit 1; }
    command_exists() { command -v "$1" >/dev/null 2>&1; }
    detect_astra_version() { printf 'unknown\n'; }
fi

BUILD_DIR=""
SOURCE_DIR="${SATDUMP_ROOT}"
OUTPUT_DIR="${SATDUMP_ROOT}/dist"
PROFILE="auto"
VERSION=""
ARCH="$(uname -m)"

usage() {
    cat <<EOF
Использование: bash scripts/astra/create-offline-bundle.sh [параметры]

Параметры:
  --build-dir PATH    Каталог собранных файлов SatDump (например, build/astra-1.7-desktop)
  --source-dir PATH   Каталог исходных кодов SatDump (по умолчанию: ${SATDUMP_ROOT})
  --output-dir PATH   Каталог назначения для готового архива (по умолчанию: ${SATDUMP_ROOT}/dist)
  --profile PROFILE   Профиль сборки (headless, desktop, full; по умолчанию: auto)
  --version VERSION   Версия SatDump (автоопределяется при отсутствии)
  --arch ARCH         Архитектура системы (по умолчанию: ${ARCH})
  -h, --help          Показать эту справку

Пример:
  bash scripts/astra/create-offline-bundle.sh --build-dir build/astra-1.7-desktop --output-dir dist
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --source-dir) SOURCE_DIR="$2"; shift 2 ;;
        --output-dir|--output) OUTPUT_DIR="$2"; shift 2 ;;
        --profile) PROFILE="$2"; shift 2 ;;
        --version) VERSION="$2"; shift 2 ;;
        --arch) ARCH="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "Неизвестный параметр: $1" ;;
    esac
done

# 1. Автоматический поиск каталога сборки, если не указан
if [[ -z "${BUILD_DIR}" ]]; then
    ASTRA_VER="$(detect_astra_version)"
    for candidate in \
        "${SATDUMP_ROOT}/build/astra-${ASTRA_VER}-desktop" \
        "${SATDUMP_ROOT}/build/astra-${ASTRA_VER}-headless" \
        "${SATDUMP_ROOT}/build/astra-${ASTRA_VER}-full" \
        "${SATDUMP_ROOT}/build"; do
        if [[ -d "${candidate}" ]] && { [[ -x "${candidate}/satdump" ]] || [[ -x "${candidate}/bin/satdump" ]] || [[ -x "${candidate}/src-cli/satdump" ]]; }; then
            BUILD_DIR="${candidate}"
            break
        fi
    done
fi

[[ -n "${BUILD_DIR}" ]] || die "Не найден каталог сборки SatDump. Укажите --build-dir или выполните сборку."
[[ -d "${BUILD_DIR}" ]] || die "Указанный каталог сборки не существует: ${BUILD_DIR}"
[[ -d "${SOURCE_DIR}" ]] || die "Каталог исходных кодов не существует: ${SOURCE_DIR}"

# 2. Определение исполняемых файлов в каталоге сборки
SATDUMP_BIN=""
SATDUMP_UI_BIN=""
SATDUMP_CORE_SO=""

if [[ -x "${BUILD_DIR}/bin/satdump" ]]; then
    SATDUMP_BIN="${BUILD_DIR}/bin/satdump"
elif [[ -x "${BUILD_DIR}/satdump" ]]; then
    SATDUMP_BIN="${BUILD_DIR}/satdump"
elif [[ -x "${BUILD_DIR}/src-cli/satdump" ]]; then
    SATDUMP_BIN="${BUILD_DIR}/src-cli/satdump"
fi

if [[ -x "${BUILD_DIR}/bin/satdump-ui" ]]; then
    SATDUMP_UI_BIN="${BUILD_DIR}/bin/satdump-ui"
elif [[ -x "${BUILD_DIR}/satdump-ui" ]]; then
    SATDUMP_UI_BIN="${BUILD_DIR}/satdump-ui"
elif [[ -x "${BUILD_DIR}/src-ui/satdump-ui" ]]; then
    SATDUMP_UI_BIN="${BUILD_DIR}/src-ui/satdump-ui"
fi

if [[ -f "${BUILD_DIR}/lib/libsatdump_core.so" ]]; then
    SATDUMP_CORE_SO="${BUILD_DIR}/lib/libsatdump_core.so"
elif [[ -f "${BUILD_DIR}/libsatdump_core.so" ]]; then
    SATDUMP_CORE_SO="${BUILD_DIR}/libsatdump_core.so"
elif [[ -f "${BUILD_DIR}/src-core/libsatdump_core.so" ]]; then
    SATDUMP_CORE_SO="${BUILD_DIR}/src-core/libsatdump_core.so"
fi

[[ -n "${SATDUMP_BIN}" ]] || die "Исполняемый файл satdump не найден в ${BUILD_DIR}."

# 3. Определение версии
if [[ -z "${VERSION}" ]]; then
    if [[ -f "${SOURCE_DIR}/CMakeLists.txt" ]]; then
        VERSION="$(grep -i 'project(SatDump VERSION' "${SOURCE_DIR}/CMakeLists.txt" 2>/dev/null | grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+' || true)"
    fi
    if [[ -z "${VERSION}" ]]; then
        VERSION="1.2.2"
    fi
fi

if [[ "${PROFILE}" == "auto" ]]; then
    if [[ -n "${SATDUMP_UI_BIN}" ]]; then
        PROFILE="desktop"
    else
        PROFILE="headless"
    fi
fi

log_info "Формирование оффлайн-бандла SatDump v${VERSION} (${ARCH}, профиль: ${PROFILE})"
log_info "Каталог сборки: ${BUILD_DIR}"

# 4. Создание временного каталога для сборки бандла
STAGING_DIR="$(mktemp -d "${TMPDIR:-/tmp}/satdump-bundle.XXXXXX")"
cleanup() { rm -rf "${STAGING_DIR}"; }
trap cleanup EXIT

BASENAME="satdump-${VERSION}-offline-${ARCH}"
BUNDLE_DIR="${STAGING_DIR}/${BASENAME}"

mkdir -p "${BUNDLE_DIR}/bin"
mkdir -p "${BUNDLE_DIR}/lib"
mkdir -p "${BUNDLE_DIR}/lib/satdump/plugins"
mkdir -p "${BUNDLE_DIR}/share/satdump"
mkdir -p "${BUNDLE_DIR}/share/applications"

# Копируем основные бинарные файлы
cp -a "${SATDUMP_BIN}" "${BUNDLE_DIR}/bin/satdump"
if [[ -n "${SATDUMP_UI_BIN}" ]]; then
    cp -a "${SATDUMP_UI_BIN}" "${BUNDLE_DIR}/bin/satdump-ui"
fi

if [[ -n "${SATDUMP_CORE_SO}" ]]; then
    cp -a "${SATDUMP_CORE_SO}" "${BUNDLE_DIR}/lib/libsatdump_core.so"
fi

# Копируем плагины SatDump
PLUGINS_FOUND=0
for pdir in "${BUILD_DIR}/lib/satdump/plugins" "${BUILD_DIR}/plugins" "${BUILD_DIR}/plugins/*"; do
    if [[ -d "${pdir}" ]]; then
        while IFS= read -r -d '' plugin_file; do
            pname="$(basename "${plugin_file}")"
            if [[ "${pname}" != "libsatdump_core.so" ]]; then
                cp -a "${plugin_file}" "${BUNDLE_DIR}/lib/satdump/plugins/"
                PLUGINS_FOUND=$((PLUGINS_FOUND + 1))
            fi
        done < <(find "${pdir}" -maxdepth 2 -type f -name '*.so' -print0 2>/dev/null)
    fi
done
log_ok "Скопировано плагинов: ${PLUGINS_FOUND}"

# Копируем ресурсы и конфигурационные файлы
[[ -f "${SOURCE_DIR}/satdump_cfg.json" ]] && cp -a "${SOURCE_DIR}/satdump_cfg.json" "${BUNDLE_DIR}/share/satdump/"
[[ -d "${SOURCE_DIR}/pipelines" ]] && cp -a "${SOURCE_DIR}/pipelines" "${BUNDLE_DIR}/share/satdump/"
[[ -d "${SOURCE_DIR}/resources" ]] && cp -a "${SOURCE_DIR}/resources" "${BUNDLE_DIR}/share/satdump/"
[[ -f "${SOURCE_DIR}/icon.png" ]] && cp -a "${SOURCE_DIR}/icon.png" "${BUNDLE_DIR}/share/satdump/"
[[ -f "${SOURCE_DIR}/satdump.desktop" ]] && cp -a "${SOURCE_DIR}/satdump.desktop" "${BUNDLE_DIR}/share/applications/"

# 5. Рекурсивный сбор динамических библиотек (.so) через ldd
GLIBC_RE='^(ld-linux.*|libc\.|libm\.|libpthread\.|libdl\.|librt\.|libresolv\.|libnsl\.|libnss_|libutil\.|libcrypt\.|libanl\.|libBrokenLocale\.)'
DEPS_PREFIX="${ASTRA_DEPS_PREFIX:-${HOME}/.local/opt/satdump-astra/deps}"
SEARCH_PATHS="${BUNDLE_DIR}/lib:${BUNDLE_DIR}/lib/satdump/plugins:${BUILD_DIR}:${DEPS_PREFIX}/lib:${DEPS_PREFIX}/lib64:${LD_LIBRARY_PATH:-}"

is_elf() {
    readelf -h "$1" >/dev/null 2>&1
}

copy_dep() {
    local libpath="$1"
    local libname
    [[ -f "${libpath}" ]] || return 0
    libname="$(basename "${libpath}")"
    [[ ! "${libname}" =~ ${GLIBC_RE} ]] || return 0
    [[ -f "${BUNDLE_DIR}/lib/${libname}" ]] && return 0
    cp -L "${libpath}" "${BUNDLE_DIR}/lib/${libname}"
    return 10
}

collect_deps_from_elf() {
    local elf="$1"
    local changed=0
    local lib
    while IFS= read -r lib; do
        [[ -n "${lib}" ]] || continue
        if copy_dep "${lib}"; then
            :
        else
            [[ "$?" == "10" ]] && changed=1 || true
        fi
    done < <(
        LD_LIBRARY_PATH="${SEARCH_PATHS}" ldd "${elf}" 2>/dev/null \
            | awk '/=> \/.*/ { print $3; next } /^[[:space:]]*\// { print $1 }'
    )
    return "${changed}"
}

log_info "Рекурсивный анализ и сбор системных библиотек (ldd)..."
for pass in 1 2 3 4 5 6 7 8; do
    changed=0
    while IFS= read -r -d '' elf; do
        is_elf "${elf}" || continue
        if collect_deps_from_elf "${elf}"; then
            :
        else
            [[ "$?" == "1" ]] && changed=1 || true
        fi
    done < <(find "${BUNDLE_DIR}/bin" "${BUNDLE_DIR}/lib" -type f -print0)
    [[ "${changed}" == "1" ]] || break
done

LIBS_COPIED="$(find "${BUNDLE_DIR}/lib" -maxdepth 1 -type f -name '*.so*' | wc -l | tr -d ' ')"
log_ok "Собрано динамических библиотек (.so): ${LIBS_COPIED}"

# 6. Создание relocatable wrapper-скриптов
cat > "${BUNDLE_DIR}/satdump" <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export SATDUMP_RESOURCES_PATH="${ROOT}/share/satdump/"
export SATDUMP_LIBRARIES_PATH="${ROOT}/lib/satdump/"
export LD_LIBRARY_PATH="${ROOT}/lib:${ROOT}/lib/satdump/plugins${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${ROOT}/bin/satdump" "$@"
EOF
chmod +x "${BUNDLE_DIR}/satdump"

if [[ -f "${BUNDLE_DIR}/bin/satdump-ui" ]]; then
    cat > "${BUNDLE_DIR}/satdump-ui" <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export SATDUMP_RESOURCES_PATH="${ROOT}/share/satdump/"
export SATDUMP_LIBRARIES_PATH="${ROOT}/lib/satdump/"
export LD_LIBRARY_PATH="${ROOT}/lib:${ROOT}/lib/satdump/plugins${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${ROOT}/bin/satdump-ui" "$@"
EOF
    chmod +x "${BUNDLE_DIR}/satdump-ui"
fi

# 7. Встраивание автономированного офлайн-инсталлятора (install.sh)
cat > "${BUNDLE_DIR}/install.sh" <<'EOF'
#!/usr/bin/env bash

# Автономный скрипт установки SatDump из офлайн-бандла.

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"

if [[ -t 1 && "${NO_COLOR:-0}" != "1" ]]; then
    C_RESET=$'\033[0m'
    C_INFO=$'\033[1;36m'
    C_OK=$'\033[1;32m'
    C_WARN=$'\033[1;33m'
    C_ERR=$'\033[1;31m'
else
    C_RESET="" C_INFO="" C_OK="" C_WARN="" C_ERR=""
fi

log_info() { printf '%sℹ%s %s\n' "${C_INFO}" "${C_RESET}" "$*"; }
log_ok() { printf '%s✔%s %s\n' "${C_OK}" "${C_RESET}" "$*"; }
log_warn() { printf '%s⚠%s %s\n' "${C_WARN}" "${C_RESET}" "$*" >&2; }
log_error() { printf '%s✖%s %s\n' "${C_ERR}" "${C_RESET}" "$*" >&2; }
die() { log_error "$*"; exit 1; }

# Определение значений по умолчанию в зависимости от прав (root/user)
if [[ "${EUID}" -eq 0 ]]; then
    DEFAULT_PREFIX="/opt/satdump"
    DEFAULT_SYMLINK_DIR="/usr/local/bin"
    DEFAULT_DESKTOP_DIR="/usr/share/applications"
else
    DEFAULT_PREFIX="${HOME}/.local/opt/satdump"
    DEFAULT_SYMLINK_DIR="${HOME}/.local/bin"
    DEFAULT_DESKTOP_DIR="${HOME}/.local/share/applications"
fi

PREFIX="${DEFAULT_PREFIX}"
SYMLINK_DIR="${DEFAULT_SYMLINK_DIR}"
DESKTOP_DIR="${DEFAULT_DESKTOP_DIR}"
ENABLE_SYMLINKS=1
ENABLE_DESKTOP=1
NON_INTERACTIVE=0

usage() {
    cat <<EOU
Автономный инсталлятор SatDump из офлайн-бандла.

Использование: ./install.sh [параметры]

Параметры:
  --prefix PATH         Префикс каталога установки (по умолчанию: ${DEFAULT_PREFIX})
  --symlink-dir PATH    Каталог для символьных ссылок на бинарники (по умолчанию: ${DEFAULT_SYMLINK_DIR})
  --desktop-dir PATH    Каталог для установки .desktop файла (по умолчанию: ${DEFAULT_DESKTOP_DIR})
  --no-symlinks         Не создавать символьные ссылки
  --no-desktop          Не регистрировать ярлык приложения (.desktop)
  -y, --yes             Автоматическое подтверждение (неинтерактивный режим)
  -h, --help            Показать эту справку
EOU
}

while (( $# > 0 )); do
    case "$1" in
        --prefix) PREFIX="$2"; shift 2 ;;
        --symlink-dir|--bin-dir) SYMLINK_DIR="$2"; shift 2 ;;
        --desktop-dir) DESKTOP_DIR="$2"; shift 2 ;;
        --no-symlinks|--no-symlink) ENABLE_SYMLINKS=0; shift ;;
        --no-desktop) ENABLE_DESKTOP=0; shift ;;
        -y|--yes) NON_INTERACTIVE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "Неизвестный параметр: $1" ;;
    esac
done

log_info "Параметры установки SatDump:"
log_info "  - Префикс установки: ${PREFIX}"
if [[ "${ENABLE_SYMLINKS}" -eq 1 ]]; then
    log_info "  - Символьные ссылки: ${SYMLINK_DIR}"
fi
if [[ "${ENABLE_DESKTOP}" -eq 1 ]]; then
    log_info "  - Ярлык приложения:  ${DESKTOP_DIR}"
fi

# Проверка содержимого бандла
[[ -f "${SCRIPT_DIR}/bin/satdump" ]] || die "В бандле не найден файл bin/satdump."

# Копирование файлов в целевой префикс
log_info "Копирование файлов в ${PREFIX}..."
mkdir -p "${PREFIX}"
cp -a "${SCRIPT_DIR}/bin" "${PREFIX}/"
cp -a "${SCRIPT_DIR}/lib" "${PREFIX}/"
cp -a "${SCRIPT_DIR}/share" "${PREFIX}/"
cp -a "${SCRIPT_DIR}/satdump" "${PREFIX}/"
[[ -f "${SCRIPT_DIR}/satdump-ui" ]] && cp -a "${SCRIPT_DIR}/satdump-ui" "${PREFIX}/"
[[ -f "${SCRIPT_DIR}/MANIFEST.txt" ]] && cp -a "${SCRIPT_DIR}/MANIFEST.txt" "${PREFIX}/"
[[ -f "${SCRIPT_DIR}/SHA256SUMS" ]] && cp -a "${SCRIPT_DIR}/SHA256SUMS" "${PREFIX}/"

chmod +x "${PREFIX}/satdump"
[[ -f "${PREFIX}/satdump-ui" ]] && chmod +x "${PREFIX}/satdump-ui"

# Создание символьных ссылок
if [[ "${ENABLE_SYMLINKS}" -eq 1 ]]; then
    mkdir -p "${SYMLINK_DIR}"
    ln -sf "${PREFIX}/satdump" "${SYMLINK_DIR}/satdump"
    log_ok "Создана символьная ссылка: ${SYMLINK_DIR}/satdump -> ${PREFIX}/satdump"
    
    if [[ -f "${PREFIX}/satdump-ui" ]]; then
        ln -sf "${PREFIX}/satdump-ui" "${SYMLINK_DIR}/satdump-ui"
        log_ok "Создана символьная ссылка: ${SYMLINK_DIR}/satdump-ui -> ${PREFIX}/satdump-ui"
    fi
fi

# Регистрация desktop-файла
if [[ "${ENABLE_DESKTOP}" -eq 1 ]]; then
    DESKTOP_SRC=""
    if [[ -f "${PREFIX}/share/applications/satdump.desktop" ]]; then
        DESKTOP_SRC="${PREFIX}/share/applications/satdump.desktop"
    elif [[ -f "${PREFIX}/share/satdump/satdump.desktop" ]]; then
        DESKTOP_SRC="${PREFIX}/share/satdump/satdump.desktop"
    fi

    if [[ -n "${DESKTOP_SRC}" ]]; then
        mkdir -p "${DESKTOP_DIR}"
        TARGET_DESKTOP="${DESKTOP_DIR}/satdump.desktop"
        
        EXEC_CMD="${PREFIX}/satdump"
        [[ -f "${PREFIX}/satdump-ui" ]] && EXEC_CMD="${PREFIX}/satdump-ui"
        ICON_CMD="${PREFIX}/share/satdump/icon.png"

        # Обновление путей Exec и Icon
        sed -e "s|^Exec=.*|Exec=${EXEC_CMD} %f|" \
            -e "s|^Icon=.*|Icon=${ICON_CMD}|" \
            "${DESKTOP_SRC}" > "${TARGET_DESKTOP}"
        chmod 0644 "${TARGET_DESKTOP}"
        log_ok "Зарегистрирован ярлык: ${TARGET_DESKTOP}"

        if command -v update-desktop-database >/dev/null 2>&1; then
            update-desktop-database "${DESKTOP_DIR}" >/dev/null 2>&1 || true
        fi
    fi
fi

# Верификация установки
log_info "Проверка и верификация установки..."
VERIFICATION_OUT="$("${PREFIX}/satdump" version 2>&1 || true)"
if grep -qi 'SatDump' <<<"${VERIFICATION_OUT}"; then
    log_ok "Верификация успешна! Команда satdump version вернула:"
    printf '  %s\n' "${VERIFICATION_OUT}"
else
    log_error "Ошибка верификации! Вывод команды satdump version:"
    printf '  %s\n' "${VERIFICATION_OUT}" >&2
    die "Установка завершена с ошибкой проверки работоспособности."
fi

log_ok "SatDump успешно установлен в ${PREFIX}!"
EOF
chmod +x "${BUNDLE_DIR}/install.sh"

# 8. Создание манифеста и SHA256SUMS
GIT_COMMIT="$(git -C "${SOURCE_DIR}" rev-parse HEAD 2>/dev/null || printf unknown)"
BUILD_DATE="$(date -u +'%Y-%m-%dT%H:%M:%SZ')"

{
    printf 'SatDump Offline Bundle Manifest\n'
    printf '===============================\n'
    printf 'Name:          satdump-offline-bundle\n'
    printf 'Version:       %s\n' "${VERSION}"
    printf 'Architecture:  %s\n' "${ARCH}"
    printf 'Profile:       %s\n' "${PROFILE}"
    printf 'Build Date:    %s\n' "${BUILD_DATE}"
    printf 'Source Commit: %s\n' "${GIT_COMMIT}"
    printf 'Astra Profile: %s\n' "$(detect_astra_version)"
    printf '\n[Executables]\n'
    find "${BUNDLE_DIR}/bin" -type f -printf '- %f\n' | sort
    printf '\n[Plugins]\n'
    find "${BUNDLE_DIR}/lib/satdump/plugins" -type f -name '*.so*' -printf '- %f\n' | sort
    printf '\n[Bundled Dynamic Libraries]\n'
    find "${BUNDLE_DIR}/lib" -maxdepth 1 -type f -name '*.so*' -printf '- %f\n' | sort
} > "${BUNDLE_DIR}/MANIFEST.txt"

(
    cd "${BUNDLE_DIR}"
    find . -type f ! -name 'SHA256SUMS' -print0 \
        | sort -z \
        | xargs -0 sha256sum > SHA256SUMS
)

# 9. Архивация в .tar.gz и генерация контрольных сумм
mkdir -p "${OUTPUT_DIR}"
ARCHIVE="${OUTPUT_DIR}/${BASENAME}.tar.gz"

log_info "Создание архива ${ARCHIVE}..."
tar -czf "${ARCHIVE}" -C "${STAGING_DIR}" "${BASENAME}"

sha256sum "${ARCHIVE}" > "${ARCHIVE}.sha256"
cp "${BUNDLE_DIR}/MANIFEST.txt" "${OUTPUT_DIR}/${BASENAME}.MANIFEST.txt"

log_ok "Офлайн-бандл успешно создан!"
log_ok "Архив:     ${ARCHIVE}"
log_ok "SHA256:    ${ARCHIVE}.sha256"
log_ok "Манифест: ${OUTPUT_DIR}/${BASENAME}.MANIFEST.txt"
