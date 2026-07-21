#!/usr/bin/env bash

set -Eeuo pipefail

PORTABLE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../common.sh
source "${PORTABLE_DIR}/../common.sh"
# shellcheck source=lock.env
source "${PORTABLE_DIR}/lock.env"

ROOTFS="/var/lib/satdump-build/stretch-amd64"
WORK_DIR="${SATDUMP_ROOT}/build/portable-glibc224"
OUTPUT_DIR="${SATDUMP_ROOT}/dist/astra-portable"
CACHE_DIR="${HOME}/.cache/satdump-portable"
OFFLINE_DIR=""
PLUGIN_PROFILE="reference"
JOBS="$(jobs_count)"
CLEAN_ROOTFS=0
CLEAN_WORK=1
PREPARE_ONLY=0
ALLOW_UNSIGNED=0
MIRROR="${SATDUMP_PORTABLE_DEBIAN_MIRROR}"

usage() {
    cat <<EOF
Использование: bash scripts/astra/build.sh --mode portable-glibc224 [параметры]

Параметры portable-профиля:
  --profile reference|meteor   reference повторяет рабочую сборку ветки astra;
                               meteor собирает минимальный набор Meteor/NOAA/APT
  --rootfs PATH                Debian Stretch chroot (по умолчанию: ${ROOTFS})
  --work-dir PATH              build/staging-каталог хоста
  --output-dir PATH            каталог готового бандла и tar.gz
  --cache-dir PATH             проверяемые архивы toolchain
  --offline-dir PATH           архивы и nng-v1.8.0.bundle для закрытого контура
  --jobs N                     параллельные задачи
  --clean-rootfs               пересоздать chroot и toolchain
  --keep-work                  не очищать build/staging перед сборкой
  --prepare-only               только подготовить chroot
  --mirror URL                 зеркало Debian Stretch
  --allow-unsigned-archive     legacy-режим архива; только для изолированной среды
  -h, --help                   показать справку

Результат:
  ${OUTPUT_DIR}/satdump-1.2.2-presentation-<profile>-glibc224-x86_64.tar.gz
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --mode) shift 2 ;; # уже обработан верхним scripts/astra/build.sh
        --profile) PLUGIN_PROFILE="$2"; shift 2 ;;
        --rootfs) ROOTFS="$2"; shift 2 ;;
        --work-dir) WORK_DIR="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --cache-dir) CACHE_DIR="$2"; shift 2 ;;
        --offline-dir) OFFLINE_DIR="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --clean-rootfs) CLEAN_ROOTFS=1; shift ;;
        --clean|--clean-work) CLEAN_WORK=1; shift ;;
        --keep-work) CLEAN_WORK=0; shift ;;
        --prepare-only) PREPARE_ONLY=1; shift ;;
        --mirror) MIRROR="$2"; shift 2 ;;
        --allow-unsigned-archive) ALLOW_UNSIGNED=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "Неизвестный portable-параметр: $1" ;;
    esac
done

[[ "${PLUGIN_PROFILE}" == "reference" || "${PLUGIN_PROFILE}" == "meteor" ]] \
    || die "--profile: reference или meteor"
[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || die "--jobs должен быть положительным целым числом."
[[ "$(uname -m)" == "x86_64" ]] || die "Portable glibc224 поддерживает только x86_64."

if (( EUID == 0 )); then
    SUDO=()
    OWNER_UID="${SUDO_UID:-0}"
    OWNER_GID="${SUDO_GID:-0}"
elif command_exists sudo; then
    SUDO=(sudo)
    OWNER_UID="$(id -u)"
    OWNER_GID="$(id -g)"
else
    die "Portable-сборка требует root для chroot/mount. Установите sudo или запустите от root."
fi

for command in debootstrap chroot mount umount rsync tar gzip sha256sum; do
    command_exists "${command}" || die "Не найдена команда ${command}. Установите debootstrap, debian-archive-keyring, rsync и build tools."
done

ensure_directory "${WORK_DIR}"
ensure_directory "${OUTPUT_DIR}"
ensure_directory "${CACHE_DIR}"
if [[ -n "${OFFLINE_DIR}" ]]; then
    [[ -d "${OFFLINE_DIR}" ]] || die "Не найден offline-dir: ${OFFLINE_DIR}"
    OFFLINE_DIR="$(readlink -f "${OFFLINE_DIR}")"
fi

PREPARE_ARGS=(
    --rootfs "${ROOTFS}"
    --mirror "${MIRROR}"
)
[[ "${CLEAN_ROOTFS}" == "1" ]] && PREPARE_ARGS+=(--force)
[[ "${ALLOW_UNSIGNED}" == "1" ]] && PREPARE_ARGS+=(--allow-unsigned-archive)

"${SUDO[@]}" bash "${PORTABLE_DIR}/prepare-rootfs.sh" "${PREPARE_ARGS[@]}"
[[ "${PREPARE_ONLY}" == "0" ]] || exit 0

ROOTFS="$(readlink -f "${ROOTFS}")"
SOURCE_REAL="$(readlink -f "${SATDUMP_ROOT}")"
WORK_REAL="$(readlink -f "${WORK_DIR}")"
OUTPUT_REAL="$(readlink -f "${OUTPUT_DIR}")"
CACHE_REAL="$(readlink -f "${CACHE_DIR}")"

MOUNTED=()
mount_bind() {
    local source="$1"
    local target="$2"
    local readonly="${3:-0}"
    "${SUDO[@]}" mkdir -p "${target}"
    "${SUDO[@]}" mount --bind "${source}" "${target}"
    MOUNTED+=("${target}")
    if [[ "${readonly}" == "1" ]]; then
        "${SUDO[@]}" mount -o remount,bind,ro "${target}"
    fi
}

mount_rbind() {
    local source="$1"
    local target="$2"
    "${SUDO[@]}" mkdir -p "${target}"
    "${SUDO[@]}" mount --rbind "${source}" "${target}"
    "${SUDO[@]}" mount --make-rslave "${target}"
    MOUNTED+=("${target}")
}

cleanup_mounts() {
    local index target
    for (( index=${#MOUNTED[@]}-1; index>=0; index-- )); do
        target="${MOUNTED[index]}"
        "${SUDO[@]}" umount -l "${target}" >/dev/null 2>&1 || true
    done
}
trap cleanup_mounts EXIT INT TERM

mount_rbind /dev "${ROOTFS}/dev"
mount_rbind /sys "${ROOTFS}/sys"
"${SUDO[@]}" mkdir -p "${ROOTFS}/proc"
"${SUDO[@]}" mount -t proc proc "${ROOTFS}/proc"
MOUNTED+=("${ROOTFS}/proc")

mount_bind "${SOURCE_REAL}" "${ROOTFS}/build/source" 1
mount_bind "${WORK_REAL}" "${ROOTFS}/build/work"
mount_bind "${OUTPUT_REAL}" "${ROOTFS}/build/output"
mount_bind "${CACHE_REAL}" "${ROOTFS}/build/cache"
if [[ -n "${OFFLINE_DIR}" ]]; then
    mount_bind "${OFFLINE_DIR}" "${ROOTFS}/build/offline" 1
fi

log_info "Portable-профиль: Debian Stretch/glibc ${SATDUMP_PORTABLE_GLIBC_BASELINE}; plugins=${PLUGIN_PROFILE}"
log_info "rootfs=${ROOTFS}"
log_info "work=${WORK_REAL}"
log_info "output=${OUTPUT_REAL}"

CHROOT_ENV=(
    HOME=/root
    USER=root
    LOGNAME=root
    PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
    SATDUMP_SOURCE=/build/source
    SATDUMP_WORK=/build/work
    SATDUMP_OUTPUT=/build/output
    SATDUMP_CACHE=/build/cache
    SATDUMP_JOBS="${JOBS}"
    SATDUMP_PORTABLE_PLUGIN_PROFILE="${PLUGIN_PROFILE}"
    SATDUMP_CLEAN_WORK="${CLEAN_WORK}"
)
if [[ -n "${OFFLINE_DIR}" ]]; then
    CHROOT_ENV+=(SATDUMP_OFFLINE=/build/offline)
fi

"${SUDO[@]}" chroot "${ROOTFS}" /usr/bin/env -i "${CHROOT_ENV[@]}" \
    /bin/bash /build/source/scripts/astra/portable/inside-chroot.sh

cleanup_mounts
trap - EXIT INT TERM

# Артефакты создавались root внутри chroot; возвращаем их пользователю, который
# запустил сборку. Системный rootfs и toolchain остаются root-owned.
"${SUDO[@]}" chown -R "${OWNER_UID}:${OWNER_GID}" "${WORK_REAL}" "${OUTPUT_REAL}"

log_ok "Portable-бандл собран: ${OUTPUT_REAL}"
log_info "Проверка на целевой Astra: bash scripts/astra/portable/validate-bundle.sh '${OUTPUT_REAL}/satdump-1.2.2-presentation-${PLUGIN_PROFILE}-glibc224-x86_64'"
