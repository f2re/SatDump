#!/usr/bin/env bash

set -Eeuo pipefail

# Astra user sessions may omit the administrative directories from PATH even
# though debootstrap and chroot are installed there.
export PATH="/usr/local/sbin:/usr/sbin:/sbin:${PATH}"

PORTABLE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../common.sh
source "${PORTABLE_DIR}/../common.sh"
# shellcheck source=lock.env
source "${PORTABLE_DIR}/lock.env"

ORIGINAL_ARGS=("$@")
ROOTFS="${SATDUMP_PORTABLE_ROOTFS:-/tmp/satdump-portable-rootfs/stretch-amd64}"
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
elif command_exists unshare && unshare --user --map-root-user true >/dev/null 2>&1; then
    command_exists fakeroot \
        || die "Для rootless portable-сборки требуется пакет fakeroot."
    log_info "Перезапуск в rootless user/mount namespace; sudo не требуется"
    exec unshare --user --map-root-user --mount \
        env SATDUMP_PORTABLE_USERNS=1 \
        bash "${PORTABLE_DIR}/build.sh" "${ORIGINAL_ARGS[@]}"
elif command_exists sudo; then
    SUDO=(sudo)
    OWNER_UID="$(id -u)"
    OWNER_GID="$(id -g)"
else
    die "Portable-сборка требует root для chroot/mount. Установите sudo или запустите от root."
fi

if [[ "${SATDUMP_PORTABLE_USERNS:-0}" == "1" ]]; then
    # Do not propagate mount operations from the private build namespace back
    # to the host mount tree.
    mount --make-rprivate /
fi

for command in debootstrap chroot mount umount findmnt rsync tar gzip sha256sum readlink; do
    command_exists "${command}" || die "Не найдена команда ${command}. Установите debootstrap, debian-archive-keyring, util-linux, rsync и build tools."
done

absolute_path() {
    readlink -m "$1"
}

ROOTFS="$(absolute_path "${ROOTFS}")"
WORK_DIR="$(absolute_path "${WORK_DIR}")"
OUTPUT_DIR="$(absolute_path "${OUTPUT_DIR}")"
CACHE_DIR="$(absolute_path "${CACHE_DIR}")"
SOURCE_REAL="$(readlink -f "${SATDUMP_ROOT}")"

[[ "${ROOTFS}" != *[$'\t\r\n ']* ]] || die "Путь rootfs не должен содержать пробельные символы: ${ROOTFS}"

paths_overlap() {
    local left="${1%/}/"
    local right="${2%/}/"
    [[ "${left}" == "${right}"* || "${right}" == "${left}"* ]]
}

for host_path in "${SOURCE_REAL}" "${WORK_DIR}" "${OUTPUT_DIR}" "${CACHE_DIR}"; do
    if paths_overlap "${ROOTFS}" "${host_path}"; then
        die "rootfs и bind-mounted каталог не должны пересекаться: ${ROOTFS} ↔ ${host_path}"
    fi
done

ensure_directory "${WORK_DIR}"
ensure_directory "${OUTPUT_DIR}"
ensure_directory "${CACHE_DIR}"
if [[ -n "${OFFLINE_DIR}" ]]; then
    [[ -d "${OFFLINE_DIR}" ]] || die "Не найден offline-dir: ${OFFLINE_DIR}"
    OFFLINE_DIR="$(readlink -f "${OFFLINE_DIR}")"
    if paths_overlap "${ROOTFS}" "${OFFLINE_DIR}"; then
        die "offline-dir не должен находиться внутри rootfs и наоборот."
    fi
fi

unmount_rootfs_children() {
    [[ -d "${ROOTFS}" ]] || return 0

    # Не используем `findmnt -R ${ROOTFS}`: если сам ROOTFS не является отдельным
    # mountpoint, findmnt может выбрать родительскую файловую систему. Сначала
    # получаем полный список mountpoints, затем строго фильтруем только пути,
    # начинающиеся с `${ROOTFS}/`. Сам ROOTFS никогда не размонтируется.
    local root_prefix="${ROOTFS%/}/"
    local target
    local -a stale_mounts=()
    while IFS= read -r target; do
        [[ "${target}" == "${root_prefix}"* ]] || continue
        stale_mounts+=("${target}")
    done < <(
        findmnt -rn -o TARGET \
            | awk '{ print length($0), $0 }' \
            | sort -rn \
            | cut -d' ' -f2-
    )

    for target in "${stale_mounts[@]}"; do
        [[ -n "${target}" ]] || continue
        log_warn "Отключение оставшегося дочернего mount: ${target}"
        "${SUDO[@]}" umount -l -- "${target}" || die "Не удалось отключить ${target}"
    done
}

if [[ "${CLEAN_ROOTFS}" == "1" ]]; then
    unmount_rootfs_children
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
        "${SUDO[@]}" umount -l -- "${target}" >/dev/null 2>&1 || true
    done
    MOUNTED=()
}

restore_ownership() {
    local path
    for path in "${WORK_REAL}" "${OUTPUT_REAL}" "${CACHE_REAL}"; do
        [[ -e "${path}" ]] || continue
        "${SUDO[@]}" chown -R "${OWNER_UID}:${OWNER_GID}" "${path}" >/dev/null 2>&1 || true
    done
}

cleanup_host_state() {
    cleanup_mounts
    restore_ownership
}

trap cleanup_host_state EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Перед новым запуском устраняем следы прерванной предыдущей сборки, но не
# удаляем сам rootfs/toolchain.
unmount_rootfs_children

mount_rbind /dev "${ROOTFS}/dev"
mount_rbind /sys "${ROOTFS}/sys"
if [[ "${SATDUMP_PORTABLE_USERNS:-0}" == "1" ]]; then
    # Astra's user namespace permits binding the existing procfs but rejects a
    # fresh proc mount.  The private mount namespace prevents propagation.
    mount_rbind /proc "${ROOTFS}/proc"
else
    "${SUDO[@]}" mkdir -p "${ROOTFS}/proc"
    "${SUDO[@]}" mount -t proc proc "${ROOTFS}/proc"
    MOUNTED+=("${ROOTFS}/proc")
fi

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
CHROOT_PREFIX=("${SUDO[@]}")
if [[ "${SATDUMP_PORTABLE_USERNS:-0}" == "1" ]]; then
    CHROOT_ENV+=(SATDUMP_PORTABLE_ROOTLESS=1)
    CHROOT_PREFIX=(env FAKEROOTDONTTRYCHOWN=1 fakeroot)
fi
if [[ -n "${OFFLINE_DIR}" ]]; then
    CHROOT_ENV+=(SATDUMP_OFFLINE=/build/offline)
fi

"${CHROOT_PREFIX[@]}" chroot "${ROOTFS}" /usr/bin/env -i "${CHROOT_ENV[@]}" \
    /bin/bash /build/source/scripts/astra/portable/inside-chroot.sh

cleanup_host_state
trap - EXIT INT TERM

log_ok "Portable-бандл собран: ${OUTPUT_REAL}"
log_info "Проверка на целевой Astra: bash scripts/astra/portable/validate-bundle.sh '${OUTPUT_REAL}/satdump-1.2.2-presentation-${PLUGIN_PROFILE}-glibc224-x86_64'"
