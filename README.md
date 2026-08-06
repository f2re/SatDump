# 🛰️ SatDump 1.2.2 Presentation

![SatDump](icon.png)

Русскоязычный форк **SatDump 1.2.2** для полного цикла обработки спутниковых данных и выпуска готовых метеорологических изображений:

```text
IQ / сырые кадры
  → демодуляция и декодирование
  → калибровка и композиты
  → геометрическая коррекция
  → географическая проекция и карта
  → научный продукт без плашек
  → две оформленные PNG-копии + JSON-паспорта
```

Рабочая ветка: **`release/1.2.2`**. Она основана на официальном SatDump 1.2.2 и развивается отдельно от SatDump 2.x.

> Геопривязанный PNG/TIFF не расширяется информационными полями. Оформление создаётся только как дополнительная копия, поэтому исходный научный растр остаётся пригодным для ГИС и количественного анализа.

## ✨ Что добавлено

- два настраиваемых вида оформления: **Minimal** и **Presentation**;
- адаптивная компоновка для вертикальных и горизонтальных кадров;
- автоматическая проверка ориентации «север сверху»;
- учёт восходящего и нисходящего направления пролёта;
- строгая верхняя панель со спутником, прибором, временем и параметрами приёма;
- непрерывные легенды физических величин;
- категориальные легенды типов облачности и других классов;
- автоматическое объяснение компонентов `R`, `G`, `B`;
- перечисление входов LUT, Lua и C++-композитов;
- JSON-паспорт `satdump.presentation/2`;
- UTF-8 и кириллица;
- профили сборки для Astra Linux Special Edition 1.6 и 1.7;
- smoke-тесты портретной/альбомной компоновки и ориентации.

## 🖼️ Два вида плашек

### Minimal

Компактные поля сверху и снизу снимка. Подходит для оперативных каталогов, автоматической рассылки и большого потока изображений.

### Presentation

Расширенная визуальная иерархия: крупный заголовок, паспорт пролёта, показатели качества, подробная легенда и аккуратная типографика. Подходит для сводок, отчётов и публикаций.

По умолчанию создаются оба варианта:

```text
<product>.png / <product>.tif                    исходный продукт SatDump
<product>_annotated_minimal.png                  компактное оформление
<product>_annotated_minimal.json                 его паспорт
<product>_annotated_presentation.png             презентационное оформление
<product>_annotated_presentation.json            его паспорт
```

Совместимый файл `<product>_annotated.png` можно включить отдельно.

Подробно: [два макета и ориентация](docs/ru/PRESENTATION_LAYOUTS.md).

## 🧭 Север сверху

Для презентационной копии ориентация определяется в следующем порядке:

1. географическая проекция фактического выходного растра;
2. GCP, TLE и временные метки исходной полосы;
3. направление пролёта;
4. сохранение исходной ориентации с отметкой о недостатке геоданных.

Для хронологически записанной полосы восходящий пролёт обычно отражается по вертикали, нисходящий сохраняется. Научный исходный файл при этом не меняется.

## 🚀 Быстрый старт в Astra Linux

### 1. Получить ветку

```bash
git clone --branch release/1.2.2 https://github.com/f2re/SatDump.git
cd SatDump
chmod +x scripts/astra/*.sh
```

### 2. Проверить среду

```bash
cat /etc/astra/build_version
bash scripts/astra/check-system.sh --strict
```

### 3. Серверная сборка без GUI

```bash
bash scripts/astra/install-deps.sh \
  --profile headless \
  --bootstrap-missing

bash scripts/astra/build.sh \
  --profile headless \
  --clean \
  --install

bash scripts/astra/run.sh -- version
```

### 4. Рабочая станция с GUI и RTL-SDR

```bash
bash scripts/astra/install-deps.sh \
  --profile desktop \
  --bootstrap-missing

bash scripts/astra/build.sh \
  --profile desktop \
  --sdr rtl \
  --clean \
  --install

### 5. Сборка оффлайн установочного бандла

Для формирования самодостаточного архива без интернет-зависимостей:

```bash
bash scripts/build-and-bundle.sh --profile headless
```

Результат сохранится в `dist/satdump-1.2.2-offline-x86_64.tar.gz`. На целевой машине без доступа в сеть установка выполняется одной командой:

```bash
tar -xzf satdump-1.2.2-offline-x86_64.tar.gz
cd satdump-1.2.2-offline-x86_64
./install.sh
```

> Не запускайте весь `install-deps.sh` через `sudo`. Сценарий сам повышает права только для APT; локальные библиотеки (CMake, NNG, VOLK, FFTW, cURL, TIFF, jemalloc) должны принадлежать обычному пользователю.

## 🛡️ Astra Linux 1.6

Обычно требуются:

- main/update-репозитории точного установленного обновления;
- `repository-dev` и `repository-dev-update` того же обновления;
- GCC/G++ 8 или совместимый компилятор C++17;
- локальный CMake 3.18.6, если системный слишком старый.

```bash
bash scripts/astra/bootstrap-cmake.sh
```

CMake устанавливается в пользовательский каталог и не заменяет системный `/usr/bin/cmake`.

## 🛡️ Astra Linux 1.7

GCC 8 с C++17 обычно доступен штатно. Сценарии всё равно выполняют реальную пробную компиляцию C++17 и при необходимости используют локальные инструменты.

Подробные инструкции:

- [установка в Astra Linux](docs/ru/INSTALL_ASTRA.md);
- [сборка](docs/ru/BUILD.md);
- [уровни проверки Astra](docs/ru/ASTRA_VALIDATION.md).

## 🧱 Профили сборки

| Профиль | Назначение |
|---|---|
| `headless` | CLI, Meteor/NOAA/APT, серверная и пакетная обработка |
| `desktop` | GUI, OpenGL, звук и выбранный SDR |
| `full` | расширенный стенд разработки; требует больше библиотек |

SDR-профили:

```text
none | rtl | common | all
```

Пример:

```bash
bash scripts/astra/build.sh --profile desktop --sdr common
```

## ▶️ Офлайн-обработка

Общая форма:

```text
satdump <pipeline_id> <input_level> <input_file> <output_directory> [параметры]
```

Meteor-M LRPT:

```bash
bash scripts/astra/run.sh -- \
  meteor_m2x_lrpt \
  baseband \
  /data/input/meteor_pass.cs16 \
  /data/output/meteor_pass \
  --samplerate 240000 \
  --baseband_format cs16
```

Pipeline, частота дискретизации и формат должны соответствовать реальной записи.

## 📡 Live-обработка

```bash
bash scripts/astra/run.sh -- sdr_probe
```

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

## 🎛️ Настройка двух оформлений

Настройка внутри пресета композита:

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

Ручные режимы ориентации:

```text
auto | keep | flip_vertical | flip_horizontal | rotate_180
```

У каждого варианта могут быть собственные branding и тема:

```json
{
  "presentation": {
    "minimal": {
      "enabled": true,
      "branding": "Оперативный метеоцентр",
      "theme": {
        "panel": "#101820",
        "accent": "#55C7E8"
      }
    },
    "editorial": {
      "enabled": true,
      "branding": "Спутниковая метеорология",
      "theme": {
        "panel": "#0E1624",
        "panel_secondary": "#172235",
        "text": "#F3F7FB",
        "muted_text": "#AAB8C8",
        "accent": "#4EC7E8"
      }
    }
  }
}
```

## 🌡️ Непрерывная легенда

```json
{
  "presentation": {
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

Цветовая шкала должна быть получена из той же LUT, которая окрашивает продукт.

## 🌈 Неоднозначные RGB-композиты

Когда обычная числовая шкала неприменима, footer всё равно показывает:

```text
R — канал, физическая величина и формула
G — канал, физическая величина и формула
B — канал, физическая величина и формула
```

Для LUT/Lua/C++ перечисляются обнаруженные входы и выводится предупреждение, что один результирующий цвет может зависеть от нескольких величин.

## 🧪 Проверка renderer

Сборка с тестами создаёт семь эталонов:

```text
continuous_editorial_landscape.png
continuous_minimal_landscape.png
categorical_editorial_landscape.png
composite_editorial_landscape.png
composite_minimal_landscape.png
continuous_editorial_portrait.png
continuous_minimal_portrait.png
```

Каталог:

```text
build/astra-<версия>-<профиль>/presentation-test-output/
```

Тестируются также точные отражения, поворот на 180°, восходящий/нисходящий пролёт и перевёрнутая пользовательская проекция.

## 🔬 Научная корректность

- Одноканальный ИК-продукт по умолчанию называется **яркостной температурой**.
- «Температура верхней границы облаков» допустима только для отдельного физически обоснованного retrieval-алгоритма.
- Тип облачности — самостоятельный категориальный многоканальный продукт.
- Радиочастота приёма и спектральный диапазон прибора показываются раздельно.
- Неизвестные параметры не подставляются по названию спутника.

## 🔒 Защищённый контур

- не смешивайте репозитории Astra и Ubuntu/Debian;
- используйте frozen-репозитории своего обновления;
- переносите архивы через утверждённый канал;
- проверяйте контрольные суммы;
- сохраняйте SHA коммита, `CMakeCache.txt` и журнал сборки;
- тестируйте на точной версии Astra, где будет работать бинарник.

## 📚 Документация

- [Индекс документации](docs/ru/README.md)
- [Установка в Astra Linux](docs/ru/INSTALL_ASTRA.md)
- [Сборка](docs/ru/BUILD.md)
- [Проверка Astra Linux](docs/ru/ASTRA_VALIDATION.md)
- [Запуск и обработка](docs/ru/RUN.md)
- [Настройка](docs/ru/CONFIGURATION.md)
- [Плашки и легенды](docs/ru/PRESENTATION.md)
- [Два макета и ориентация](docs/ru/PRESENTATION_LAYOUTS.md)
- [Развёртывание](docs/ru/DEPLOYMENT.md)
- [Диагностика](docs/ru/TROUBLESHOOTING.md)

## 🤝 Разработка

Основная линия:

```text
release/1.2.2
```

Перед публикацией изменения renderer должны пройти:

```bash
bash scripts/astra/build.sh --profile headless --clean
```

Затем требуется визуальная проверка портретных и альбомных эталонов и хотя бы одного реального восходящего и нисходящего пролёта.

## 📄 Лицензия

Проект распространяется на условиях GNU GPL v3. См. [LICENSE](LICENSE).
