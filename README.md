# 🛰️ SatDump 1.2.2 Presentation

![SatDump](icon.png)

Форк SatDump 1.2.2 для полного цикла обработки спутниковых данных:

```text
IQ / сырые кадры
  → демодуляция и декодирование
  → калибровка и композиты
  → геометрическая коррекция
  → географическая проекция
  → карта и координатные слои
  → оформленный PNG с паспортом пролёта и легендой
```

Рабочая ветка: **`release/1.2.2`**. Она основана на официальном SatDump 1.2.2 и
развивается отдельно от линии SatDump 2.x.

> Проект не заменяет научный растр плашкой. Геопривязанный PNG/TIFF сохраняется
> отдельно, а оформление создаётся как дополнительный `*_annotated.png`.

## ✨ Что добавлено в этом форке

- строгая верхняя панель с данными спутника, прибора и пролёта;
- интервал наблюдения UTC;
- каналы и центральные длины волн;
- отдельное отображение радиочастоты и спектрального диапазона;
- проекция, вариант обработки и доступные показатели качества;
- непрерывные легенды физических величин;
- категориальные легенды;
- автоматическое объяснение R/G/B-композитов;
- перечисление каналов для LUT, Lua и C++-алгоритмов;
- JSON-паспорт `satdump.presentation/1`;
- адаптивная UTF-8-вёрстка с кириллицей;
- профили сборки для Astra Linux Special Edition 1.6 и 1.7;
- smoke-тесты оформления и CI.

![Макет оформленного продукта](docs/assets/presentation-product-mockup.svg)

## 📦 Результаты обработки

Для оформленного продукта создаются связанные файлы:

```text
<product>.png / <product>.tif          обычный продукт SatDump
<product>_annotated.png                публикационная копия с плашкой
<product>_annotated.json               паспорт отображения и метаданных
```

Исходный растр остаётся пригодным для ГИС, количественного анализа и повторной
обработки.

## 🚀 Быстрый старт в Astra Linux

### 1. Клонировать нужную ветку

```bash
git clone --branch release/1.2.2 https://github.com/f2re/SatDump.git
cd SatDump
chmod +x scripts/astra/*.sh
```

### 2. Сервер без GUI

```bash
bash scripts/astra/check-system.sh
bash scripts/astra/install-deps.sh \
  --profile headless \
  --bootstrap-missing

bash scripts/astra/build.sh \
  --profile headless \
  --install

bash scripts/astra/run.sh -- version
```

### 3. Рабочая станция с GUI и RTL-SDR

```bash
bash scripts/astra/install-deps.sh \
  --profile desktop \
  --bootstrap-missing

bash scripts/astra/build.sh \
  --profile desktop \
  --sdr rtl \
  --install

bash scripts/astra/run.sh --ui
```

> Не запускайте `install-deps.sh` целиком через `sudo`: сценарий сам повышает
> права только для APT. Локальный CMake и библиотеки должны принадлежать обычному
> пользователю.

## 🛡️ Astra Linux 1.6

Для 1.6 обычно требуются:

- main/update-репозитории своего обновления;
- `repository-dev` и `repository-dev-update`;
- GCC/G++ 8 или новее с C++17;
- локальный CMake 3.18.6.

Подготовка CMake:

```bash
bash scripts/astra/bootstrap-cmake.sh
```

Он устанавливается в пользовательский каталог и не заменяет системную версию.

## 🛡️ Astra Linux 1.7

GCC 8 с C++17 обычно доступен штатно. Сценарий всё равно проверяет возможность
скомпилировать программу C++17. Если системный CMake ниже 3.18, автоматически
используется тот же безопасный bootstrap.

Подробно: [установка в Astra Linux](docs/ru/INSTALL_ASTRA.md).

## 🧱 Профили сборки

| Профиль | Назначение |
|---|---|
| `headless` | CLI, Meteor/NOAA/APT, серверная и пакетная обработка |
| `desktop` | GUI, OpenGL, звук, выбранный SDR |
| `full` | расширенный набор протоколов и устройств для стенда разработки |

SDR-профили:

```text
none | rtl | common | all
```

Пример:

```bash
bash scripts/astra/build.sh \
  --profile desktop \
  --sdr common
```

## 🧰 Обычная Linux-сборка без Astra-обёртки

Минимальные зависимости Debian-подобной системы:

```bash
sudo apt install \
  git build-essential cmake g++ pkg-config \
  libfftw3-dev libpng-dev libtiff-dev \
  libjemalloc-dev libcurl4-openssl-dev \
  libnng-dev libvolk-dev
```

Сборка CLI:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_GUI=OFF \
  -DBUILD_OPENCL=OFF \
  -DBUILD_ZIQ=OFF

cmake --build build --parallel
```

На старых дистрибутивах используйте Astra-сценарии: они проверяют совместимость
CMake и компилятора и отключают непереносимый `-march=native`.

## ▶️ Офлайн-обработка

Общая форма:

```text
satdump <pipeline_id> <input_level> <input_file> <output_directory> [параметры]
```

Пример Meteor-M LRPT:

```bash
bash scripts/astra/run.sh -- \
  meteor_m2x_lrpt \
  baseband \
  /data/input/meteor_pass.cs16 \
  /data/output/meteor_pass \
  --samplerate 240000 \
  --baseband_format cs16
```

Pipeline, samplerate и формат должны соответствовать вашей записи.

## 📡 Live-обработка

Сначала проверьте устройства:

```bash
bash scripts/astra/run.sh -- sdr_probe
```

Пример:

```bash
bash scripts/astra/run.sh -- \
  live meteor_m2x_lrpt \
  /data/output/live-meteor \
  --source rtlsdr \
  --samplerate 240000 \
  --frequency 137900000 \
  --gain 35 \
  --timeout 900
```

## 🎛️ Настройка

Основной файл после установки:

```text
<префикс>/share/satdump/satdump_cfg.json
```

Пользовательский diff:

```text
~/.config/satdump/settings.json
```

TLE:

```text
~/.config/satdump/satdump_tles.txt
```

Проверка JSON:

```bash
python3 -m json.tool ~/.config/satdump/settings.json >/dev/null
```

## 🌡️ Пример температурной легенды

```json
{
  "presentation": {
    "enabled": true,
    "title": "Яркостная температура облачной поверхности",
    "legend": {
      "kind": "continuous",
      "title": "Яркостная температура",
      "unit": "K",
      "min": 180,
      "max": 320,
      "ticks": [180, 200, 220, 240, 260, 280, 300, 320],
      "colors": [
        "#1B1844",
        "#1E5E8B",
        "#24A093",
        "#96C959",
        "#FCE725"
      ]
    }
  }
}
```

## 🌈 Неоднозначные композиты

Если обычная числовая шкала неприменима, footer всё равно показывает:

```text
R — канал или формула
G — канал или формула
B — канал или формула
```

Для LUT/Lua/C++ перечисляются входные каналы и добавляется пояснение, что один
результирующий цвет может зависеть от нескольких физических величин.

## 🧪 Проверка renderer

При сборке с тестами создаются:

```text
build/astra-*/presentation-test-output/continuous.png
build/astra-*/presentation-test-output/categorical.png
build/astra-*/presentation-test-output/composite.png
```

Они проверяют непрерывную шкалу, категории, RGB-компоненты, кириллицу и
сохранение исходной ширины растра.

## 📚 Документация

- [Индекс документации](docs/ru/README.md)
- [Установка в Astra Linux](docs/ru/INSTALL_ASTRA.md)
- [Сборка](docs/ru/BUILD.md)
- [Запуск и обработка](docs/ru/RUN.md)
- [Настройка](docs/ru/CONFIGURATION.md)
- [Плашки и легенды](docs/ru/PRESENTATION.md)
- [Развёртывание](docs/ru/DEPLOYMENT.md)
- [Диагностика](docs/ru/TROUBLESHOOTING.md)

## 🔬 Научная корректность

Одноканальный ИК-продукт по умолчанию следует называть **яркостной температурой**.
Название «температура верхней границы облаков» используется только для отдельного
алгоритма физического восстановления.

Тип облачности — самостоятельный категориальный многоканальный продукт, а не
другое название температурной раскраски.

## 🔒 Защищённый контур

- не смешивайте репозитории Astra и Ubuntu/Debian;
- используйте frozen-репозиторий своего обновления;
- переносите исходные архивы через утверждённый канал;
- проверяйте контрольные суммы;
- сохраняйте SHA коммита, CMakeCache и журнал сборки;
- тестируйте на точной версии Astra, где будет работать бинарник;
- не выводите выдуманные параметры приёма в плашку.

## 🤝 Разработка

Основная линия этого форка:

```text
release/1.2.2
```

Перед изменением presentation renderer проверьте:

```bash
bash scripts/astra/build.sh --profile headless --clean
```

Для визуальных изменений приложите три smoke-test PNG и пример реального
спутникового продукта.

## 🙏 Происхождение проекта

Форк основан на открытом проекте [SatDump/SatDump](https://github.com/SatDump/SatDump).
Все исходные авторские права и уведомления upstream сохраняются.

## 📄 Лицензия

Проект распространяется по [GNU General Public License v3](LICENSE).
