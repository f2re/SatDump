#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

PROFILE="headless"
BOOTSTRAP_MISSING=0
APT_UPDATE=1
DRY_RUN=0

usage() {
    cat <<'EOF'
Использование: scripts/astra/install-deps.sh [параметры]

Параметры:
  --profile headless|desktop|full
      headless — CLI, обработка Meteor/NOAA/APT, без GUI и SDR-драйверов;
      desktop  — GUI, RTL-SDR и звук;
      full     — расширенный набор доступных SDR/форматов.

  --bootstrap-missing  Локально собрать CMake, NNG и VOLK, если пакетов нет
  --no-update          Не выполнять apt-get update
  --dry-run            Показать действия без установки
  -h, --help           Показать справку

Сценарий не изменяет /etc/apt/sources.list. Примеры репозиториев лежат в
scripts/astra/repos/ и должны быть согласованы с администратором защищённой среды.
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --profile) PROFILE="$2"; shift 2 ;;
        --bootstrap-missing) BOOTSTRAP_MISSING=1; shift ;;
        --no-update) APT_UPDATE=0; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "Неизвестный параметр: $1" ;;
    esac
done

case "${PROFILE}" in
    headless|desktop|full) ;;
    *) die "Неизвестный профиль: ${PROFILE}" ;;
esac

if (( EUID == 0 )); then
    SUDO=()
elif command_exists sudo; then
    SUDO=(sudo)
else
    die "Для установки пакетов требуется root или команда sudo."
fi

run_root() {
    if (( DRY_RUN == 1 )); then
        printf 'DRY-RUN:'
        printf ' %q' "${SUDO[@]}" "$@"
        printf '\n'
    else
        "${SUDO[@]}" "$@"
    fi
}

install_list() {
    (( $# > 0 )) || return 0
    log_info "Установка пакетов: $*"
    run_root apt-get install -y --no-install-recommends "$@"
}

install_alternative() {
    local display="$1"
    shift
    local package=""
    package="$(first_available_package "$@" 2>/dev/null || true)"
    if [[ -n "${package}" ]]; then
        install_list "${package}"
        printf '%s\n' "${package}"
        return 0
    fi
    log_warn "В подключённых репозиториях не найден пакет для ${display}: $*"
    return 1
}

print_platform_summary
ASTRA_VERSION="$(detect_astra_version)"
if [[ "${ASTRA_VERSION}" != "1.6" && "${ASTRA_VERSION}" != "1.7" && "${ASTRA_VERSION}" != "unknown" ]]; then
    die "Этот сценарий рассчитан на Astra Linux 1.6/1.7."
fi

if (( APT_UPDATE == 1 )); then
    log_info "Обновление индексов APT"
    run_root apt-get update
fi

BASE_PACKAGES=(
    build-essential git make pkg-config
    ca-certificates apt-transport-https
    curl wget tar xz-utils unzip
    python3
)

# python3-mako нужен при локальной сборке VOLK. В некоторых закрытых
# репозиториях пакет может отсутствовать — тогда он проверяется отдельно.
if apt_package_available python3-mako; then
    BASE_PACKAGES+=(python3-mako)
fi

install_list "${BASE_PACKAGES[@]}"

MANDATORY_PACKAGES=(libfftw3-dev libpng-dev libjemalloc-dev)
install_list "${MANDATORY_PACKAGES[@]}"

install_alternative "libtiff" libtiff5-dev libtiff-dev >/dev/null || die "Без заголовков libtiff SatDump не собирается."
install_alternative "libcurl" libcurl4-openssl-dev libcurl4-gnutls-dev libcurl3-openssl-dev >/dev/null || die "Без libcurl SatDump не собирается."

if apt_package_available cmake; then
    install_list cmake
fi

# Astra Linux 1.6 нередко имеет старый системный GCC. Пакеты GCC 8
# устанавливаются только если они реально присутствуют в подключённом dev-репозитории.
if ! (select_compiler >/dev/null 2>&1); then
    if apt_package_available gcc-8 && apt_package_available g++-8; then
        install_list gcc-8 g++-8
    fi
fi

if ! (select_compiler >/dev/null 2>&1); then
    die "Не найден GCC/G++ 8+ или совместимый Clang с C++17. Подключите штатный repository-dev для вашей версии Astra или задайте CC/CXX."
fi

CMAKE_EXECUTABLE="$(find_cmake 3.18.0 2>/dev/null || true)"
if [[ -z "${CMAKE_EXECUTABLE}" ]]; then
    if (( BOOTSTRAP_MISSING == 1 )); then
        "${ASTRA_SCRIPT_DIR}/bootstrap-cmake.sh"
    else
        die "CMake 3.18+ не найден. Запустите с --bootstrap-missing или выполните scripts/astra/bootstrap-cmake.sh."
    fi
fi

NNG_PACKAGE=""
VOLK_PACKAGE=""
if NNG_PACKAGE="$(install_alternative "NNG" libnng-dev nng-dev 2>/dev/null)"; then
    :
else
    NNG_PACKAGE=""
fi
if VOLK_PACKAGE="$(install_alternative "VOLK" libvolk2-dev libvolk1-dev libvolk-dev 2>/dev/null)"; then
    :
else
    VOLK_PACKAGE=""
fi

if [[ -z "${NNG_PACKAGE}" || -z "${VOLK_PACKAGE}" ]]; then
    if (( BOOTSTRAP_MISSING == 1 )); then
        COMPONENT="all"
        if [[ -n "${NNG_PACKAGE}" ]]; then
            COMPONENT="volk"
        elif [[ -n "${VOLK_PACKAGE}" ]]; then
            COMPONENT="nng"
        fi
        "${ASTRA_SCRIPT_DIR}/bootstrap-thirdparty.sh" --component "${COMPONENT}"
    else
        die "NNG/VOLK доступны не полностью. Повторите с --bootstrap-missing для локальной сборки недостающих библиотек."
    fi
fi

case "${PROFILE}" in
    headless)
        ;;
    desktop)
        install_list libglfw3-dev libgl1-mesa-dev zenity
        install_alternative "PortAudio" portaudio19-dev libportaudio2-dev >/dev/null || true
        install_alternative "RTL-SDR" librtlsdr-dev librtlsdr0-dev >/dev/null || true
        ;;
    full)
        install_list libglfw3-dev libgl1-mesa-dev zenity
        OPTIONAL_PACKAGES=(
            portaudio19-dev libzstd-dev libhdf5-dev
            librtlsdr-dev libhackrf-dev libairspy-dev libairspyhf-dev
            libbladerf-dev libiio-dev libad9361-dev
            ocl-icd-opencl-dev
        )
        for package in "${OPTIONAL_PACKAGES[@]}"; do
            if apt_package_available "${package}"; then
                install_list "${package}"
            else
                log_warn "Дополнительный пакет недоступен и пропущен: ${package}"
            fi
        done
        ;;
esac

log_ok "Зависимости профиля ${PROFILE} обработаны."
log_info "Следующий шаг: scripts/astra/build.sh --profile ${PROFILE}"

if (( DRY_RUN == 0 )); then
    "${ASTRA_SCRIPT_DIR}/check-system.sh" || true
fi
