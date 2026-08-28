#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

PROFILE="headless"
BOOTSTRAP_MISSING=0
APT_UPDATE=1
DRY_RUN=0
SELECTED_PACKAGE=""

usage() {
    cat <<'EOF'
Использование: bash scripts/astra/install-deps.sh [параметры]

Параметры:
  --profile headless|desktop|full
      headless — CLI, обработка Meteor/NOAA/APT, без GUI и SDR-драйверов;
      desktop  — GUI, RTL-SDR и звук;
      full     — расширенный набор доступных SDR/форматов.

  --bootstrap-missing  Локально собрать CMake, NNG и VOLK, если пакетов нет
  --no-update          Не выполнять apt-get update
  --dry-run            Показать действия без установки и без post-check
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
elif (( DRY_RUN == 1 )); then
    SUDO=(sudo)
    log_warn "sudo не найден; в dry-run команда показана условно."
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

select_apt_alternative() {
    local display="$1"
    shift

    SELECTED_PACKAGE="$(first_available_package "$@" 2>/dev/null || true)"
    if [[ -n "${SELECTED_PACKAGE}" ]]; then
        log_info "${display}: выбран пакет ${SELECTED_PACKAGE}"
        return 0
    fi

    if (( DRY_RUN == 1 )); then
        SELECTED_PACKAGE="$1"
        log_warn "${display}: APT-кэш не содержит подходящий пакет; dry-run показывает первый кандидат ${SELECTED_PACKAGE}."
        return 0
    fi

    log_warn "В подключённых репозиториях не найден пакет для ${display}: $*"
    return 1
}

install_apt_alternative() {
    local display="$1"
    shift
    select_apt_alternative "${display}" "$@" || return 1
    install_list "${SELECTED_PACKAGE}"
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

# python3-mako нужен при локальной сборке VOLK.
if apt_package_available python3-mako || (( DRY_RUN == 1 )); then
    BASE_PACKAGES+=(python3-mako)
fi

install_list "${BASE_PACKAGES[@]}"
install_list libfftw3-dev libpng-dev libjemalloc-dev

install_apt_alternative "libtiff" libtiff5-dev libtiff-dev \
    || die "Без заголовков libtiff SatDump не собирается."
install_apt_alternative "libcurl" libcurl4-openssl-dev libcurl4-gnutls-dev libcurl3-openssl-dev \
    || die "Без libcurl SatDump не собирается."

if apt_package_available cmake || (( DRY_RUN == 1 )); then
    install_list cmake
fi

# Astra Linux 1.6 нередко имеет старый системный GCC. Пакеты GCC 8
# устанавливаются только если они реально присутствуют в подключённом dev-репозитории.
if ! (select_compiler >/dev/null 2>&1); then
    if (apt_package_available gcc-8 && apt_package_available g++-8) || (( DRY_RUN == 1 )); then
        install_list gcc-8 g++-8
    fi
fi

NNG_PACKAGE="$(first_available_package libnng-dev nng-dev 2>/dev/null || true)"
VOLK_PACKAGE="$(first_available_package libvolk2-dev libvolk1-dev libvolk-dev 2>/dev/null || true)"

if [[ -n "${NNG_PACKAGE}" ]]; then
    install_list "${NNG_PACKAGE}"
else
    log_warn "Пакет разработчика NNG не найден."
fi

if [[ -n "${VOLK_PACKAGE}" ]]; then
    install_list "${VOLK_PACKAGE}"
else
    log_warn "Пакет разработчика VOLK не найден."
fi

case "${PROFILE}" in
    headless)
        ;;
    desktop)
        install_list libglfw3-dev libgl1-mesa-dev zenity
        install_apt_alternative "PortAudio" portaudio19-dev libportaudio2-dev || true
        install_apt_alternative "RTL-SDR" librtlsdr-dev librtlsdr0-dev || true
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
            if apt_package_available "${package}" || (( DRY_RUN == 1 )); then
                install_list "${package}"
            else
                log_warn "Дополнительный пакет недоступен и пропущен: ${package}"
            fi
        done
        ;;
esac

if (( DRY_RUN == 1 )); then
    printf '\n'
    log_ok "Dry-run завершён: системные файлы и пакеты не изменены."
    if (( BOOTSTRAP_MISSING == 1 )); then
        log_info "После реальной установки будут локально собраны недостающие CMake/NNG/VOLK."
    fi
    exit 0
fi

# После реальной установки проверяем toolchain.
select_compiler >/dev/null

CMAKE_EXECUTABLE="$(find_cmake 3.18.0 2>/dev/null || true)"
if [[ -z "${CMAKE_EXECUTABLE}" ]]; then
    if (( BOOTSTRAP_MISSING == 1 )); then
        bash "${ASTRA_SCRIPT_DIR}/bootstrap-cmake.sh"
    else
        die "CMake 3.18+ не найден. Запустите с --bootstrap-missing или выполните bash scripts/astra/bootstrap-cmake.sh."
    fi
fi

NEED_NNG=0
NEED_VOLK=0
[[ -z "${NNG_PACKAGE}" ]] && NEED_NNG=1
[[ -z "${VOLK_PACKAGE}" ]] && NEED_VOLK=1

if (( NEED_NNG == 1 || NEED_VOLK == 1 )); then
    if (( BOOTSTRAP_MISSING == 0 )); then
        die "NNG/VOLK доступны не полностью. Повторите с --bootstrap-missing для локальной сборки недостающих библиотек."
    fi

    if (( NEED_VOLK == 1 )) && ! python3 -c 'import mako' >/dev/null 2>&1; then
        die "Для локальной сборки VOLK требуется Python-модуль Mako. Подключите штатный repository-dev и установите python3-mako."
    fi

    # Не используем component=all: он также пересобирает FFTW/cURL/TIFF/jemalloc,
    # хотя эти библиотеки уже установлены из штатных Astra-пакетов. Bootstrap
    # должен быть минимальным и воспроизводимым: только реально отсутствующие ABI.
    if (( NEED_NNG == 1 )); then
        bash "${ASTRA_SCRIPT_DIR}/bootstrap-thirdparty.sh" --component nng
    fi
    if (( NEED_VOLK == 1 )); then
        bash "${ASTRA_SCRIPT_DIR}/bootstrap-thirdparty.sh" --component volk
    fi
fi

log_ok "Зависимости профиля ${PROFILE} обработаны."
log_info "Следующий шаг: bash scripts/astra/build.sh --profile ${PROFILE}"
bash "${ASTRA_SCRIPT_DIR}/check-system.sh" || true
