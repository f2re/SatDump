#!/usr/bin/env bash
set -Eeuo pipefail
export PATH="/usr/local/sbin:/usr/sbin:/sbin:${PATH}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ROOTFS=/var/lib/satdump-build/station-stretch-amd64
WORK="$ROOT/build/station"
OUTPUT="$ROOT/dist/station"
JOBS=2
PROFILE=reference
while (( $# )); do
    case "$1" in
        --jobs) JOBS="${2:?}"; shift 2 ;;
        --output-dir) OUTPUT="${2:?}"; shift 2 ;;
        --profile) PROFILE="${2:?}"; shift 2 ;;
        --rootfs) ROOTFS="${2:?}"; shift 2 ;;
        -h|--help) echo './station.sh build [--jobs 2] [--profile reference|meteor] [--output-dir DIR] [--rootfs DIR]'; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
[[ "$JOBS" =~ ^[1-9][0-9]*$ && ( "$PROFILE" == reference || "$PROFILE" == meteor ) ]] || exit 2
[[ "$(uname -m)" == x86_64 ]] || { echo 'x86_64 build host required' >&2; exit 1; }
[[ "$ROOTFS" =~ ^/[A-Za-z0-9_./-]+$ && "$ROOTFS" != *..* ]] || exit 2
for tool in debootstrap rsync readelf python3 git chroot; do command -v "$tool" >/dev/null || { echo "Install build prerequisite: $tool" >&2; exit 1; }; done
SUDO=(); (( EUID == 0 )) || SUDO=(sudo)
mkdir -p "$WORK" "$OUTPUT"
REVISION="$(git -C "$ROOT" rev-parse HEAD)"
[[ -z "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]] || { echo 'Build a clean committed tree' >&2; exit 1; }
export SOURCE_DATE_EPOCH="$(git -C "$ROOT" show -s --format=%ct HEAD)"
"${SUDO[@]}" bash "$ROOT/scripts/astra/portable/build.sh" \
    --rootfs "$ROOTFS" --work-dir "$WORK/engine-build" --output-dir "$WORK/engine-dist" \
    --cache-dir "$WORK/downloads" --profile "$PROFILE" --jobs "$JOBS"
"${SUDO[@]}" cp "$ROOT/scripts/station/bundle-python.sh" "$ROOTFS/tmp/station-python.sh"
"${SUDO[@]}" chroot "$ROOTFS" /bin/bash /tmp/station-python.sh
# Packaging inside the rootfs would hide accidental dependencies on the build host.
# Run the embedded interpreter on the host and later again in a clean runtime test.
ENGINE="$WORK/engine-dist/satdump-1.2.2-presentation-$PROFILE-glibc224-x86_64"
"${SUDO[@]}" env SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" python3 "$ROOT/scripts/station/pack.py" \
    --engine "$ENGINE" --runtime "$ROOTFS/opt/station-python" --output "$OUTPUT" --revision "$REVISION"
"${SUDO[@]}" chown -R "${SUDO_UID:-$(id -u)}:${SUDO_GID:-$(id -g)}" "$OUTPUT"
printf 'Готовый офлайн-пакет: %s\n' "$OUTPUT"
