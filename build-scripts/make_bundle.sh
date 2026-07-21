#!/bin/bash
set -e
SD=/opt/satdump
OUT=/build/satdump-astra
rm -rf "$OUT"; mkdir -p "$OUT/bin" "$OUT/lib" "$OUT/lib/satdump/plugins" "$OUT/share"
cp "$SD/bin/satdump" "$OUT/bin/"
cp "$SD/lib/libsatdump_core.so" "$OUT/lib/"
cp "$SD"/lib/satdump/plugins/*.so "$OUT/lib/satdump/plugins/"
cp -r "$SD/share/satdump" "$OUT/share/"
GLIBC_RE='^(ld-linux.*|libc\.|libm\.|libpthread\.|libdl\.|librt\.|libresolv\.|libnsl\.|libnss_|libutil\.|libcrypt\.|libanl\.|libBrokenLocale\.)'
export LD_LIBRARY_PATH="/opt/gcc9/lib64:/usr/local/lib:$SD/lib:$SD/lib/satdump/plugins"
collect() {
  ldd "$1" 2>/dev/null | awk '/=>/{print $3}' | while read -r L; do
    [ -f "$L" ] || continue
    b=$(basename "$L")
    echo "$b" | grep -qE "$GLIBC_RE" && continue
    [ -f "$OUT/lib/$b" ] && continue
    cp -L "$L" "$OUT/lib/$b"
  done
}
for pass in 1 2 3 4; do
  for f in "$OUT"/bin/satdump "$OUT"/lib/*.so* "$OUT"/lib/satdump/plugins/*.so; do collect "$f"; done
done
cp -L /opt/gcc9/lib64/libstdc++.so.6 "$OUT/lib/"
cp -L /opt/gcc9/lib64/libgcc_s.so.1 "$OUT/lib/" 2>/dev/null || true
[ -f /opt/gcc9/lib64/libgomp.so.1 ] && cp -L /opt/gcc9/lib64/libgomp.so.1 "$OUT/lib/" || true
cat > "$OUT/satdump" <<'WRAP'
#!/bin/bash
D="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export LD_LIBRARY_PATH="$D/lib:$D/lib/satdump/plugins:$LD_LIBRARY_PATH"
exec "$D/bin/satdump" "$@"
WRAP
chmod +x "$OUT/satdump"
echo "=== bundle libs count ==="; ls "$OUT/lib"/*.so* 2>/dev/null | wc -l
echo "=== размер ==="; du -sh "$OUT"
echo "=== not found проверка (только bundle libs) ==="
LD_LIBRARY_PATH="$OUT/lib:$OUT/lib/satdump/plugins" ldd "$OUT/bin/satdump" 2>/dev/null | grep -i "not found" || echo "OK: бинарь разрешён"
miss=0
for p in "$OUT"/lib/satdump/plugins/*.so; do
  if LD_LIBRARY_PATH="$OUT/lib" ldd "$p" 2>/dev/null | grep -qi "not found"; then echo "MISS в $(basename "$p")"; miss=1; fi
done
[ "$miss" = 0 ] && echo "OK: плагины разрешены"
