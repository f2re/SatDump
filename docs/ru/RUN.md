# ▶️ Запуск и обработка спутниковых данных

## 1. Проверка установки

```bash
bash scripts/astra/run.sh -- version
```

Ожидается строка с SatDump 1.2.2 и SHA сборки.

Проверка доступных SDR:

```bash
bash scripts/astra/run.sh -- sdr_probe
```

## 2. Запуск GUI

```bash
bash scripts/astra/run.sh --ui
```

GUI доступен только в профиле `desktop` или `full`.

Не запускайте GUI от root. Для доступа к USB-устройству настройте правила udev и
группы пользователя согласно документации производителя SDR.

## 3. Общая форма CLI

Офлайн-обработка:

```text
satdump <pipeline_id> <input_level> <input_file> <output_directory> [параметры]
```

Через Astra-обёртку:

```bash
bash scripts/astra/run.sh -- \
  <pipeline_id> \
  <input_level> \
  <input_file> \
  <output_directory> \
  [параметры]
```

## 4. Входные уровни

Точный набор зависит от pipeline. Часто встречаются:

| Уровень | Данные |
|---|---|
| `baseband` | комплексная IQ-запись |
| `soft` / `soft_symbols` | мягкие символы демодулятора |
| `frames` | синхронизированные кадры |
| `cadu` | CCSDS CADU |
| `products` | уже декодированный продукт |

Используйте тот уровень, который соответствует реальному файлу. Неверный
уровень нельзя компенсировать только сменой расширения.

## 5. Пример: Meteor-M LRPT из IQ

```bash
bash scripts/astra/run.sh -- \
  meteor_m2x_lrpt \
  baseband \
  /data/input/meteor_pass.cs16 \
  /data/output/meteor_pass \
  --samplerate 240000 \
  --baseband_format cs16
```

Значения `pipeline_id`, samplerate и формата приведены как пример. Используйте
фактические параметры приёмной записи.

## 6. Пример: MetOp AHRPT

```bash
bash scripts/astra/run.sh -- \
  metop_ahrpt \
  baseband \
  /data/input/metop.cs16 \
  /data/output/metop \
  --samplerate 6000000 \
  --baseband_format cs16
```

## 7. Live-обработка

Форма:

```text
satdump live <pipeline_id> <output_directory> \
  --source <source> \
  --samplerate <Hz> \
  --frequency <Hz> \
  [параметры устройства]
```

Пример с RTL-SDR:

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

Перед запуском:

```bash
bash scripts/astra/run.sh -- sdr_probe
```

Параметры источника зависят от конкретного плагина и устройства.

## 8. Запись IQ

Форма:

```text
satdump record <output_without_extension> [параметры]
```

Пример:

```bash
bash scripts/astra/run.sh -- \
  record /data/recordings/meteor_20260721 \
  --source rtlsdr \
  --samplerate 240000 \
  --frequency 137900000 \
  --baseband_format cs16 \
  --timeout 900
```

Проверьте свободное место перед записью:

```bash
df -h /data/recordings
```

## 9. Распространённые параметры

| Параметр | Назначение |
|---|---|
| `--samplerate` | частота дискретизации, Гц |
| `--baseband_format` | `cf32`, `cs16`, `cs8`, `cu8` и другие поддерживаемые форматы |
| `--dc_block` | удаление постоянной составляющей |
| `--iq_swap` | перестановка I/Q |
| `--freq_shift` | цифровой сдвиг частоты |
| `--tle_override` | путь к пользовательскому TLE-файлу |
| `--timeout` | длительность live/record, секунд |
| `--source` | тип SDR-источника |
| `--source_id` | конкретный экземпляр устройства |

Любой параметр pipeline или модуля может передаваться в CLI в том же формате.

## 10. Каталог результата

Типовая структура зависит от спутника и прибора:

```text
output/
├── dataset.json
├── product.cbor
├── MSU-MR/
│   ├── ... исходные каналы ...
│   ├── ... композиты ...
│   ├── *_projected.png или *.tif
│   ├── *_annotated.png
│   └── *_annotated.json
└── satdump.log
```

Фактические имена формируются pipeline и настройками инструмента.

## 11. Оформленные изображения

Presentation renderer выбирает наиболее полный вариант:

1. географическая проекция с картографическими слоями;
2. геометрически исправленный растр с картой;
3. растр с картой;
4. геометрически исправленный растр;
5. базовый композит.

Создаются:

```text
<имя>_annotated.png
<имя>_annotated.json
```

Исходный научный продукт не заменяется.

## 12. Автоматическая RGB-легенда

Для обычного трёхкомпонентного выражения нижняя панель содержит:

```text
R — канал/формула
G — канал/формула
B — канал/формула
```

Для LUT, Lua и C++-композитов перечисляются обнаруженные входные каналы и
указывается, что результирующий цвет может зависеть от нескольких величин.

## 13. Температурные продукты

Для физической шкалы настройте `presentation.legend.kind = continuous` в
пресете. Без явной шкалы композит будет описан как состав каналов, а не получит
выдуманную числовую легенду.

Наименование должно соответствовать алгоритму:

- `Яркостная температура, K` — для калиброванного ИК-канала;
- `Температура верхней границы облаков` — только для отдельного алгоритма
  восстановления температуры верхней границы.

## 14. Остановка live-режима

Нажмите `Ctrl+C`. SatDump обрабатывает SIGINT/SIGTERM и останавливает pipeline
штатно. Не используйте `kill -9`, если нет зависания: это может оставить
незавершённые файлы.

## 15. Пакетная обработка

Пример:

```bash
#!/usr/bin/env bash
set -Eeuo pipefail

for file in /data/incoming/*.cs16; do
    name="$(basename "${file}" .cs16)"
    bash /opt/src/SatDump/scripts/astra/run.sh -- \
      meteor_m2x_lrpt \
      baseband \
      "${file}" \
      "/data/processed/${name}" \
      --samplerate 240000 \
      --baseband_format cs16

done
```

Добавьте блокировку, журналирование и перемещение успешно обработанных файлов,
если скрипт запускается планировщиком.

## 16. Права и каталоги

Рекомендуемая схема:

```text
/data/satdump/incoming      входные файлы, только чтение после приёма
/data/satdump/working       временная обработка
/data/satdump/products      готовые продукты
/data/satdump/archive       архив исходных данных
```

У процесса должны быть права на `working` и `products`, но не обязательно на
системные каталоги.

Следующий документ: [Настройка](CONFIGURATION.md).
