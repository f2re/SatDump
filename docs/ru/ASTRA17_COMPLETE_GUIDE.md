# SatDump 1.2.2 для Astra Linux 1.7 — полное руководство

Документ относится к ветке `release/1.2.2` форка `f2re/SatDump` и к готовому
release-пакету:

```text
satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz
```

Пакет предназначен для Astra Linux 1.7 x86_64. Он собирается непосредственно в
официальной Astra Linux 1.7, содержит runtime-библиотеки приложения и после
сборки проверяется повторным развёртыванием в чистой Astra Linux 1.7 без
`apt-get install`.

## 1. Что находится в полном bundle

В архив входят:

- `bin/satdump` — CLI SatDump;
- `bin/satdump-ui` — GUI;
- `lib/libsatdump*.so` — библиотеки SatDump;
- `lib/satdump/plugins/` — плагины собранного release-профиля;
- `lib/` — полный обнаруженный ELF runtime closure;
- `lib/libc.so.6` и `lib/ld-linux-x86-64.so.2` — glibc и dynamic loader Astra 1.7;
- `libstdc++`, `libgcc_s`, OpenMP runtime;
- GUI/OpenGL/GLFW и их транзитивные библиотеки;
- PortAudio/ALSA runtime;
- RTL-SDR, libusb/udev runtime;
- curl/TLS, TIFF, PNG, FFTW, VOLK, jemalloc и необходимые транзитивные `.so`;
- `pipelines/`, `resources/`, шрифты и конфигурация SatDump;
- русская документация;
- `ASTRA17-MANIFEST.txt`;
- `RUNTIME-LIBRARIES.txt`;
- `SHA256SUMS`;
- `verify.sh`;
- `install.sh`.

Kernel, устройства `/dev`, аппаратные firmware и видеодрайвер конкретного
оборудования предоставляет сама Astra Linux. Для запуска SatDump из bundle
устанавливать дополнительные runtime-пакеты APT не требуется.

## 2. Как выпускаются релизы

Каждый успешный push в `release/1.2.2`, затрагивающий Astra/build/docs-контур,
запускает GitHub Actions:

```text
checkout
  → официальный Astra Linux UBI 1.7
  → установка build-зависимостей только в build-контейнер
  → компиляция SatDump внутри Astra 1.7
  → presentation smoke tests
  → DESTDIR staging
  → recursive ELF closure
  → упаковка glibc/loader/NSS и остальных runtime-библиотек
  → SHA-256/manifest
  → скачивание готового artifact
  → чистый Astra 1.7 runtime-контейнер без apt install
  → CLI + dynamic-loader validation
  → GitHub Release
```

Версия release имеет вид:

```text
v1.2.2-astra17-r<номер GitHub Actions run>
```

В GitHub Release прикладываются как минимум:

```text
satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz
satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz.sha256
ASTRA17-MANIFEST.txt
RUNTIME-LIBRARIES.txt
ASTRA17_COMPLETE_GUIDE.md
```

## 3. Проверка скачанного архива

В одном каталоге должны лежать `.tar.gz` и `.sha256`:

```bash
sha256sum -c satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz.sha256
```

Ожидается:

```text
satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz: OK
```

Распаковка:

```bash
tar -xzf satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz
cd satdump-1.2.2-astra17-desktop-full-x86_64
```

Внутренняя проверка содержимого:

```bash
sha256sum -c SHA256SUMS
```

## 4. Запуск без установки

Проверка CLI:

```bash
./satdump version
```

Запуск GUI:

```bash
./satdump-ui
```

Launcher не зависит от системного `LD_LIBRARY_PATH`: он запускает бинарник через
вложенный loader:

```text
lib/ld-linux-x86-64.so.2
```

с `--library-path`, направленным на `lib/` самого bundle.

## 5. Постоянная установка

От обычного пользователя:

```bash
./install.sh
```

По умолчанию файлы копируются в:

```text
~/.local/opt/satdump-1.2.2
```

а launcher-ссылки создаются в:

```text
~/.local/bin
```

Если `~/.local/bin` отсутствует в `PATH`:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

Системная установка:

```bash
sudo ./install.sh
```

По умолчанию:

```text
/opt/satdump-1.2.2
/usr/local/bin/satdump
/usr/local/bin/satdump-ui
```

Можно изменить пути:

```bash
SATDUMP_PREFIX=/srv/satdump \
SATDUMP_BINDIR=/usr/local/bin \
sudo ./install.sh
```

`ldconfig` и установка дополнительных `.deb` для самого SatDump не нужны.

## 6. Самодиагностика

В полном bundle есть:

```bash
./verify.sh
```

Проверяются:

- наличие bundled glibc/loader;
- C++ runtime;
- запуск `satdump version`;
- разрешение ELF-зависимостей;
- отсутствие `undefined symbol`.

Для диагностики состава release:

```bash
cat ASTRA17-MANIFEST.txt
less RUNTIME-LIBRARIES.txt
```

Манифест фиксирует build OS, glibc, ABI, source commit, количество библиотек и
плагинов.

## 7. GUI и права пользователя

GUI запускайте от обычного пользователя:

```bash
satdump-ui
```

Bundle содержит пользовательские библиотеки GUI, но доступ к реальному дисплею,
DRM/X11/Wayland и аппаратному GPU обеспечивает установленная Astra Linux.

Для RTL-SDR также требуется доступ пользователя к USB-устройству. Сам runtime
`librtlsdr` и `libusb` находится в bundle; udev-policy и права на `/dev/bus/usb`
относятся к конфигурации ОС.

Проверка видимых SDR-источников:

```bash
satdump sdr_probe
```

## 8. Общая форма CLI

Офлайн-обработка:

```text
satdump <pipeline_id> <input_level> <input_file> <output_directory> [параметры]
```

Пример Meteor-M LRPT:

```bash
satdump meteor_m2x_lrpt \
  baseband \
  /data/input/meteor_pass.cs16 \
  /data/output/meteor_pass \
  --samplerate 240000 \
  --baseband_format cs16
```

Пример MetOp AHRPT:

```bash
satdump metop_ahrpt \
  baseband \
  /data/input/metop.cs16 \
  /data/output/metop \
  --samplerate 6000000 \
  --baseband_format cs16
```

Часто используемые входные уровни:

| Уровень | Смысл |
|---|---|
| `baseband` | IQ-запись |
| `soft` / `soft_symbols` | мягкие символы |
| `frames` | синхронизированные кадры |
| `cadu` | CCSDS CADU |
| `products` | уже декодированные продукты |

## 9. Live-приём и запись IQ

Live-обработка:

```bash
satdump live meteor_m2x_lrpt \
  /data/output/live-meteor \
  --source rtlsdr \
  --samplerate 240000 \
  --frequency 137900000 \
  --gain 35 \
  --timeout 900
```

Запись IQ:

```bash
satdump record /data/recordings/meteor_20260721 \
  --source rtlsdr \
  --samplerate 240000 \
  --frequency 137900000 \
  --baseband_format cs16 \
  --timeout 900
```

Перед live/record:

```bash
satdump sdr_probe
```

## 10. Основные каталоги данных

Рекомендуемая схема:

```text
/data/satdump/incoming
/data/satdump/working
/data/satdump/products
/data/satdump/archive
```

Процессу SatDump нужны права записи в `working` и `products`.

## 11. Конфигурация

Системные ресурсы bundle находятся внутри установленного дерева:

```text
share/satdump/
```

Пользовательские настройки SatDump обычно находятся в пользовательском каталоге
конфигурации. Перед пакетной эксплуатацией рекомендуется явно проверить:

- источник TLE и период обновления;
- каталог временных файлов;
- уровень логирования;
- параметры SDR;
- настройки картографических слоёв;
- параметры presentation renderer.

Для полностью закрытого контура отключите сетевое обновление TLE и используйте
локальный файл/заранее подготовленный кэш.

## 12. Presentation renderer: принцип

Оформление выполняется после получения научного продукта:

```text
декодирование
→ калибровка/композит
→ геометрическая коррекция
→ проекция/карта
→ исходный научный продукт
→ оформленные PNG + JSON-паспорта
```

Исходный научный растр не заменяется. Плашки не записываются внутрь GeoTIFF,
потому что служебные пиксели не должны становиться частью геопривязанных данных.

## 13. Какие снимки формируются

По умолчанию доступны два независимых оформления:

```text
<имя>_annotated_minimal.png
<имя>_annotated_minimal.json
<имя>_annotated_presentation.png
<имя>_annotated_presentation.json
```

При необходимости совместимости:

```text
<имя>_annotated.png
<имя>_annotated.json
```

`minimal` — простой оперативный режим.

`editorial` / presentation — расширенный дизайн для публикации и аналитических
материалов.

## 14. Простой режим `minimal`

Минимальный вариант добавляет компактные панели над/под снимком:

- спутник;
- прибор;
- название продукта;
- интервал наблюдения;
- направление пролёта;
- канал/спектральный диапазон;
- частоту приёма, если она известна;
- проекцию;
- компактную легенду;
- предупреждение об условных цветах для RGB.

Он предназначен для больших потоков снимков, каталогов, оперативных сводок и
быстрого просмотра. Исходное изображение не ресемплируется.

## 15. Расширенный режим `editorial`

Presentation-вариант использует выраженную информационную иерархию:

1. спутник и прибор;
2. название тематического продукта;
3. время и параметры пролёта;
4. качество;
5. технические сведения;
6. полноценная легенда;
7. branding.

При наличии данных показываются:

- NORAD ID;
- направление пролёта;
- максимальная высота;
- радиочастота;
- частота дискретизации;
- проекция;
- вариант обработки;
- SNR;
- потери строк/пакетов;
- интегральная оценка качества.

Неизвестные поля не выдумываются и не заполняются по одному только названию
спутника.

## 16. Легенды

Поддерживаются четыре типа.

### Непрерывная

Для физических величин:

- яркостная температура;
- отражательная способность;
- альбедо;
- радианс;
- высота;
- водность;
- интенсивность осадков.

Показывает цветовую шкалу, ticks, единицу и пояснения.

### Категориальная

Для классов:

- тип/фаза облачности;
- снег, лёд, вода;
- классы осадков;
- quality mask;
- `не определено`.

### RGB/composite

Показывает состав:

```text
R — канал/формула
G — канал/формула
B — канал/формула
```

а также длины волн, тип калибровки, формулы, LUT/Lua/C++-алгоритм, нормализацию,
инверсию и другие доступные сведения.

### Без физической шкалы

Если физическую легенду достоверно построить нельзя, footer всё равно содержит
каналы и пояснение условности цветов вместо выдуманной шкалы.

## 17. Автоматический разбор RGB

Для трёхкомпонентного выражения renderer:

1. разделяет R/G/B по запятым верхнего уровня;
2. сопоставляет токены с каналами `ImageProducts`;
3. различает `ch...` и `cch...`;
4. извлекает длину волны из wavenumber;
5. добавляет тип калибровки;
6. сохраняет исходную формулу.

Например:

```text
cch8-cch9
```

распознаётся как разность двух каналов.

## 18. LUT, Lua и C++ композиты

Для нелинейных композитов система не приписывает одному цвету одну физическую
величину без основания. В подписи показываются обнаруженные входы, алгоритм и
экспертные notes пресета.

## 19. Север сверху и ориентация

Автоматическая ориентация presentation-копии определяется по приоритету:

1. географическая проекция фактического выходного растра;
2. GCP;
3. TLE и время строк;
4. направление пролёта;
5. сохранение исходной ориентации, если данных недостаточно.

Исходный научный растр не переворачивается.

Ручные режимы:

| `orientation.mode` | Действие |
|---|---|
| `auto` | автоматический анализ |
| `keep` | оставить как есть |
| `flip_vertical` | вертикальное отражение |
| `flip_horizontal` | горизонтальное отражение |
| `rotate_180` | поворот на 180° без интерполяции |

## 20. Вертикальные и горизонтальные кадры

Оформление адаптируется к отношению сторон.

Для вертикального кадра уменьшается масштаб заголовков, блоки переносятся,
категории строятся компактнее.

Для горизонтального кадра используются более широкие строки метаданных и при
необходимости двухколоночные категории.

Кадр не поворачивается только ради плашки: географический смысл изображения
сохраняется.

## 21. Настройка выходов

```json
{
  "presentation": {
    "enabled": true,
    "outputs": {
      "minimal": true,
      "presentation": true,
      "legacy_alias": false
    },
    "orientation": {
      "mode": "auto",
      "north_up": true
    }
  }
}
```

Краткая запись:

```json
{
  "presentation": {
    "outputs": ["minimal", "editorial"]
  }
}
```

## 22. Темы, branding и дизайн

Общая тема задаётся в `presentation.theme`; `minimal` и `editorial` могут иметь
собственные темы.

```json
{
  "presentation": {
    "minimal": {
      "branding": "Оперативный метеоцентр",
      "show_branding": true,
      "theme": {
        "panel": "#101820",
        "text": "#F4F7FA",
        "muted_text": "#A8B4C2",
        "accent": "#55C7E8"
      }
    },
    "editorial": {
      "branding": "Спутниковая метеорология",
      "theme": {
        "panel": "#0E1624",
        "panel_secondary": "#172235",
        "border": "#2A3A50",
        "accent": "#4EC7E8",
        "reference_width": 1600,
        "minimum_scale": 0.58,
        "maximum_scale": 2.0
      }
    }
  }
}
```

Поддерживаются цвета `#RRGGBB`, `#RRGGBBAA` и массивы RGB.

## 23. Пример физической шкалы

```json
{
  "presentation": {
    "legend": {
      "kind": "continuous",
      "title": "Яркостная температура",
      "subtitle": "Канал 10,8 мкм",
      "unit": "K",
      "min": 180,
      "max": 320,
      "ticks": [180, 200, 220, 240, 260, 280, 300, 320],
      "colors": ["#1B1844", "#1E5E8B", "#24A093", "#96C959", "#FCE725"]
    }
  }
}
```

Шкала должна использовать ту же палитру, что и окраска продукта.

## 24. JSON-паспорт

Для каждого оформленного изображения создаётся JSON-паспорт
`satdump.presentation/2`.

Он хранит:

- фактические метаданные продукта;
- layout/theme;
- вид легенды;
- формулы и каналы;
- настройки ориентации;
- факт и источник преобразования north-up;
- сведения, необходимые для аудита и повторного оформления.

Это позволяет индексировать архив, строить web-карточки и повторно оформлять
продукты без повторного декодирования исходного сигнала.

## 25. Научная терминология

Одноканальный откалиброванный ИК-продукт следует подписывать как
`Яркостная температура, K`, если именно она рассчитана.

`Температура верхней границы облаков` используется только при наличии отдельного
retrieval-алгоритма. Простая ИК-калибровка в кельвины не является автоматически
cloud-top temperature.

`Тип облачности` — категориальный многоканальный продукт, а не название обычной
температурной LUT.

## 26. Level-1C / SATPROF

Форк также содержит контур Level-1C/SATPROF. Его отдельные параметры и формат
описаны в:

```text
share/doc/satdump/LEVEL1C_SATPROF.md
```

и в репозитории `docs/ru/LEVEL1C_SATPROF.md`.

## 27. Проверка presentation renderer при сборке

Release-сборка запускает presentation smoke test. Проверяются как минимум:

- landscape и portrait;
- `minimal` и `editorial`;
- непрерывная легенда;
- категориальная легенда;
- RGB/composite;
- вертикальное/горизонтальное отражение;
- поворот 180°;
- сценарии north-up.

Эталонные PNG публикуются отдельным GitHub Actions artifact для QA.

## 28. Обновление

Новый release распаковывайте в новый каталог. Для системной установки выполните
`install.sh` из нового bundle. Старый каталог рекомендуется сохранять до
функциональной проверки новой версии.

Проверка после обновления:

```bash
satdump version
satdump sdr_probe
satdump-ui
```

Для rollback достаточно вернуть предыдущий каталог/символические ссылки.

## 29. Что смотреть при проблеме

Сначала:

```bash
./satdump version
./verify.sh
cat ASTRA17-MANIFEST.txt
less RUNTIME-LIBRARIES.txt
```

Если проблема связана с USB/SDR, проверьте права и наличие устройства в ОС.
Если GUI не открывает окно — проверяйте DISPLAY/Wayland/X11 и графический driver.
Если pipeline не найден — проверяйте `share/satdump/pipelines` и выбранный
`pipeline_id`.

Расширенная диагностика находится в `share/doc/satdump/TROUBLESHOOTING.md`.
