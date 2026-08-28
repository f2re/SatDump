# 🛰️ SatDump 1.2.2 Presentation

![SatDump](icon.png)

Форк **SatDump 1.2.2** для обработки спутниковых данных и выпуска готовых
метеорологических изображений с двумя вариантами оформления, физическими/RGB-
легендами, north-up ориентацией и JSON-паспортом.

Рабочая линия: **`release/1.2.2`**.

```text
IQ / кадры
→ демодуляция и декодирование
→ калибровка / композиты
→ геометрическая коррекция
→ проекция и карта
→ исходный научный продукт
→ minimal + editorial PNG
→ JSON-паспорта
```

Исходный геопривязанный продукт не заменяется оформленной копией и остаётся
пригодным для ГИС и количественного анализа.

## Astra Linux 1.7: готовый release без установки зависимостей

Основной способ развёртывания — скачать последний GitHub Release, созданный из
ветки `release/1.2.2`.

Asset:

```text
satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz
satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz.sha256
```

Проверка и запуск:

```bash
sha256sum -c satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz.sha256

tar -xzf satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz
cd satdump-1.2.2-astra17-desktop-full-x86_64

./satdump version
./satdump-ui
```

Постоянная установка:

```bash
./install.sh
```

На целевой Astra Linux 1.7 для SatDump не выполняется `apt-get install`.
Release содержит полный обнаруженный ELF runtime closure, включая:

- Astra glibc и dynamic loader;
- `libstdc++`, `libgcc_s`, OpenMP;
- GUI/OpenGL/GLFW runtime;
- PortAudio/ALSA runtime;
- RTL-SDR/libusb/udev runtime;
- curl/TLS, TIFF, PNG, FFTW, VOLK, jemalloc;
- транзитивные `.so`;
- NSS-модули glibc;
- pipelines/resources/fonts/config;
- документацию.

Аппаратные kernel modules/firmware, USB permissions и конкретный graphics driver
остаются частью самой Astra Linux и оборудования.

Полное руководство: **[docs/ru/ASTRA17_COMPLETE_GUIDE.md](docs/ru/ASTRA17_COMPLETE_GUIDE.md)**.

## Как формируется Astra 1.7 Release

Release не компилируется в Debian/Buster compatibility chroot.

GitHub Actions запускает сборку непосредственно внутри официального:

```text
registry.astralinux.ru/library/astra/ubi17:1.7.5
```

Контур:

```text
официальная Astra Linux 1.7
→ build toolchain
→ CMake build
→ presentation smoke tests
→ DESTDIR staging
→ полный recursive DT_NEEDED closure
→ glibc/loader/NSS в bundle
→ SHA-256 + manifest
→ новый чистый Astra Linux 1.7
→ запуск скачанного artifact БЕЗ apt-get install
→ GitHub Release
```

Сборочный скрипт запрещает `ASTRA_VERSION_OVERRIDE` и требует фактическую Astra
Linux 1.7 с glibc 2.28.

Versioned release tag:

```text
v1.2.2-astra17-r<GitHub Actions run number>
```

## Локальная release-сборка

Только на фактической Astra Linux 1.7 x86_64:

```bash
git clone --branch release/1.2.2 https://github.com/f2re/SatDump.git
cd SatDump

bash scripts/astra/build.sh \
  --mode portable-astra17 \
  --profile desktop \
  --prepare-build-env
```

`--prepare-build-env` ставит зависимости только в **сборочную** Astra. Они не
нужны системе, где запускается готовый release archive.

Результат:

```text
dist/astra17/satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz
```

## Что добавлено в presentation renderer

- два независимых оформления: **Minimal** и **Editorial/Presentation**;
- адаптивный layout для portrait/landscape;
- автоматическая ориентация «север сверху»;
- анализ проекции, GCP, TLE, времени строк и направления пролёта;
- ручные `keep`, `flip_vertical`, `flip_horizontal`, `rotate_180`;
- верхняя информационная панель;
- расширенный паспорт пролёта;
- показатели качества, если они реально доступны;
- continuous legends физических величин;
- categorical legends;
- автоматическое объяснение R/G/B;
- анализ разностей каналов вроде `cch8-cch9`;
- описание LUT, Lua и C++-композитов;
- themes/branding;
- UTF-8/кириллица;
- JSON-паспорт `satdump.presentation/2`;
- smoke tests для layout/legend/orientation.

## Два вида оформления

### Minimal

Компактные поля сверху и снизу. Для оперативных каталогов, большого потока
снимков и автоматических сводок.

### Editorial / Presentation

Расширенная информационная иерархия: крупный заголовок, паспорт пролёта,
качество, технические сведения, полноценная легенда и branding.

Создаются:

```text
<product>.png / <product>.tif
<product>_annotated_minimal.png
<product>_annotated_minimal.json
<product>_annotated_presentation.png
<product>_annotated_presentation.json
```

Опциональный legacy alias:

```text
<product>_annotated.png
<product>_annotated.json
```

## Легенды

Поддерживаются:

1. непрерывная шкала — яркостная температура, отражательная способность,
   альбедо, радианс и другие количественные величины;
2. категориальная — облачность, фаза, снег/лёд/вода, маски качества;
3. RGB/composite — R/G/B, каналы, длины волн, формулы и калибровка;
4. информационный footer без числовой шкалы, если физическая шкала недостоверна.

Цветовая шкала физического продукта должна использовать ту же палитру, которой
окрашен растр.

## Ориентация

Автоматический приоритет:

```text
projection
→ GCP
→ TLE + line timestamps
→ pass direction
→ keep
```

Преобразование применяется только к presentation-копии. Исходный научный растр
не переворачивается.

## Настройка двух выходов

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

У `minimal` и `editorial` могут быть собственные themes/branding.

Подробные примеры находятся в:

- [Presentation renderer](docs/ru/PRESENTATION.md)
- [Minimal/editorial и ориентация](docs/ru/PRESENTATION_LAYOUTS.md)
- [Полное руководство Astra 1.7](docs/ru/ASTRA17_COMPLETE_GUIDE.md)

## CLI

Офлайн-обработка:

```text
satdump <pipeline_id> <input_level> <input_file> <output_directory> [options]
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

## Live и запись IQ

Проверка SDR:

```bash
satdump sdr_probe
```

Live RTL-SDR:

```bash
satdump live meteor_m2x_lrpt \
  /data/output/live-meteor \
  --source rtlsdr \
  --samplerate 240000 \
  --frequency 137900000 \
  --gain 35
```

Запись:

```bash
satdump record /data/recordings/pass \
  --source rtlsdr \
  --samplerate 240000 \
  --frequency 137900000 \
  --baseband_format cs16
```

## Научная корректность подписей

- Калиброванный одноканальный ИК-продукт по умолчанию описывается как
  **яркостная температура**.
- `Температура верхней границы облаков` допустима только для отдельного retrieval.
- `Тип облачности` — категориальный многоканальный алгоритм.
- Радиочастота приёма и спектральный диапазон прибора показываются отдельно.
- Неизвестные metadata не подставляются по одному названию спутника.

## Документация

- [Полное руководство Astra Linux 1.7](docs/ru/ASTRA17_COMPLETE_GUIDE.md)
- [Техническое устройство full bundle](docs/ru/ASTRA17_BUNDLE.md)
- [Индекс документации](docs/ru/README.md)
- [Запуск](docs/ru/RUN.md)
- [Настройка](docs/ru/CONFIGURATION.md)
- [Presentation renderer](docs/ru/PRESENTATION.md)
- [Layout и orientation](docs/ru/PRESENTATION_LAYOUTS.md)
- [Level-1C / SATPROF](docs/ru/LEVEL1C_SATPROF.md)
- [Диагностика](docs/ru/TROUBLESHOOTING.md)

## Разработка

Основная release-линия:

```text
release/1.2.2
```

Изменение Astra release-контура должно пройти:

```text
shell/docs gate
→ native Astra 1.7 build
→ full dependency closure
→ fresh Astra 1.7 zero-install runtime gate
→ merge
→ post-merge build
→ GitHub Release
```

## Лицензия

GNU GPL v3. См. [LICENSE](LICENSE).
