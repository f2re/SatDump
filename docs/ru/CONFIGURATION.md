# ⚙️ Настройка SatDump 1.2.2 Presentation

## 1. Конфигурационные файлы

### Основная конфигурация

После установки:

```text
<префикс>/share/satdump/satdump_cfg.json
```

При запуске из каталога, где находится `satdump_cfg.json`, SatDump сначала использует локальный файл.

### Пользовательские настройки

В Linux:

```text
~/.config/satdump/settings.json
```

Если в текущем рабочем каталоге есть `settings.json`, он имеет приоритет.
`settings.json` — JSON-разница, накладываемая поверх основной конфигурации, а не её полная копия.

### TLE

```text
~/.config/satdump/satdump_tles.txt
```

Пользовательский файл:

```bash
--tle_override /data/orbits/custom_tle.txt
```

## 2. Безопасный порядок изменения

1. Сохраните резервную копию.
2. Проверяйте синтаксис:

```bash
python3 -m json.tool ~/.config/satdump/settings.json >/dev/null
```

3. Меняйте одну группу параметров за раз.
4. Запускайте SatDump из терминала и проверяйте журнал.
5. Храните эксплуатационную конфигурацию в системе контроля версий без секретов.

Не редактируйте установленный `satdump_cfg.json` для персональных настроек: он может быть заменён при обновлении.

## 3. Основные разделы

| Раздел | Назначение |
|---|---|
| `user_interface` | тема, масштаб GUI, поведение окон |
| `satdump_general` | логирование, форматы, QTH и глобальное оформление |
| `satdump_directories` | каталоги входа, выхода, записи и проекций |
| `viewer.instruments` | композиты, проекции и оформление по приборам |
| `advanced_settings` | буферы и служебные параметры |
| `plugin_settings` | настройки отдельных плагинов |

## 4. Форматы изображений

```json
{
  "satdump_general": {
    "product_format": { "value": "png" },
    "image_format": { "value": "png" }
  }
}
```

Рекомендации:

- `png` — обычные и оформленные визуальные продукты;
- `tif` — геопривязанные и научные продукты;
- `jpg` — только для просмотра;
- `qoi` — быстрый 8-битный служебный формат.

Presentation renderer создаёт дополнительные PNG независимо от формата научного продукта.

## 5. Каталоги

```json
{
  "satdump_directories": {
    "recording_path": { "value": "/data/satdump/recordings" },
    "live_processing_path": { "value": "/data/satdump/live" },
    "default_input_directory": { "value": "/data/satdump/incoming" },
    "default_output_directory": { "value": "/data/satdump/products" }
  }
}
```

```bash
sudo install -d -o satdump -g satdump -m 0750 \
  /data/satdump/recordings \
  /data/satdump/live \
  /data/satdump/incoming \
  /data/satdump/products
```

Имена пользователя и группы приведены как пример.

## 6. QTH станции

```json
{
  "satdump_general": {
    "qth_lat": { "value": 55.7558 },
    "qth_lon": { "value": 37.6176 },
    "qth_alt": { "value": 180.0 },
    "default_qth_label": { "value": "Приёмная станция" }
  }
}
```

Северная широта и восточная долгота положительны; южная и западная отрицательны. Высота задаётся в метрах.

## 7. Логирование

```json
{
  "satdump_general": {
    "log_level": { "value": "info" },
    "log_to_file": { "value": true }
  }
}
```

Уровни:

```text
trace → debug → info → warn → error → critical
```

Для штатной эксплуатации используйте `info`; для кратковременной диагностики — `debug` или `trace`.

## 8. Глобальная настройка оформления

Современная форма:

```json
{
  "satdump_general": {
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
}
```

Старый совместимый переключатель также поддерживается:

```json
{
  "satdump_general": {
    "presentation_enabled": { "value": true }
  }
}
```

При конфликте настройка конкретного пресета имеет приоритет над глобальной.

## 9. Выходные файлы

По умолчанию:

```text
<имя>_annotated_minimal.png
<имя>_annotated_minimal.json
<имя>_annotated_presentation.png
<имя>_annotated_presentation.json
```

Совместимый alias:

```json
{
  "presentation": {
    "outputs": {
      "legacy_alias": true
    }
  }
}
```

Тогда дополнительно создаются `<имя>_annotated.png` и `<имя>_annotated.json`.

Краткая запись:

```json
{
  "presentation": {
    "outputs": ["minimal", "editorial"]
  }
}
```

Допустимые имена: `minimal`, `compact`, `editorial`, `presentation`, `presentational`, `legacy`, `annotated`.

## 10. Настройка отдельного композита

Секция `presentation` располагается рядом с `equation`, `lut`, `project` и другими параметрами:

```json
{
  "name": "Cloud Temperature",
  "equation": "...",
  "presentation": {
    "enabled": true,
    "title": "Яркостная температура облачной поверхности",
    "outputs": {
      "minimal": true,
      "presentation": true
    },
    "orientation": {
      "mode": "auto",
      "north_up": true
    },
    "legend": {
      "kind": "continuous",
      "title": "Яркостная температура",
      "unit": "K",
      "min": 180,
      "max": 320,
      "tick_count": 8,
      "colors": ["#1B1844", "#1E5E8B", "#24A093", "#96C959", "#FCE725"]
    }
  }
}
```

Поле `presentation` не входит в старую структуру `ImageCompositeCfg` и не ломает совместимость пресетов 1.2.2.

## 11. Отключение

Для одного продукта:

```json
"presentation": false
```

или:

```json
"presentation": { "enabled": false }
```

Отключить только один вариант:

```json
{
  "presentation": {
    "minimal": { "enabled": false },
    "editorial": { "enabled": true }
  }
}
```

## 12. Ориентация снимка

```json
{
  "presentation": {
    "orientation": {
      "mode": "auto",
      "north_up": true
    }
  }
}
```

Режимы:

| Режим | Действие |
|---|---|
| `auto` | целевая проекция → GCP/TLE/timestamps → направление пролёта |
| `keep` | оставить исходную ориентацию |
| `flip_vertical` | отразить сверху вниз |
| `flip_horizontal` | отразить слева направо |
| `rotate_180` | повернуть на 180° без интерполяции |

Автокоррекция применяется только к оформленной копии. Исходный научный продукт не изменяется.

## 13. Общая тема

```json
{
  "presentation": {
    "theme": {
      "panel": "#0E1624",
      "panel_secondary": "#172235",
      "border": "#2A3A50",
      "text": "#F3F7FB",
      "muted_text": "#AAB8C8",
      "accent": "#4EC7E8"
    }
  }
}
```

Цвет задаётся как `#RRGGBB`, `#RRGGBBAA` либо массив `[R,G,B]` в диапазоне `0…1` или `0…255`.

## 14. Раздельные темы Minimal и Presentation

```json
{
  "presentation": {
    "minimal": {
      "branding": "Оперативный метеоцентр",
      "show_branding": true,
      "theme": {
        "panel": "#101820",
        "accent": "#55C7E8"
      }
    },
    "editorial": {
      "branding": "Спутниковая метеорология",
      "show_branding": true,
      "theme": {
        "panel": "#0E1624",
        "panel_secondary": "#172235",
        "reference_width": 1600,
        "minimum_scale": 0.58,
        "maximum_scale": 2.0
      }
    }
  }
}
```

Акцент предназначен для линии иерархии, статуса и маркеров `R/G/B`, а не для больших фоновых областей.

## 15. Непрерывная легенда

```json
{
  "kind": "continuous",
  "title": "Яркостная температура",
  "unit": "K",
  "min": 180,
  "max": 320,
  "ticks": [180, 200, 220, 240, 260, 280, 300, 320],
  "colors": [
    { "position": 0.0, "color": "#1B1844" },
    { "position": 0.5, "color": "#24A093" },
    { "position": 1.0, "color": "#FCE725" }
  ],
  "notes": ["Яркостная температура излучающей поверхности."]
}
```

Цвета изображения и шкалы должны происходить из одной спецификации.

## 16. Категориальная легенда

```json
{
  "kind": "categorical",
  "title": "Тип облачности",
  "categories": [
    { "color": "#101820", "label": "Ясно" },
    { "color": "#D9E6F2", "label": "Низкая облачность" },
    { "color": "#78A8CC", "label": "Средняя облачность" },
    { "color": "#355C9A", "label": "Высокая облачность" },
    { "color": "#8C5DA8", "label": "Перистая" },
    { "color": "#D84A4A", "label": "Мощная конвективная" },
    { "color": "#646B73", "label": "Не определено" }
  ]
}
```

Классы и цвета задаются алгоритмом тематической обработки.

## 17. Явное описание RGB

Автоматический анализ включён всегда, но экспертное описание предпочтительнее:

```json
{
  "kind": "composite",
  "title": "Ночная микрофизика облаков",
  "components": [
    {
      "component": "R",
      "color": "#F24747",
      "description": "T(12,0 мкм) − T(10,8 мкм), диапазон −4…2 K"
    },
    {
      "component": "G",
      "color": "#40D17A",
      "description": "T(10,8 мкм) − T(3,9 мкм), диапазон 0…15 K"
    },
    {
      "component": "B",
      "color": "#4D94FF",
      "description": "T(10,8 мкм), 243…293 K, инверсия"
    }
  ],
  "notes": [
    "Результирующий цвет не является самостоятельной физической величиной."
  ]
}
```

## 18. Метаданные приёма

```json
{
  "acquisition": {
    "pass": {
      "direction": "descending",
      "max_elevation_deg": 67.2
    },
    "downlink": {
      "center_frequency_hz": 137900000,
      "sample_rate_hz": 240000
    }
  },
  "quality": {
    "score": 94,
    "packet_loss_percent": 0.8,
    "snr_db": 17.4
  }
}
```

Рекомендуемые значения направления: `ascending` и `descending`. Русские формы также распознаются. Отсутствующие поля не угадываются по названию спутника.

## 19. JSON-паспорт

Схема:

```text
satdump.presentation/2
```

Паспорт содержит:

- тип макета;
- размеры исходного и итогового кадра;
- классификацию кадра как portrait/landscape/square;
- запрос и результат проверки «север сверху»;
- применённое отражение или поворот;
- источник решения: проекция, GCP либо направление пролёта;
- текст шапки и поля пролёта;
- вид легенды, цвета, деления и категории;
- формулы `R/G/B`;
- branding.

Подробно: [Два макета и ориентация](PRESENTATION_LAYOUTS.md).
