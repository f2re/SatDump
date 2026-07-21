#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

STRICT=0
case "${1:-}" in
    "") ;;
    --strict) STRICT=1 ;;
    -h|--help)
        cat <<'EOF'
Использование: bash scripts/astra/check-system.sh [--strict]

Без --strict сценарий печатает полный отчёт и возвращает 0 даже при проблемах.
С --strict критические проблемы дают ненулевой код возврата.
EOF
        exit 0
        ;;
    *) die "Неизвестный параметр: ${1}" ;;
esac

failures=0
warnings=0

check_ok() { log_ok "$*"; }
check_warn() { log_warn "$*"; warnings=$((warnings + 1)); }
check_fail() { log_error "$*"; failures=$((failures + 1)); }

print_platform_summary
printf '\n'

ASTRA_VERSION="$(detect_astra_version)"
case "${ASTRA_VERSION}" in
    1.6|1.7) check_ok "Поддерживаемая линия Astra Linux ${ASTRA_VERSION}" ;;
    unknown) check_warn "Версия Astra Linux не определена. Используйте ASTRA_VERSION_OVERRIDE=1.6 или 1.7 только для контролируемой совместимой среды." ;;
    *) check_fail "Обнаружена неподдерживаемая версия Astra Linux: ${ASTRA_VERSION}" ;;
esac

case "$(uname -m)" in
    x86_64|amd64) check_ok "Архитектура x86_64" ;;
    aarch64|arm64) check_warn "ARM64 поддерживается экспериментально; профиль Astra тестируется прежде всего на x86_64." ;;
    *) check_fail "Непроверенная архитектура: $(uname -m)" ;;
esac

printf '\n🔧 Инструментарий\n'
# select_compiler завершает процесс при ошибке, поэтому сначала запускаем его
# в subshell. При успехе повторяем в текущем shell, чтобы получить CC/CXX.
if (select_compiler >/dev/null 2>&1); then
    select_compiler >/dev/null
    check_ok "$(${CXX} --version | head -n1)"
    check_ok "Проверочная программа C++17 компилируется"
else
    check_fail "Нет компилятора с рабочей поддержкой C++17."
fi

if CMAKE_FOUND="$(find_cmake 3.18.0 2>/dev/null)"; then
    check_ok "CMake $(cmake_version "${CMAKE_FOUND}") (${CMAKE_FOUND})"
else
    check_fail "Требуется CMake 3.18 или новее. Запустите bash scripts/astra/bootstrap-cmake.sh."
fi

for command in git make pkg-config tar sha256sum md5sum readlink; do
    if command_exists "${command}"; then
        check_ok "${command}: $(command -v "${command}")"
    else
        check_fail "Не найдена команда ${command}"
    fi
done

if command_exists curl || command_exists wget; then
    if command_exists curl; then
        check_ok "загрузчик: $(command -v curl)"
    else
        check_ok "загрузчик: $(command -v wget)"
    fi
else
    check_warn "Не найдены curl/wget. Online-bootstrap недоступен; используйте офлайн-архивы."
fi

printf '\n📦 Библиотеки\n'
check_pkg_config() {
    local display="$1"
    shift
    local module
    for module in "$@"; do
        if pkg-config --exists "${module}" 2>/dev/null; then
            check_ok "${display}: ${module} $(pkg-config --modversion "${module}" 2>/dev/null || true)"
            return 0
        fi
    done
    check_fail "${display}: не найдена через pkg-config (${*})"
    return 1
}

if command_exists pkg-config; then
    check_pkg_config "FFTW3f" fftw3f || true
    check_pkg_config "libcurl" libcurl || true
    check_pkg_config "VOLK" volk || true
else
    check_fail "pkg-config недоступен; библиотеки проверить невозможно."
fi

INCLUDE_ROOTS=(/usr/include /usr/local/include "${ASTRA_DEPS_PREFIX}/include")
if command_exists dpkg-architecture; then
    MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || true)"
    if [[ -n "${MULTIARCH}" ]]; then
        INCLUDE_ROOTS+=("/usr/include/${MULTIARCH}")
    fi
fi

check_any_header() {
    local display="$1"
    shift
    local root header
    for header in "$@"; do
        for root in "${INCLUDE_ROOTS[@]}"; do
            if [[ -e "${root}/${header}" ]]; then
                check_ok "${display}: ${root}/${header}"
                return 0
            fi
        done
    done
    check_fail "${display}: не найден ни один заголовок из списка: $*"
    return 1
}

check_any_header "libpng" "png.h" "libpng16/png.h" || true
check_any_header "libtiff" "tiffio.h" || true
check_any_header "jemalloc" "jemalloc/jemalloc.h" || true
check_any_header "NNG" "nng/nng.h" || true

if [[ -d "${ASTRA_DEPS_PREFIX}" ]]; then
    check_ok "Локальный префикс зависимостей: ${ASTRA_DEPS_PREFIX}"
fi

printf '\n🌐 Репозитории APT\n'
APT_LIST_FILES=(/etc/apt/sources.list)
while IFS= read -r file; do
    APT_LIST_FILES+=("${file}")
done < <(find /etc/apt/sources.list.d -maxdepth 1 -type f -name '*.list' 2>/dev/null || true)

READABLE_APT_FILES=()
for file in "${APT_LIST_FILES[@]}"; do
    [[ -r "${file}" ]] && READABLE_APT_FILES+=("${file}")
done

if (( ${#READABLE_APT_FILES[@]} > 0 )); then
    mapfile -t ASTRA_REPOS < <(grep -hE '^[[:space:]]*deb[[:space:]].*(astralinux|astra/)' "${READABLE_APT_FILES[@]}" 2>/dev/null || true)
    if (( ${#ASTRA_REPOS[@]} > 0 )); then
        printf '%s\n' "${ASTRA_REPOS[@]}" | sed 's/^/  /'
        check_ok "Репозитории Astra Linux обнаружены"
    else
        check_warn "В активных APT-источниках не найдены репозитории Astra Linux."
    fi
else
    check_warn "Файлы APT-репозиториев недоступны для чтения."
fi

if command_exists pdp-id; then
    log_info "Контекст безопасности: $(pdp-id 2>/dev/null || printf 'недоступен')"
else
    log_info "Утилита pdp-id не установлена; уровень целостности сценарием не определяется."
fi

printf '\n'
if (( failures == 0 )); then
    log_ok "Базовая среда готова к сборке. Предупреждений: ${warnings}."
    exit 0
fi

log_error "Обнаружено критических проблем: ${failures}; предупреждений: ${warnings}."
log_info "Автоматическая установка: bash scripts/astra/install-deps.sh --profile headless --bootstrap-missing"

if (( STRICT == 1 )); then
    exit 1
fi
exit 0
