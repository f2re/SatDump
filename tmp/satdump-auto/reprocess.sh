#!/bin/bash
# reprocess.sh <path-to-dat>
# Декодирует CADU .dat через SatDump и строит equirectangular с оверлеями.
# Выход: /opt/geo-products/<yyyy-mm-dd>/<Спутник>_<ГГММДД>_<ЧЧММСС>/
#        (сеансы разложены по дата-каталогам yyyy-mm-dd = дата приёма)
set -u

DAT="${1:-}"
SATDUMP=/opt/satdump/satdump
PRODUCTS=/opt/geo-products
TLE_SRC=/opt/Receiver/Tlg/weather.txt
TLE_DST=/root/.config/satdump/satdump_tles.txt
LOG=/opt/satdump-auto/reprocess.log
DECODE_TIMEOUT=2400    # предел на декод (сек, 40 мин); страховка от редкого зависания SatDump в оверлее
PROJ_TIMEOUT=900       # предел на проекцию (сек, 15 мин)

log(){ echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOG"; }

[ -n "$DAT" ] && [ -f "$DAT" ] || { log "НЕТ файла: $DAT"; exit 1; }

base="$(basename "$DAT")"; base="${base%.dat}"     # METEOR-M2 3_DMW_260528_101703
tm="${base##*_}";  rest="${base%_*}"
dt="${rest##*_}";  rest="${rest%_*}"
band="${rest##*_}"; sat="${rest%_*}"

# спутник -> пайплайн / уровень / прибор / уравнение композита для проекции
case "$sat" in
  "METEOR-M2 2"|"METEOR-M2 3"|"METEOR-M2 4"|"METEOR-M23"|"METEOR-M24")
      PIPE=meteor_hrpt; LEVEL=cadu; INST=MSU-MR; EQN="ch3, ch2, ch1" ;;
  "METOP-A"|"METOP-B"|"METOP-C")
      PIPE=metop_ahrpt; LEVEL=cadu; INST=AVHRR;  EQN="ch2, ch2, ch1" ;;
  "NOAA-15"|"NOAA-18"|"NOAA-19")
      PIPE=noaa_hrpt;  LEVEL=cadu; INST=AVHRR;  EQN="ch2, ch2, ch1" ;;
  *)  log "НЕИЗВЕСТНЫЙ спутник '$sat' (band=$band) — пропуск $base"; exit 2 ;;
esac
if [ "$band" = "LRPT" ]; then
  log "LRPT (VHF) пока не автоматизирован — пропуск $base"; exit 3
fi

DATEDIR="20${dt:0:2}-${dt:2:2}-${dt:4:2}"          # dt=ГГММДД -> yyyy-mm-dd (дата приёма)
OUT="$PRODUCTS/${DATEDIR}/${sat}_${dt}_${tm}"
EQP="$OUT/${sat}_${dt}_${tm}_equirect.png"
# Защита от повтора с учётом оформленных CLI-продуктов. Старые каталоги, где уже
# есть проекция, но нет Full/Compact, должны быть переобработаны новой версией.
has_geo=0; has_minimal=0; has_presentation=0
{ [ -f "$EQP" ] || [ -f "$OUT/${sat}_${dt}_${tm}_stereo.png" ]; } && has_geo=1
[ -d "$OUT" ] && find "$OUT" -type f -name '*_annotated_minimal.png' -print -quit | grep -q . && has_minimal=1
[ -d "$OUT" ] && find "$OUT" -type f -name '*_annotated_presentation.png' -print -quit | grep -q . && has_presentation=1
if [ "$has_geo" -eq 1 ] && [ "$has_minimal" -eq 1 ] && [ "$has_presentation" -eq 1 ]; then
  log "уже полностью обработано (карта + CLI Full/Compact), пропуск $base"
  exit 0
fi
mkdir -p "$OUT"
log "СТАРТ $base -> $OUT  (pipe=$PIPE level=$LEVEL)"

# TLE из телеграммы Receiver
mkdir -p "$(dirname "$TLE_DST")"
cp -f "$TLE_SRC" "$TLE_DST" 2>/dev/null

# 1) декод CADU -> продукты (каналы + композиты, включая *_corrected_map)
#    timeout — страховка: при редком зависании SatDump в отрисовке оверлея процесс
#    прервётся, rc=124 -> проекция пропускается, конвейер (watch.sh) не встаёт.
nice -n 15 ionice -c3 timeout "$DECODE_TIMEOUT" "$SATDUMP" "$PIPE" "$LEVEL" "$DAT" "$OUT" >> "$LOG" 2>&1
rc=$?
[ "$rc" = 124 ] && log "ВНИМАНИЕ: декод прерван по timeout (${DECODE_TIMEOUT}s) — вероятно зависание SatDump"
log "decode rc=$rc"

# отключение MTVZA: удаляем его продукты (раздувают METEOR-папку до ~1.3 ГБ)
[ -d "$OUT/MTVZA" ] && rm -rf "$OUT/MTVZA" && log "MTVZA удалён"

# 2) equirectangular с АВТО-ПОДБОРОМ границ и разрешения под полосу пролёта
#    ТОЛЬКО при успешном декоде (rc=0) — иначе битый продукт даёт отрицательные
#    размеры и SIGSEGV в проекции
CBOR="$OUT/$INST/product.cbor"
OVL="--draw_map_overlay --draw_cities_overlay --draw_latlon_overlay --draw_qth_overlay true"
if [ "$rc" = 0 ] && [ -f "$CBOR" ]; then
  PLOG="$OUT/.project.log"
  # 2a) equirect с авто-подбором
  nice -n 15 ionice -c3 timeout "$PROJ_TIMEOUT" "$SATDUMP" project -layer --type product --file "$CBOR" --equation "$EQN" \
      -target --type equirec --auto_mode true --auto_scale_mode true $OVL --file "$EQP" > "$PLOG" 2>&1
  log "project(equirec) rc=$? -> $(basename "$EQP")"
  W=0; H=0
  if [ -f "$EQP" ]; then
    set -- $(od -An -j16 -N8 -tu1 "$EQP")
    W=$(( $1*16777216 + $2*65536 + $3*256 + $4 )); H=$(( $5*16777216 + $6*65536 + $7*256 + $8 ))
  fi
  if [ "$W" -gt 9000 ]; then
    # 2b) полярный/вырожденный equirect (полоса достаёт полюс) -> стереографическая проекция
    rm -f "$EQP" "${EQP%.png}.wld" "${EQP%.png}.prj" "${EQP%.png}.tif"
    STP="$OUT/${sat}_${dt}_${tm}_stereo.png"
    nice -n 15 ionice -c3 timeout "$PROJ_TIMEOUT" "$SATDUMP" project -layer --type product --file "$CBOR" --equation "$EQN" \
        -target --type stereo --auto_mode true --width 3000 --height 3000 $OVL --file "$STP" > "$PLOG" 2>&1
    log "полярный пролёт (equirect был ${W}px) -> стерео rc=$? $(basename "$STP")"
    # для стерео world-file не строим (проекция не EPSG:4326-affine); привязка визуальная (оверлеи)
  else
    # 2c) equirect в норме -> world-file/GeoTIFF из границ (Final Bounds) + размеров PNG
    B=$(grep -a "Final Bounds are" "$PLOG" | tail -1 | sed -E 's/.*Final Bounds are : //')
    if [ -n "$B" ] && [ -f "$EQP" ]; then
      L="${B% - *}"; R="${B#* - }"
      minlon="${L%,*}"; minlat="${L#*, }"; maxlon="${R%,*}"; maxlat="${R#*, }"
      awk -v mnx="$minlon" -v mny="$minlat" -v mxx="$maxlon" -v mxy="$maxlat" -v w="$W" -v h="$H" \
        'BEGIN{sx=(mxx-mnx)/w; sy=-(mxy-mny)/h; printf "%.10f\n0\n0\n%.10f\n%.10f\n%.10f\n", sx, sy, mnx+sx/2, mxy+sy/2}' \
        > "${EQP%.png}.wld"
      echo 'GEOGCS["WGS 84",DATUM["WGS_1984",SPHEROID["WGS 84",6378137,298.257223563]],PRIMEM["Greenwich",0],UNIT["degree",0.0174532925199433]]' > "${EQP%.png}.prj"
      log "bbox lon[$minlon..$maxlon] lat[$minlat..$maxlat] ${W}x${H}"
      command -v gdal_translate >/dev/null 2>&1 && gdal_translate -q -a_srs EPSG:4326 -a_ullr "$minlon" "$maxlat" "$maxlon" "$minlat" "$EQP" "${EQP%.png}.tif" >> "$LOG" 2>&1
    fi
  fi
  tail -15 "$PLOG" >> "$LOG"; rm -f "$PLOG"
else
  log "декод неуспешен (rc=$rc) или нет product.cbor — проекция пропущена"
fi

# 3) обрезка: во ВСЕХ папках приборов оставляем каналы, композиты corrected+map
#    и ОБА автоматически созданных CLI-варианта оформления:
#      *_annotated_minimal.png       — компактные плашки;
#      *_annotated_presentation.png  — полное презентационное оформление.
#    Эти файлы создаёт встроенный products_processor во время команды декодирования
#    выше. Отдельный GUI или постобработка не требуются.
kept=0; del=0
for d in "$OUT"/*/; do
  [ -d "$d" ] || continue
  di=$(basename "$d")
  for f in "$d"*.png; do
    [ -f "$f" ] || continue
    b="${f##*/}"
    case "$b" in
      "$di"-*)              kept=$((kept+1)) ;;   # каналы прибора (сырьё)
      *_corrected_map.png)  kept=$((kept+1)) ;;   # композиты corrected + map(overlay)
      *_annotated_minimal.png) kept=$((kept+1)) ;; # CLI: компактные подписи
      *_annotated_presentation.png) kept=$((kept+1)) ;; # CLI: полные подписи
      *_annotated.png)      kept=$((kept+1)) ;;   # опциональный legacy alias
      *) rm -f "$f"; del=$((del+1)) ;;
    esac
  done
done
minimal_count=$(find "$OUT" -type f -name '*_annotated_minimal.png' | wc -l)
presentation_count=$(find "$OUT" -type f -name '*_annotated_presentation.png' | wc -l)
geo_count=$(find "$OUT" -maxdepth 1 -type f \( -name '*_equirect.png' -o -name '*_stereo.png' \) | wc -l)
log "обрезка: оставлено $kept, удалено $del; CLI-плашки minimal=$minimal_count presentation=$presentation_count; проекции=$geo_count"
final_rc=$rc
if [ "$rc" = 0 ] && { [ "$minimal_count" -eq 0 ] || [ "$presentation_count" -eq 0 ]; }; then
  final_rc=4
  log "ОШИБКА: CLI-декод завершён, но оба варианта плашек не найдены; нужен SatDump Presentation и auto_process_products=true"
fi
if [ "$rc" = 0 ] && [ "$geo_count" -eq 0 ]; then
  final_rc=5
  log "ОШИБКА: CLI-декод завершён, но итоговая equirect/stereo-проекция не создана"
fi

# каталог доступен на чтение оператору
chmod -R a+rX "$OUT" 2>/dev/null
if [ "$final_rc" -ne 0 ]; then
  log "НЕПОЛНЫЙ РЕЗУЛЬТАТ $base (rc=$final_rc; обязательны декод, проекция и CLI Full/Compact)"
  exit "$final_rc"
fi
log "ГОТОВО $base"
