#!/bin/bash
# Переобработка ВСЕХ существующих .dat под новый формат. Последовательно.
LOG=/opt/satdump-auto/batch_reprocess.log
RAW=/opt/Receiver/Raw
echo "[$(date '+%F %T')] BATCH START" >> "$LOG"
n=0; ok=0
while IFS= read -r -d '' dat; do
  base=$(basename "$dat" .dat)
  n=$((n+1))
  echo "[$(date '+%F %T')] ($n) $base  <-  $(basename "$dat")" >> "$LOG"
  if nice -n 19 ionice -c3 /opt/satdump-auto/reprocess.sh "$dat" >> "$LOG" 2>&1; then ok=$((ok+1)); fi
done < <(find "$RAW" -maxdepth 1 -type f -name '*.dat' -print0 | sort -z)
echo "[$(date '+%F %T')] BATCH DONE: обработано $ok из $n" >> "$LOG"
