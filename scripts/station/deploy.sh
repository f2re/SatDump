#!/usr/bin/env bash
# Only an explicitly specified trusted SSH destination. Never disable host-key checks.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ARCHIVE="${1:-}"
HOST="${2:-}"
[[ -f "$ARCHIVE" && "$HOST" =~ ^[A-Za-z0-9_.@:-]+$ && "$HOST" != -* ]] || {
    echo 'Использование: ./station.sh deploy ARCHIVE USER@HOST (нужны SSH и sudo -n на целевой машине)' >&2; exit 2;
}
[[ -f "$ARCHIVE.sha256" ]] || { echo 'Missing archive checksum' >&2; exit 1; }
(cd "$(dirname "$ARCHIVE")"; sha256sum -c "$(basename "$ARCHIVE").sha256")
SHA="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
PYTHON=python3
[[ ! -x "$ROOT/runtime/python" ]] || PYTHON="$ROOT/runtime/python"
SSH_OPTIONS=(-o BatchMode=yes -o StrictHostKeyChecking=yes)
if [[ -n "${SSH_KNOWN_HOSTS:-}" ]]; then
    [[ -r "$SSH_KNOWN_HOSTS" ]] || { echo 'Known-hosts file is unreadable' >&2; exit 1; }
    SSH_OPTIONS+=(-o "UserKnownHostsFile=$SSH_KNOWN_HOSTS")
fi
# Refuse unsafe archives before transferring. Packages are trusted administrator input.
"$PYTHON" - "$ARCHIVE" <<'PY'
import sys,tarfile,posixpath
with tarfile.open(sys.argv[1]) as archive:
    for m in archive:
        name=posixpath.normpath(m.name)
        if name.startswith('/') or name=='..' or name.startswith('../') or m.isdev() or m.isfifo():
            raise SystemExit('Unsafe archive member')
        if m.issym() or m.islnk():
            target=posixpath.normpath(m.linkname if m.islnk() else posixpath.join(posixpath.dirname(name),m.linkname))
            if target.startswith('/') or target.split('/')[0] != name.split('/')[0]:
                raise SystemExit('Unsafe archive link')
PY
remote="$(ssh "${SSH_OPTIONS[@]}" "$HOST" 'mktemp -d /tmp/satdump-deploy.XXXXXXXX')"
[[ "$remote" =~ ^/tmp/satdump-deploy\.[A-Za-z0-9]+$ ]] || { echo 'Unexpected remote directory' >&2; exit 1; }
scp "${SSH_OPTIONS[@]}" "$ARCHIVE" "$HOST:$remote/package.tar.gz"
ssh "${SSH_OPTIONS[@]}" "$HOST" /bin/bash -s -- "$remote" "$SHA" <<'REMOTE'
set -Eeuo pipefail
cd "$1"
printf '%s  package.tar.gz\n' "$2" | sha256sum -c -
mkdir extracted
tar --no-same-owner -xzf package.tar.gz -C extracted
mapfile -t packages < <(find extracted -mindepth 1 -maxdepth 1 -type d -name 'satdump-*')
[[ ${#packages[@]} == 1 ]]
sudo -n bash "${packages[0]}/install.sh"
cd /
rm -rf -- "$1"
REMOTE
