# ⚙️ Настройка SatDump 1.2.2 Presentation

## 1. Какие конфигурационные файлы используются

### Основная конфигурация

После установки:

```text
<префикс>/share/satdump/satdump_cfg.json
```

При запуске из каталога, где лежит `satdump_cfg.json`, SatDump сначала использует
локальный файл.

### Пользовательские настройки

В Linux:

```text
~/.config/satdump/settings.json
```

Если в текущем рабочем каталоге есть `settings.json`, он имеет приоритет.

`settings.json` — не полная копия основной конфигурации, а JSON-разница, которая
накладывается поверх `satdump_cfg.json`.

### TLE

```text
~/.config/satdump/satdump_tles.txt
```

Пользовательский файл можно передать через CLI:

```bash
--tle_override /data/orbits/custom_tle.txt
```

## 2. Безопасный порядок изменения

1. Сохраните резервную копию.
2. Проверьте JSON:

```bash
python3 -m json.tool ~/.config/satdump/settings.json >/dev/null
```

3. Меняйте одну группу параметров за раз.
4. Запускайте SatDump из терминала и контролируйте журнал.
5. Для эксплуатационной конфигурации храните файл в системе контроля версий без
   паролей и персональных данных.

Не редактируйте установленный `satdump_cfg.json` для персональных настроек — он
может быть заменён при обновлении.

## 3. Основные разделы

| Раздел | Назначение |
|---|---|
| `user_interface` | тема, масштаб GUI, поведение окон |
| `satdump_general` | логирование, форматы изображений, QTH, автообработка |
| `satdump_directories` | каталоги входа, выхода, записи и проекций |
| `viewer.instruments` | композиты, проекции и оформление по приборам |
| `advanced_settings` | размеры буферов и служебные параметры |
| `plugin_settings` | параметры отдельных плагинов |

## 4. Форматы изображений

Пример пользовательского diff:

```json
{
  "satdump_general": {
    "product_format": {
      "value": "png"
    },
    "image_format": {
      "value": "png"
    }
  }
}
```

Рекомендации:

- `png` — оформленные и обычные визуальные продукты;
- `tif` — геопривязанные/научные продукты;
- `jpg` — только для просмотра, не для количественной обработки;
- `qoi` — быстрый 8-битный служебный формат.

Presentation renderer всегда создаёт дополнительный PNG, независимо от формата
научного продукта.

## 5. Каталоги

```json
{
  "satdump_directories": {
    "recording_path": {
      "value": "/data/satdump/recordings"
    },
    "live_processing_path": {
      "value": "/data/satdump/live"
    },
    "default_input_directory": {
      "value": "/data/satdump/incoming"
    },
    "default_output_directory": {
      "value": "/data/satdump/products"
    }
  }
}
```

Создайте каталоги заранее и назначьте владельца:

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

Координаты:

- северная широта и восточная долгота — положительные;
- южная широта и западная долгота — отрицательные;
- высота — метры над уровнем моря.

## 7. Логирование

```json
{
  "satdump_general": {
    "log_level": {
      "value": "info"
    },
    "log_to_file": {
      "value": true
    }
  }
}
```

Уровни:

```text
trace → debug → info → warn → error → critical
```

Для штатной эксплуатации используйте `info`; для диагностики — `debug` или
`trace` на ограниченное время.

## 8. Глобальное включение presentation renderer

Renderer включён по умолчанию. Отключение через пользовательский diff:

```json
{
  "satdump_general": {
    "presentation_enabled": {
      "value": false
    }
  }
}
```

Включение:

```json
{
  "satdump_general": {
    "presentation_enabled": {
      "value": true
    }
  }
}
```

## 9. Настройка отдельного композита

Секция `presentation` располагается рядом с `equation`, `lut`, `project` и
другими параметрами композита:

```json
{
  "name": "Cloud Temperature",
  "equation": "...",
  "presentation": {
    "enabled": true,
    "title": "Яркостная температура облачной поверхности",
    "branding": "Метеоцентр · SatDump 1.2.2",
    "legend": {
      "kind": "continuous",
      "title": "Яркостная температура",
      "unit": "K",
      "min": 180,
      "max": 320,
      "tick_count": 8,
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

Поле `presentation` не входит в старую структуру `ImageCompositeCfg` и поэтому
не ломает совместимость пресетов SatDump 1.2.2.

## 10. Отключение для одного продукта

```json
"presentation": false
```

или:

```json
"presentation": {
  "enabled": false
}
```

## 11. Тема оформления

```json
{
  "presentation": {
    "theme": {
      "panel": "#0E1624",
      "panel_secondary": "#172235",
      "text": "#F3F7FB",
      "muted_text": "#AAB8C8",
      "accent": "#4EC7E8"
    }
  }
}
```

Цвет задаётся `#RRGGBB` или массивом `[R, G, B]` в диапазоне 0…1/0…255.

Не используйте цветовой акцент для больших фоновых областей. Он предназначен
для линии иерархии, статуса и маркеров R/G/B.

## 12. Непрерывная легенда

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
  "notes": [
    "Яркостная температура излучающей поверхности."
  ]
}
```

Цвета изображения и шкалы должны происходить из одной спецификации.

## 13. Категориальная легенда

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

Классы и цвета должны задаваться алгоритмом тематической обработки.

## 14. Явное описание RGB

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

## 15. Метаданные приёма

Renderer распознаёт нормализованный блок:

```json
{
  "acquisition": {
    "pass": {
      "direction": "нисходящий пролёт",
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

Если этих полей нет, они не показываются. Renderer не угадывает частоту по
названию спутника.

## 16. JSON-паспорт

Рядом с PNG создаётся:

```text
<имя>_annotated.json
```

Схема:

```text
satdump.presentation/1
```

В паспорте записаны:

- текст шапки;
- поля пролёта;
- тип легенды;
- цвета и деления;
- категории;
- формулы R/G/B;
- примечания;
- branding.

Следующий документ: [Плашки и легенды](PRESENTATION.md).
