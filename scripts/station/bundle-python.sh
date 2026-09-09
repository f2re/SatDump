#!/usr/bin/env bash
# Run INSIDE the trusted Stretch build root, never on the target workstation.
set -Eeuo pipefail
export LC_ALL=C DEBIAN_FRONTEND=noninteractive
[[ "$(getconf GNU_LIBC_VERSION)" == 'glibc 2.24' ]] || { echo 'Expected Stretch glibc 2.24' >&2; exit 1; }
apt-get update
apt-get install -y --no-install-recommends python3 python3-pil ca-certificates
OUT=/opt/station-python
rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/lib/python3.5/site-packages" "$OUT/licenses"
cp -L /usr/bin/python3.5 "$OUT/bin/python3.5"
cp -aL /usr/lib/python3.5/. "$OUT/lib/python3.5/"
cp -aL /usr/lib/python3/dist-packages/PIL "$OUT/lib/python3.5/site-packages/"
# The portable builder unmounts /proc before this stage. Bash process
# substitution depends on /dev/fd and can silently skip dependencies there.
# Materialise inventories instead; every command now has a checked exit code.
SCAN=$(mktemp -d /tmp/station-python-deps.XXXXXXXX)
trap 'rm -rf -- "$SCAN"' EXIT
find "$OUT/bin" "$OUT/lib/python3.5" -type f -print0 > "$SCAN/files"
while IFS= read -r -d '' file; do
    readelf -h "$file" >/dev/null 2>&1 || continue
    readelf -d "$file" > "$SCAN/dynamic"
    grep -q '(NEEDED)' "$SCAN/dynamic" || continue
    ldd "$file" > "$SCAN/ldd"
    if grep -q 'not found' "$SCAN/ldd"; then
        printf 'Unresolved runtime dependency: %s\n' "$file" >&2
        cat "$SCAN/ldd" >&2
        exit 1
    fi
    awk '/=> \/.*/ {print $1, $3}' "$SCAN/ldd" > "$SCAN/dependencies"
    while read -r name path; do
        case "$name" in ld-linux*|libc.so*|libm.so*|libpthread.so*|libdl.so*|librt.so*|libresolv.so*|libnss_*|libutil.so*|libcrypt.so*|libnsl.so*|libanl.so*|libBrokenLocale.so*) continue ;; esac
        [[ -f $path ]] || { printf 'Missing dependency: %s\n' "$path" >&2; exit 1; }
        cp -L "$path" "$OUT/lib/$name"
    done < "$SCAN/dependencies"
done < "$SCAN/files"
# The chroot may otherwise satisfy these from its own system directories and
# conceal an incomplete bundle until it is started on another machine.
for library in libjpeg.so.62 libpng16.so.16 libssl.so.1.1 libcrypto.so.1.1 libsqlite3.so.0; do
    [[ -f "$OUT/lib/$library" ]] || { printf 'Runtime closure incomplete: %s\n' "$library" >&2; exit 1; }
done
find "$OUT" -type d -name __pycache__ -prune -exec rm -rf '{}' +
find "$OUT" -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete
for doc in /usr/share/doc/*/copyright; do
    [[ -f $doc ]] || continue
    cp -L "$doc" "$OUT/licenses/$(basename "$(dirname "$doc")").copyright"
done
dpkg-query -W -f='${Package}\t${Version}\n' > "$OUT/licenses/build-packages.tsv"
cat > "$OUT/python" <<'LAUNCHER'
#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
unset PYTHONPATH
export LC_ALL=C.UTF-8 LANG=C.UTF-8
export PYTHONHOME="$ROOT" PYTHONNOUSERSITE=1 PYTHONDONTWRITEBYTECODE=1
# Debian's patched site.py uses dist-packages, not upstream site-packages.
# Set only our private path, never inherit a caller-controlled PYTHONPATH.
export PYTHONPATH="$ROOT/lib/python3.5/site-packages"
export LD_LIBRARY_PATH="$ROOT/lib"
exec "$ROOT/bin/python3.5" "$@"
LAUNCHER
chmod +x "$OUT/python"
"$OUT/python" - <<'PY'
import encodings, io, sqlite3, ssl
from PIL import Image
for format_name in ('PNG', 'JPEG'):
    with io.BytesIO() as stream:
        Image.new('RGB', (4, 4)).save(stream, format_name)
        stream.seek(0)
        with Image.open(stream) as image:
            image.load()
            assert image.size == (4, 4)
print('Embedded Python/Pillow and PNG/JPEG codecs OK')
PY
