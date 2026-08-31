#!/bin/bash
# watch.sh — следит за появлением новых .dat в /opt/Receiver/Raw и запускает обработку.
# Обработка последовательная (одна за раз), чтобы не грузить CPU во время приёма.
RAW=/opt/Receiver/Raw
LOG=/opt/satdump-auto/watch.log
REPROC=/opt/satdump-auto/reprocess.sh

echo "[$(date '+%Y-%m-%d %H:%M:%S')] watcher запущен, слежу за $RAW" >> "$LOG"

inotifywait -m -e close_write -e moved_to --format '%f' "$RAW" 2>>"$LOG" | while read -r f; do
  case "$f" in
    *.dat)
      echo "[$(date '+%Y-%m-%d %H:%M:%S')] новый файл: $f" >> "$LOG"
      sleep 5                      # дать ФС дописать
      # Синхронно = сериализация, без гонки за CPU. Ошибка одного пролёта не
      # останавливает watcher, но обязательно фиксируется в общем журнале.
      "$REPROC" "$RAW/$f"
      rc=$?
      [ "$rc" -eq 0 ] || echo "[$(date '+%Y-%m-%d %H:%M:%S')] ОШИБКА обработки rc=$rc: $f" >> "$LOG"
      ;;
  esac
done
