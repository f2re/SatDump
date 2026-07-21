#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

STRICT=0
if [[ "${1:-}" == "--strict" ]]; then
    STRICT=1
fi

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
if select_compiler >/dev/null 2>&1; then
    check_ok "$(${CXX} --version | head -n1)"
else
    check_fail "Нет компилятора с поддержкой C++17."
fi

if CMAKE_FOUND="$(find_cmake 3.18.0 2>/dev/null)"; then
    check_ok "CMake $(cmake_version "${CMAKE_FOUND}") (${CMAKE_FOUND})"
else
    check_fail "Требуется CMake 3.18 или новее. Запустите scripts/astra/bootstrap-cmake.sh."
fi

for command in git make pkg-config curl tar sha256sum; do
    if command_exists "${command}"; then
        check_ok "${command}: $(command -v "${command}")"
    else
        check_fail "Не найдена команда ${command}"
    fi
done

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

check_header() {
    local display="$1"
    local header="$2"
    local found=0
    local root
    for root in /usr/include /usr/local/include "${ASTRA_DEPS_PREFIX}/include"; do
        if [[ -e "${root}/${header}" ]]; then
            check_ok "${display}: ${root}/${header}"
            found=1
            break
        fi
    done
    if [[ "${found}" == "0" ]]; then
        check_fail "${display}: заголовок ${header} не найден"
    fi
}

check_header "libpng" "png.h"
check_header "libtiff" "tiffio.h"
check_header "jemalloc" "jemalloc/jemalloc.h"
check_header "NNG" "nng/nng.h"

if [[ -d "${ASTRA_DEPS_PREFIX}" ]]; then
    check_ok "Локальный префикс зависимостей: ${ASTRA_DEPS_PREFIX}"
fi

printf '\n🌐 Репозитории APT\n'
if [[ -r /etc/apt/sources.list ]] || compgen -G '/etc/apt/sources.list.d/*.list' >/dev/null; then
    mapfile -t ASTRA_REPOS < <(grep -hE '^[[:space:]]*deb[[:space:]].*(astralinux|astra/)' /etc/apt/sources.list /etc/apt/sources.list.d/*.list 2>/dev/null || true)
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
log_info "Автоматическая установка: sudo scripts/astra/install-deps.sh --profile headless --bootstrap-missing"

if (( STRICT == 1 )); then
    exit 1
fi
exit 0
