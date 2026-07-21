#!/usr/bin/env bash

set -Eeuo pipefail

PORTABLE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../common.sh
source "${PORTABLE_DIR}/../common.sh"
# shellcheck source=lock.env
source "${PORTABLE_DIR}/lock.env"

ROOTFS="/var/lib/satdump-build/stretch-amd64"
MIRROR="${SATDUMP_PORTABLE_DEBIAN_MIRROR}"
ALLOW_UNSIGNED=0
FORCE=0

usage() {
    cat <<EOF
Использование: sudo bash scripts/astra/portable/prepare-rootfs.sh [параметры]

Параметры:
  --rootfs PATH              Каталог chroot (по умолчанию: ${ROOTFS})
  --mirror URL               Зеркало Debian Stretch
  --allow-unsigned-archive   Разрешить legacy-режим без проверки подписи Release
  --force                    Полностью пересоздать ранее подготовленный rootfs
  -h, --help                 Показать справку

По умолчанию используется строгая проверка через debian-archive-keyring.
Параметр --allow-unsigned-archive допустим только для изолированного build-chroot
или утверждённого внутреннего зеркала. Он никогда не изменяет APT хостовой ОС.
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --rootfs) ROOTFS="$2"; shift 2 ;;
        --mirror) MIRROR="$2"; shift 2 ;;
        --allow-unsigned-archive) ALLOW_UNSIGNED=1; shift ;;
        --force) FORCE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "Неизвестный параметр: $1" ;;
    esac
done

[[ "$(uname -m)" == "x86_64" ]] || die "Portable-профиль glibc 2.24 сейчас поддерживает только x86_64."
(( EUID == 0 )) || die "Подготовка chroot требует root. Запустите команду через sudo."
command_exists debootstrap || die "Не найдена команда debootstrap. Установите пакеты debootstrap и debian-archive-keyring."
command_exists chroot || die "Не найдена команда chroot."

MARKER="${ROOTFS}/etc/satdump-portable-rootfs"
if [[ -f "${MARKER}" && "${FORCE}" == "0" ]]; then
    if grep -q "profile_version=${SATDUMP_PORTABLE_PROFILE_VERSION}" "${MARKER}"; then
        log_ok "Rootfs уже подготовлен: ${ROOTFS}"
        exit 0
    fi
    die "Rootfs создан другой версией профиля. Используйте --force после проверки пути."
fi

if [[ -e "${ROOTFS}" && "${FORCE}" == "0" ]]; then
    if [[ -n "$(find "${ROOTFS}" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]]; then
        die "Каталог ${ROOTFS} не пуст и не содержит маркер SatDump. Укажите другой путь или используйте --force осознанно."
    fi
fi

if [[ "${FORCE}" == "1" && -e "${ROOTFS}" ]]; then
    case "${ROOTFS}" in
        /|/usr|/var|/var/lib|/home|/root) die "Отказ удалять опасный путь: ${ROOTFS}" ;;
    esac
    log_warn "Полное удаление rootfs: ${ROOTFS}"
    rm -rf --one-file-system "${ROOTFS}"
fi
mkdir -p "${ROOTFS}"

DEBOOTSTRAP_ARGS=(
    --arch="${SATDUMP_PORTABLE_ARCH}"
    --variant=minbase
)

KEYRING="/usr/share/keyrings/debian-archive-keyring.gpg"
if [[ "${ALLOW_UNSIGNED}" == "1" ]]; then
    DEBOOTSTRAP_ARGS+=(--no-check-gpg)
    log_warn "Включён legacy-режим без проверки подписи архива. Используйте только изолированную среду или доверенное зеркало."
elif [[ -r "${KEYRING}" ]]; then
    DEBOOTSTRAP_ARGS+=(--keyring="${KEYRING}")
else
    die "Не найден ${KEYRING}. Установите debian-archive-keyring либо явно используйте --allow-unsigned-archive."
fi

log_info "Создание Debian ${SATDUMP_PORTABLE_DEBIAN_SUITE} rootfs в ${ROOTFS}"
debootstrap "${DEBOOTSTRAP_ARGS[@]}" \
    "${SATDUMP_PORTABLE_DEBIAN_SUITE}" \
    "${ROOTFS}" \
    "${MIRROR}"

TRUST_OPTION="check-valid-until=no"
if [[ "${ALLOW_UNSIGNED}" == "1" ]]; then
    TRUST_OPTION="trusted=yes check-valid-until=no"
fi

cat > "${ROOTFS}/etc/apt/sources.list" <<EOF
deb [${TRUST_OPTION}] ${MIRROR} ${SATDUMP_PORTABLE_DEBIAN_SUITE} main contrib
deb [${TRUST_OPTION}] ${MIRROR} ${SATDUMP_PORTABLE_DEBIAN_SUITE}-backports main
EOF

cat > "${ROOTFS}/etc/apt/apt.conf.d/99satdump-archive" <<EOF
Acquire::Check-Valid-Until "false";
Acquire::Retries "3";
APT::Get::Assume-Yes "true";
Dpkg::Use-Pty "0";
EOF

if [[ "${ALLOW_UNSIGNED}" == "1" ]]; then
    cat >> "${ROOTFS}/etc/apt/apt.conf.d/99satdump-archive" <<EOF
Acquire::AllowInsecureRepositories "true";
Acquire::AllowDowngradeToInsecureRepositories "true";
APT::Get::AllowUnauthenticated "true";
EOF
fi

if [[ -r /etc/resolv.conf ]]; then
    cp -L /etc/resolv.conf "${ROOTFS}/etc/resolv.conf"
fi

cat > "${MARKER}" <<EOF
profile=satdump-portable-glibc224
profile_version=${SATDUMP_PORTABLE_PROFILE_VERSION}
debian_suite=${SATDUMP_PORTABLE_DEBIAN_SUITE}
architecture=${SATDUMP_PORTABLE_ARCH}
glibc_baseline=${SATDUMP_PORTABLE_GLIBC_BASELINE}
mirror=${MIRROR}
archive_verification=$([[ "${ALLOW_UNSIGNED}" == "1" ]] && printf legacy || printf strict)
created_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

log_ok "Rootfs подготовлен: ${ROOTFS}"
log_info "Следующий шаг: bash scripts/astra/build.sh --mode portable-glibc224 --rootfs '${ROOTFS}'"
