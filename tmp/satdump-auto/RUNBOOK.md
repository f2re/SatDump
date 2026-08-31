# SatDump: запуск, автопроцессинг и оформленные изображения

## 1. Сначала проверить совместимость бандла

Ошибка из `logs.txt` вида `GLIBC_2.25/2.27/2.28 not found` означает, что программа
собрана для **более новой** glibc, чем на машине. Это происходит до запуска SatDump,
поэтому systemd, `LD_LIBRARY_PATH` и правка скриптов проблему не исправляют.

На целевой машине выполните:

```bash
ldd --version | head -n1
```

| glibc на целевой машине | Какой архив ставить |
|---|---|
| 2.28 (Astra Linux 1.7) | `satdump-1.2.2-astra17-desktop-glibc228-x86_64.tar.gz` |
| 2.24 (типичная Astra Linux 1.6) | `satdump-1.2.2-presentation-meteor-glibc224-x86_64.tar.gz` |
| ниже 2.24 | нужна пересборка с соответствующим baseline |

Не устанавливайте архив glibc 2.28 на Astra 1.6: именно это зафиксировано в
`logs.txt`. После копирования проверяйте checksum:

```bash
sha256sum -c satdump-*.tar.gz.sha256
```

## 2. Установка и самостоятельная проверка

Готовый комплект `satdump-1.2.2-astra16-cli-auto-x86_64.tar.gz` уже содержит
каталоги `satdump/` и `automation/`. Распакуйте его и установите CLI-runtime:

```bash
tar -xzf satdump-1.2.2-astra16-cli-auto-x86_64.tar.gz
cd satdump-1.2.2-astra16-cli-auto-x86_64
sudo mkdir -p /opt/satdump
sudo cp -a satdump/. /opt/satdump/
sudo chmod -R a+rX /opt/satdump
/opt/satdump/satdump version
```

Если используется отдельный переносимый архив, эквивалентная установка выглядит
так (системные библиотеки не изменяются):

```bash
sudo mkdir -p /opt/satdump
sudo tar -xzf satdump-1.2.2-presentation-meteor-glibc224-x86_64.tar.gz \
  -C /opt/satdump --strip-components=1
sudo chmod -R a+rX /opt/satdump
/opt/satdump/satdump version
```

Всегда запускайте CLI-wrapper `/opt/satdump/satdump`, а не
`/opt/satdump/bin/satdump`: wrapper задаёт пути к ресурсам, плагинам и библиотекам.

Если последняя команда вывела версию, runtime исправен. Если она сообщает
`GLIBC_x.y not found`, удалить этот каталог `/opt/satdump` и поставить архив с нужным
baseline; не смешивать файлы от разных архивов.

## 3. Автообработка новых пролётов

Скрипты ожидают:

* исходные CADU: `/opt/Receiver/Raw/*.dat`;
* TLE: `/opt/Receiver/Tlg/weather.txt`;
* результат: `/opt/geo-products/YYYY-MM-DD/<спутник>_<дата>_<время>/`.

Установка из распакованного комплекта:

```bash
sudo install -d -m 0755 /opt/satdump-auto /opt/geo-products
sudo install -m 0755 automation/{watch.sh,reprocess.sh,batch_reprocess.sh} /opt/satdump-auto/
sudo install -m 0644 automation/satdump-auto.service /etc/systemd/system/
command -v inotifywait >/dev/null || sudo apt-get install inotify-tools
sudo systemctl daemon-reload
sudo systemctl enable --now satdump-auto.service
```

Проверка без ожидания нового пролёта:

```bash
systemctl is-active satdump-auto.service
F="$(find /opt/Receiver/Raw -maxdepth 1 -type f -name 'METEOR-M2*_DMW_*.dat' -printf '%T@ %p\n' | sort -nr | head -1 | cut -d' ' -f2-)"
sudo /opt/satdump-auto/reprocess.sh "$F"
tail -n 80 /opt/satdump-auto/reprocess.log
```

Признак успеха — строка `ГОТОВО` и PNG в каталоге результата. Для диагностики:

```bash
journalctl -u satdump-auto.service -n 100 --no-pager
tail -n 100 /opt/satdump-auto/watch.log
tail -n 100 /opt/satdump-auto/reprocess.log
```

Watcher обрабатывает файлы последовательно после события `close_write`; уже лежащие
в `Raw/` файлы он не подхватывает сам, их нужно запустить через `reprocess.sh` или
`batch_reprocess.sh`.

## 4. TLE

Текущий `tle_update.log` сообщает DNS/HTTP 401 для `373.gov`. Это не останавливает
обработку: скрипт намеренно сохраняет прежний `weather.txt`. Но старые TLE ухудшают
привязку. Нужно восстановить доступ к утверждённому внутреннему источнику либо
положить актуальный валидный `weather.txt` в `/opt/Receiver/Tlg/weather.txt`.

Перед пролётом проверьте:

```bash
grep -cE '^1 [0-9]{5}' /opt/Receiver/Tlg/weather.txt
```

Ноль означает, что геопривязка и карты не будут корректными.

## 5. Полностью автоматические изображения и подписи через CLI

GUI не используется. Цепочка целиком выполняется сервисом:

```text
watch.sh → satdump <pipeline> cadu → products_processor
         → project → сохранение → проверка Full/Compact
```

Одна команда декодирования внутри `reprocess.sh` автоматически создаёт:

* каналы инструмента;
* `*_corrected_map.png` — геометрически исправленный продукт с картой, городами,
  сеткой координат и точкой приёма;
* `*_equirect.png` или `*_stereo.png` — общий геопривязанный вид пролёта.
* `*_annotated_minimal.png` и `.json` — компактные подписи для каталога/рассылки;
* `*_annotated_presentation.png` и `.json` — полные панели, паспорт пролёта и легенда.

`products_processor` запускается автоматически после CLI-декодера, когда
`auto_process_products=true` (это штатное значение). Исправленный `reprocess.sh`
сохраняет оба оформленных варианта; прежняя версия ошибочно удаляла эти PNG на этапе
очистки. В конце лога теперь должна появляться строка:

```text
CLI-плашки minimal=N presentation=N
```

Оба `N` должны быть больше нуля для пролёта с доступными композитами. Если выводится
ноль, проверьте, что установлен именно SatDump 1.2.2 Presentation, присутствует шрифт
`share/satdump/resources/fonts/Roboto-Medium.ttf`, а в настройках не отключены
`auto_process_products` или `presentation`.

Полные и компактные картинки создаются для каждого подходящего автокомпозита. Растр
не обрезается: Full добавляет развёрнутые панели, Compact — узкие. Имя станции задаётся
полем `default_qth_label` в `/root/.config/satdump/settings.json`.

Для ночного пролёта замените в `reprocess.sh` выражение `EQN` на ИК-канал/ИК-композит:
видимые `ch1–ch3` ночью почти чёрные. После изменения перезапустите сервис:

```bash
sudo systemctl restart satdump-auto.service
```

Ручная проверка той же CLI-цепочки без watcher:

```bash
sudo /opt/satdump-auto/reprocess.sh "/opt/Receiver/Raw/<файл>.dat"
find /opt/geo-products -type f \
  \( -name '*_annotated_minimal.png' -o -name '*_annotated_presentation.png' \) \
  -printf '%p\n'
```
