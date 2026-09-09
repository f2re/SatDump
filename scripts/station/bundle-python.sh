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
while IFS= read -r -d '' file; do
    readelf -h "$file" >/dev/null 2>&1 || continue
    while read -r name path; do
        case "$name" in ld-linux*|libc.so*|libm.so*|libpthread.so*|libdl.so*|librt.so*|libresolv.so*|libnss_*|libutil.so*|libcrypt.so*|libnsl.so*|libanl.so*|libBrokenLocale.so*) continue ;; esac
        [[ -f $path ]] || continue
        cp -L "$path" "$OUT/lib/$name"
    done < <(ldd "$file" | awk '/=> \/.*/ {print $1, $3}')
done < <(find "$OUT/bin" "$OUT/lib/python3.5" -type f -print0)
find "$OUT" -type d -name __pycache__ -prune -exec rm -rf '{}' +
find "$OUT" -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete
for doc in /usr/share/doc/*/copyright; do
    [[ -f $doc ]] || continue
    cp -L "$doc" "$OUT/licenses/$(basename "$(dirname "$doc")").copyright"
done
dpkg-query -W -f='${Package}\t${Version}\n' > "$OUT/licenses/build-packages.tsv"
cat > "$OUT/python" <<'EOF'
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
EOF
chmod +x "$OUT/python"
"$OUT/python" -c 'import encodings,sqlite3,ssl;from PIL import Image;print("Embedded Python/Pillow OK")'
