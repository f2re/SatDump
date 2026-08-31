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
| 2.24 (типичная Astra Linux 1.6) | только отдельный бандл, собранный с baseline glibc 2.24 |
| ниже 2.24 | нужна пересборка с соответствующим baseline |

Не устанавливайте архив glibc 2.28 на Astra 1.6: именно это зафиксировано в
`logs.txt`. После копирования проверяйте checksum:

```bash
sha256sum -c satdump-*.tar.gz.sha256
```

## 2. Установка и самостоятельная проверка

Ниже пример для Astra 1.7 и переносимого архива. Он не меняет системные библиотеки.

```bash
sudo mkdir -p /opt/satdump
sudo tar -xzf satdump-1.2.2-astra17-desktop-glibc228-x86_64.tar.gz \
  -C /opt/satdump --strip-components=1
sudo chmod -R a+rX /opt/satdump
/opt/satdump/satdump version
```

Всегда запускайте wrappers `/opt/satdump/satdump` и `/opt/satdump/satdump-ui`, а не
`/opt/satdump/bin/*`: wrappers задают пути к ресурсам, плагинам и библиотекам.

Если последняя команда вывела версию, runtime исправен. Если она сообщает
`GLIBC_x.y not found`, удалить этот каталог `/opt/satdump` и поставить архив с нужным
baseline; не смешивать файлы от разных архивов.

## 3. Автообработка новых пролётов

Скрипты ожидают:

* исходные CADU: `/opt/Receiver/Raw/*.dat`;
* TLE: `/opt/Receiver/Tlg/weather.txt`;
* результат: `/opt/geo-products/YYYY-MM-DD/<спутник>_<дата>_<время>/`.

Установка:

```bash
sudo install -d -m 0755 /opt/satdump-auto /opt/geo-products
sudo install -m 0755 tmp/satdump-auto/{watch.sh,reprocess.sh} /opt/satdump-auto/
sudo install -m 0644 tmp/satdump-auto/satdump-auto.service /etc/systemd/system/
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

## 5. Красивые изображения и подписи

`reprocess.sh` автоматически создаёт:

* каналы инструмента;
* `*_corrected_map.png` — геометрически исправленный продукт с картой, городами,
  сеткой координат и точкой приёма;
* `*_equirect.png` или `*_stereo.png` — общий геопривязанный вид пролёта.

Для полного оформленного кадра откройте `satdump-ui`, загрузите продукт/композит,
включите проекцию и карту, затем выберите оформление **Presentation / Full**. Оно
сохраняет весь растр и добавляет развёрнутые верхнюю и нижнюю панели с данными пролёта.

Для компактного каталога или рассылки выберите **Minimal / Compact**. Растр остаётся
тем же, сокращаются только подписи. Не обрезайте плашки редактором: так теряются
легенда и контекст продукта. Имя станции задаётся в настройках SatDump полем
`default_qth_label`.

Для ночного пролёта замените в `reprocess.sh` выражение `EQN` на ИК-канал/ИК-композит:
видимые `ch1–ch3` ночью почти чёрные. После изменения перезапустите сервис:

```bash
sudo systemctl restart satdump-auto.service
```
