#!/usr/bin/env bash
# Explicit authenticated destination; no agent forwarding or disabled host checking.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ARCHIVE=${1:-} HOST=${2:-}
[[ -f $ARCHIVE && $HOST =~ ^[A-Za-z0-9_.-]+@[A-Za-z0-9_.-]+$ && $HOST != -* ]] || {
    echo 'Использование: ./station.sh deploy ARCHIVE USER@HOST -- [параметры установщика]' >&2; exit 2;
}
shift 2
if [[ ${1:-} == -- ]]; then shift; fi
OPTIONS=(--non-interactive)
while (( $# )); do
    case "$1" in
        --data-dir|--listen|--port|--control-port|--backend-port|--web-server|--source-path|--source-kind)
            [[ $# -ge 2 && $2 != --* && $2 =~ ^[A-Za-z0-9_./-]+$ ]] || { echo 'Некорректный параметр установки' >&2; exit 2; }
            OPTIONS+=("$1" "$2"); shift 2 ;;
        --no-start|--no-color|--no-animation|--allow-compatible|--yes|--non-interactive) OPTIONS+=("$1"); shift ;;
        *) printf 'Параметр не разрешён для SSH: %s\n' "$1" >&2; exit 2 ;;
    esac
done
[[ -f $ARCHIVE.sha256 ]] || { echo 'Нет .sha256 рядом с архивом' >&2; exit 1; }
(cd "$(dirname "$ARCHIVE")"; sha256sum -c "$(basename "$ARCHIVE").sha256")
SHA=$(sha256sum "$ARCHIVE" | awk '{print $1}')
PYTHON=python3
[[ ! -x $ROOT/runtime/python ]] || PYTHON="$ROOT/runtime/python"
SSH_OPTIONS=(-o BatchMode=yes -o StrictHostKeyChecking=yes -o ConnectTimeout=15 -o ForwardAgent=no)
if [[ -n ${SSH_KNOWN_HOSTS:-} ]]; then
    [[ -r $SSH_KNOWN_HOSTS ]] || { echo 'Нет файла known_hosts' >&2; exit 1; }
    SSH_OPTIONS+=(-o "UserKnownHostsFile=$SSH_KNOWN_HOSTS")
fi
"$PYTHON" - "$ARCHIVE" <<'PY'
import sys, tarfile, posixpath
roots = set()
with tarfile.open(sys.argv[1]) as archive:
    for member in archive:
        name = posixpath.normpath(member.name)
        if name.startswith('/') or name == '..' or name.startswith('../') or member.isdev() or member.isfifo():
            raise SystemExit('Unsafe archive member')
        roots.add(name.split('/')[0])
        if member.issym() or member.islnk():
            target = posixpath.normpath(member.linkname if member.islnk() else posixpath.join(posixpath.dirname(name), member.linkname))
            if target.startswith('/') or target.split('/')[0] != name.split('/')[0]:
                raise SystemExit('Unsafe archive link')
if len(roots) != 1 or not next(iter(roots)).startswith('satdump-'):
    raise SystemExit('Expected one satdump-* top-level directory')
PY
REMOTE=''
cleanup() {
    local rc=$?
    trap - EXIT
    if [[ $REMOTE =~ ^/tmp/satdump-deploy\.[A-Za-z0-9]+$ ]]; then
        ssh "${SSH_OPTIONS[@]}" "$HOST" "rm -rf -- '$REMOTE'" || echo "Не удалось удалить временный каталог $REMOTE" >&2
    fi
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
ssh "${SSH_OPTIONS[@]}" "$HOST" 'command -v bash >/dev/null && command -v sha256sum >/dev/null && sudo -n true'
REMOTE=$(ssh "${SSH_OPTIONS[@]}" "$HOST" 'mktemp -d /tmp/satdump-deploy.XXXXXXXX')
[[ $REMOTE =~ ^/tmp/satdump-deploy\.[A-Za-z0-9]+$ ]] || { echo 'Некорректный временный путь' >&2; exit 1; }
printf 'Передача проверенного пакета на %s\n' "$HOST"
scp "${SSH_OPTIONS[@]}" "$ARCHIVE" "$HOST:$REMOTE/package.tar.gz"
# POSIX quoting survives the remote login shell; arguments remain separate data.
REMOTE_COMMAND=$("$PYTHON" - "$REMOTE" "$SHA" "${OPTIONS[@]}" <<'PY'
import shlex, sys
print('/bin/bash -s -- ' + ' '.join(shlex.quote(x) for x in sys.argv[1:]))
PY
)
ssh "${SSH_OPTIONS[@]}" "$HOST" "$REMOTE_COMMAND" <<'REMOTE_SCRIPT'
set -Eeuo pipefail
cd "$1"
printf '%s  package.tar.gz\n' "$2" | sha256sum -c -
shift 2
mkdir extracted
tar --no-same-owner --no-same-permissions -xzf package.tar.gz -C extracted
mapfile -t packages < <(find extracted -mindepth 1 -maxdepth 1 -type d -name 'satdump-*')
[[ ${#packages[@]} == 1 ]]
sudo -n bash "${packages[0]}/install.sh" "$@"
REMOTE_SCRIPT
