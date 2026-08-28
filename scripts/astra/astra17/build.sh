#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SATDUMP_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

PROFILE="desktop"
ROOTFS="/var/lib/satdump-build/astra17-buster-amd64"
WORK_DIR="${SATDUMP_ROOT}/build/astra17-portable"
OUTPUT_DIR="${SATDUMP_ROOT}/dist/astra17"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
CLEAN_ROOTFS=0
KEEP_WORK=0
MIRROR="http://archive.debian.org/debian"
ALLOW_UNSIGNED=0

log() { printf '[astra17] %s\n' "$*"; }
warn() { printf '[astra17] WARNING: %s\n' "$*" >&2; }
fail() { printf '[astra17] ERROR: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<EOF2
Использование: bash scripts/astra/astra17/build.sh [параметры]

Собирает SatDump 1.2.2 в Debian 10 Buster chroot (glibc 2.28), затем формирует
relocatable-бандл с замкнутым набором non-glibc runtime-библиотек.

Параметры:
  --profile headless|desktop  Профиль (по умолчанию: desktop)
  --rootfs PATH               Buster chroot (по умолчанию: ${ROOTFS})
  --work-dir PATH             Каталог сборки (по умолчанию: ${WORK_DIR})
  --output-dir PATH           Каталог артефактов (по умолчанию: ${OUTPUT_DIR})
  --jobs N                    Параллельные задачи
  --clean-rootfs              Пересоздать rootfs
  --keep-work                 Не очищать CMake build/stage
  --mirror URL                Debian Buster mirror/archive
  --allow-unsigned-archive    Только для доверенного локального архива
  -h, --help                  Справка
EOF2
}

while (( $# > 0 )); do
    case "$1" in
        --profile) PROFILE="$2"; shift 2 ;;
        --rootfs) ROOTFS="$2"; shift 2 ;;
        --work-dir) WORK_DIR="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --clean-rootfs) CLEAN_ROOTFS=1; shift ;;
        --keep-work) KEEP_WORK=1; shift ;;
        --mirror) MIRROR="$2"; shift 2 ;;
        --allow-unsigned-archive) ALLOW_UNSIGNED=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) fail "Неизвестный параметр: $1" ;;
    esac
done

[[ "${PROFILE}" == "headless" || "${PROFILE}" == "desktop" ]] || fail "--profile: headless или desktop"
[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || fail "--jobs должен быть положительным целым числом"
[[ "$(uname -m)" == "x86_64" ]] || fail "Сейчас поддерживается только x86_64"

if (( EUID == 0 )); then
    SUDO=()
    OWNER_UID="${SUDO_UID:-0}"
    OWNER_GID="${SUDO_GID:-0}"
elif command -v sudo >/dev/null 2>&1; then
    SUDO=(sudo)
    OWNER_UID="$(id -u)"
    OWNER_GID="$(id -g)"
else
    fail "Нужен root/sudo для debootstrap и chroot"
fi

for cmd in debootstrap chroot mount umount findmnt rsync readlink; do
    command -v "${cmd}" >/dev/null 2>&1 || fail "Не найдена команда ${cmd}"
done

abs() { readlink -m "$1"; }
ROOTFS="$(abs "${ROOTFS}")"
WORK_DIR="$(abs "${WORK_DIR}")"
OUTPUT_DIR="$(abs "${OUTPUT_DIR}")"
SOURCE_DIR="$(readlink -f "${SATDUMP_ROOT}")"

for p in "${ROOTFS}" "${WORK_DIR}" "${OUTPUT_DIR}"; do
    [[ "${p}" != *[$'\n\r\t ']* ]] || fail "Путь содержит пробелы: ${p}"
done

case "${ROOTFS}" in
    /|/usr|/var|/var/lib|/home|/root|/opt) fail "Опасный rootfs: ${ROOTFS}" ;;
esac

mkdir -p "${WORK_DIR}" "${OUTPUT_DIR}"

unmount_children() {
    [[ -d "${ROOTFS}" ]] || return 0
    local prefix="${ROOTFS%/}/" target
    while IFS= read -r target; do
        [[ "${target}" == "${prefix}"* ]] || continue
        "${SUDO[@]}" umount -l -- "${target}" >/dev/null 2>&1 || true
    done < <(findmnt -rn -o TARGET | awk '{print length($0),$0}' | sort -rn | cut -d' ' -f2-)
}

if (( CLEAN_ROOTFS == 1 )); then
    unmount_children
    warn "Удаление rootfs ${ROOTFS}"
    "${SUDO[@]}" rm -rf --one-file-system "${ROOTFS}"
fi

MARKER="${ROOTFS}/etc/satdump-astra17-rootfs"
if [[ ! -f "${MARKER}" ]]; then
    [[ ! -e "${ROOTFS}" || -z "$(find "${ROOTFS}" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]] \
        || fail "rootfs не пуст и не содержит маркер SatDump: ${ROOTFS}"
    "${SUDO[@]}" mkdir -p "${ROOTFS}"

    DEB_ARGS=(--arch=amd64 --variant=minbase)
    KEYRING="/usr/share/keyrings/debian-archive-keyring.gpg"
    if (( ALLOW_UNSIGNED == 1 )); then
        DEB_ARGS+=(--no-check-gpg)
    elif [[ -r "${KEYRING}" ]]; then
        DEB_ARGS+=(--keyring="${KEYRING}")
    else
        fail "Нет ${KEYRING}; установите debian-archive-keyring или явно используйте --allow-unsigned-archive"
    fi

    log "debootstrap Debian 10 Buster (${MIRROR})"
    "${SUDO[@]}" debootstrap "${DEB_ARGS[@]}" buster "${ROOTFS}" "${MIRROR}"

    TRUST="check-valid-until=no"
    (( ALLOW_UNSIGNED == 1 )) && TRUST="trusted=yes check-valid-until=no"
    cat <<EOF2 | "${SUDO[@]}" tee "${ROOTFS}/etc/apt/sources.list" >/dev/null
deb [${TRUST}] ${MIRROR} buster main contrib non-free
EOF2
    cat <<'EOF2' | "${SUDO[@]}" tee "${ROOTFS}/etc/apt/apt.conf.d/99satdump-archive" >/dev/null
Acquire::Check-Valid-Until "false";
Acquire::Retries "3";
APT::Get::Assume-Yes "true";
Dpkg::Use-Pty "0";
EOF2
    if (( ALLOW_UNSIGNED == 1 )); then
        cat <<'EOF2' | "${SUDO[@]}" tee -a "${ROOTFS}/etc/apt/apt.conf.d/99satdump-archive" >/dev/null
Acquire::AllowInsecureRepositories "true";
Acquire::AllowDowngradeToInsecureRepositories "true";
APT::Get::AllowUnauthenticated "true";
EOF2
    fi
    [[ -r /etc/resolv.conf ]] && "${SUDO[@]}" cp -L /etc/resolv.conf "${ROOTFS}/etc/resolv.conf"

    GLIBC="$(${SUDO[@]} chroot "${ROOTFS}" /usr/bin/ldd --version 2>&1 | head -n1 | grep -o '[0-9][0-9.]*$' || true)"
    [[ "${GLIBC}" == "2.28" ]] || fail "Buster rootfs дал glibc ${GLIBC:-unknown}, ожидалась 2.28"
    cat <<EOF2 | "${SUDO[@]}" tee "${MARKER}" >/dev/null
profile=astra17-buster
architecture=amd64
glibc=2.28
mirror=${MIRROR}
EOF2
else
    grep -Fxq 'glibc=2.28' "${MARKER}" || fail "rootfs не соответствует Astra 1.7/glibc 2.28"
fi

MOUNTED=()
mount_bind() {
    local src="$1" dst="$2" ro="${3:-0}"
    "${SUDO[@]}" mkdir -p "${dst}"
    "${SUDO[@]}" mount --bind "${src}" "${dst}"
    MOUNTED+=("${dst}")
    (( ro == 0 )) || "${SUDO[@]}" mount -o remount,bind,ro "${dst}"
}
mount_rbind() {
    local src="$1" dst="$2"
    "${SUDO[@]}" mkdir -p "${dst}"
    "${SUDO[@]}" mount --rbind "${src}" "${dst}"
    "${SUDO[@]}" mount --make-rslave "${dst}"
    MOUNTED+=("${dst}")
}
cleanup() {
    local i
    for ((i=${#MOUNTED[@]}-1; i>=0; i--)); do
        "${SUDO[@]}" umount -l -- "${MOUNTED[i]}" >/dev/null 2>&1 || true
    done
    "${SUDO[@]}" chown -R "${OWNER_UID}:${OWNER_GID}" "${WORK_DIR}" "${OUTPUT_DIR}" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

unmount_children
mount_rbind /dev "${ROOTFS}/dev"
mount_rbind /sys "${ROOTFS}/sys"
"${SUDO[@]}" mkdir -p "${ROOTFS}/proc"
"${SUDO[@]}" mount -t proc proc "${ROOTFS}/proc"
MOUNTED+=("${ROOTFS}/proc")
mount_bind "${SOURCE_DIR}" "${ROOTFS}/build/source" 1
mount_bind "${WORK_DIR}" "${ROOTFS}/build/work"
mount_bind "${OUTPUT_DIR}" "${ROOTFS}/build/output"

log "Сборка profile=${PROFILE}, glibc=2.28, jobs=${JOBS}"
"${SUDO[@]}" chroot "${ROOTFS}" /usr/bin/env -i \
    HOME=/root USER=root LOGNAME=root \
    PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    SATDUMP_SOURCE=/build/source \
    SATDUMP_WORK=/build/work \
    SATDUMP_OUTPUT=/build/output \
    SATDUMP_PROFILE="${PROFILE}" \
    SATDUMP_JOBS="${JOBS}" \
    SATDUMP_KEEP_WORK="${KEEP_WORK}" \
    /bin/bash /build/source/scripts/astra/astra17/inside-chroot.sh

cleanup
trap - EXIT INT TERM
log "Готовые артефакты: ${OUTPUT_DIR}"
