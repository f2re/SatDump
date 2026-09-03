#!/usr/bin/env bash

# Общие функции для сценариев сборки SatDump 1.2.2 в Astra Linux.
# Файл предназначен для подключения через source.

set -Eeuo pipefail

ASTRA_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SATDUMP_ROOT="$(cd "${ASTRA_SCRIPT_DIR}/../.." && pwd)"
ASTRA_CACHE_DIR="${ASTRA_CACHE_DIR:-${HOME}/.cache/satdump-astra}"
ASTRA_TOOLS_DIR="${ASTRA_TOOLS_DIR:-${HOME}/.local/opt/satdump-astra}"
ASTRA_DEPS_PREFIX="${ASTRA_DEPS_PREFIX:-${ASTRA_TOOLS_DIR}/deps}"

if [[ -t 1 && "${NO_COLOR:-0}" != "1" ]]; then
    C_RESET=$'\033[0m'
    C_INFO=$'\033[1;36m'
    C_OK=$'\033[1;32m'
    C_WARN=$'\033[1;33m'
    C_ERR=$'\033[1;31m'
else
    C_RESET=""
    C_INFO=""
    C_OK=""
    C_WARN=""
    C_ERR=""
fi

log_info() { printf '%sℹ%s %s\n' "${C_INFO}" "${C_RESET}" "$*"; }
log_ok() { printf '%s✔%s %s\n' "${C_OK}" "${C_RESET}" "$*"; }
log_warn() { printf '%s⚠%s %s\n' "${C_WARN}" "${C_RESET}" "$*" >&2; }
log_error() { printf '%s✖%s %s\n' "${C_ERR}" "${C_RESET}" "$*" >&2; }
die() { log_error "$*"; exit 1; }

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

jobs_count() {
    if command_exists nproc; then
        nproc
    elif command_exists getconf; then
        getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2\n'
    else
        printf '2\n'
    fi
}

version_ge() {
    local left="$1"
    local right="$2"

    if command_exists dpkg; then
        dpkg --compare-versions "${left}" ge "${right}"
        return
    fi

    [[ "$(printf '%s\n%s\n' "${right}" "${left}" | sort -V | tail -n1)" == "${left}" ]]
}

extract_astra_version() {
    local input="$1"
    if [[ "${input}" =~ (1\.[67])([._-][0-9]+)* ]]; then
        printf '%s\n' "${BASH_REMATCH[1]}"
        return 0
    fi
    return 1
}

detect_astra_version() {
    local candidate=""

    if [[ -n "${ASTRA_VERSION_OVERRIDE:-}" ]]; then
        if ! extract_astra_version "${ASTRA_VERSION_OVERRIDE}"; then
            printf 'unknown\n'
        fi
        return
    fi

    for file in /etc/astra/build_version /etc/astra_version /etc/astra/build-version; do
        if [[ -r "${file}" ]]; then
            candidate="$(tr -d '\r\n' < "${file}")"
            if extract_astra_version "${candidate}"; then
                return
            fi
        fi
    done

    if [[ -r /etc/os-release ]]; then
        candidate="$(. /etc/os-release; printf '%s %s %s\n' "${ID:-}" "${VERSION_ID:-}" "${PRETTY_NAME:-}")"
        if extract_astra_version "${candidate}"; then
            return
        fi
    fi

    printf 'unknown\n'
}

astra_full_version() {
    for file in /etc/astra/build_version /etc/astra_version /etc/astra/build-version; do
        if [[ -r "${file}" ]]; then
            tr -d '\r\n' < "${file}"
            return
        fi
    done

    if [[ -r /etc/os-release ]]; then
        (. /etc/os-release; printf '%s\n' "${PRETTY_NAME:-${VERSION_ID:-unknown}}")
        return
    fi

    printf 'unknown\n'
}

compiler_supports_cxx17() {
    local compiler="$1"
    local tmpdir
    tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/satdump-cxx17.XXXXXX")"

    cat > "${tmpdir}/test.cpp" <<'EOF'
#include <optional>
#include <string_view>
#include <vector>

int main()
{
    std::optional<int> value = 17;
    std::string_view text = "SatDump";
    std::vector<int> values = {*value};
    return text.empty() || values.front() != 17;
}
EOF

    if "${compiler}" -std=c++17 "${tmpdir}/test.cpp" -o "${tmpdir}/test" >/dev/null 2>&1; then
        rm -rf "${tmpdir}"
        return 0
    fi

    rm -rf "${tmpdir}"
    return 1
}

select_compiler() {
    local cxx_candidates=()
    local selected_cxx=""
    local selected_cc=""

    if [[ -n "${CXX:-}" ]]; then
        cxx_candidates+=("${CXX}")
    fi

    cxx_candidates+=(
        g++-13 g++-12 g++-11 g++-10 g++-9 g++-8
        clang++-16 clang++-15 clang++-14 clang++-13 clang++-12 clang++-11
        g++ clang++
    )

    local candidate
    for candidate in "${cxx_candidates[@]}"; do
        if ! command_exists "${candidate}"; then
            continue
        fi
        if compiler_supports_cxx17 "${candidate}"; then
            selected_cxx="$(command -v "${candidate}")"
            break
        fi
    done

    [[ -n "${selected_cxx}" ]] || die "Не найден компилятор с рабочей поддержкой C++17. Для Astra 1.6 обычно требуется GCC/G++ 8 или новее."

    if [[ -n "${CC:-}" ]] && command_exists "${CC}"; then
        selected_cc="$(command -v "${CC}")"
    else
        case "$(basename "${selected_cxx}")" in
            g++-*) selected_cc="$(command -v "gcc-${selected_cxx##*-}" 2>/dev/null || true)" ;;
            g++) selected_cc="$(command -v gcc 2>/dev/null || true)" ;;
            clang++-*) selected_cc="$(command -v "clang-${selected_cxx##*-}" 2>/dev/null || true)" ;;
            clang++) selected_cc="$(command -v clang 2>/dev/null || true)" ;;
        esac
    fi

    [[ -n "${selected_cc}" ]] || die "Для ${selected_cxx} не найден соответствующий компилятор C. Задайте CC вручную."

    export CC="${selected_cc}"
    export CXX="${selected_cxx}"
    log_ok "Компиляторы: CC=${CC}, CXX=${CXX}"
}

cmake_version() {
    local executable="$1"
    "${executable}" --version 2>/dev/null | awk 'NR == 1 { print $3 }'
}

find_cmake() {
    local minimum="${1:-3.18.0}"
    local candidates=()

    if [[ -n "${CMAKE_BIN:-}" ]]; then
        candidates+=("${CMAKE_BIN}")
    fi

    candidates+=(
        "${ASTRA_TOOLS_DIR}/cmake-3.18.6/bin/cmake"
        "${HOME}/.local/bin/cmake"
        cmake
    )

    local candidate version
    for candidate in "${candidates[@]}"; do
        if [[ "${candidate}" == */* ]]; then
            [[ -x "${candidate}" ]] || continue
        elif ! command_exists "${candidate}"; then
            continue
        fi

        version="$(cmake_version "${candidate}")"
        if [[ -n "${version}" ]] && version_ge "${version}" "${minimum}"; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    return 1
}

apt_package_available() {
    local package="$1"
    local candidate_metadata=""

    command_exists apt-cache || return 1

    # `apt-cache show` without this option also prints versions which are known
    # to the cache but have no installable Candidate.  Astra repositories can
    # leave such records behind for packages which were removed or replaced
    # (for example libvolk2-dev), after which `apt-get install` still fails.
    # Asking only for the candidate is locale-independent and produces no
    # output when Candidate is `(none)`.
    candidate_metadata="$(apt-cache --no-all-versions show "${package}" 2>/dev/null)" || return 1
    [[ -n "${candidate_metadata}" ]]
}

first_available_package() {
    local package
    for package in "$@"; do
        if apt_package_available "${package}"; then
            printf '%s\n' "${package}"
            return 0
        fi
    done
    return 1
}

ensure_directory() {
    mkdir -p "$1"
}

print_platform_summary() {
    local astra_version
    astra_version="$(detect_astra_version)"

    log_info "ОС: $(astra_full_version)"
    log_info "Профиль Astra: ${astra_version}"
    log_info "Архитектура: $(uname -m)"
    log_info "Ядро: $(uname -r)"
}
