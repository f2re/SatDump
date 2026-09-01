# 📚 Документация SatDump 1.2.2 Presentation

Эта документация относится к ветке **`release/1.2.2`** форка `f2re/SatDump`.
Форк сохраняет возможности SatDump 1.2.2 и добавляет оформление готовых
спутниковых изображений: два варианта плашек, легенды, нормализацию ориентации
«север сверху» и JSON-паспорт.

## С чего начать

| Документ | Для чего |
|---|---|
| [Установка в Astra Linux](INSTALL_ASTRA.md) | Astra Linux SE 1.6/1.7, репозитории, зависимости, офлайн-контур |
| [Сборка](BUILD.md) | Профили, параметры CMake, переносимость и тесты |
| [Portable glibc 2.24](PORTABLE_ASTRA.md) | Переносимый CLI-бандл, повторяющий рабочую технологию ветки `astra` |
| [Проверка Astra Linux](ASTRA_VALIDATION.md) | Уровни CI/UBI/нативной проверки, эталонные изображения и приёмка |
| [Запуск и обработка](RUN.md) | CLI, GUI, офлайн- и live-обработка |
| [Настройка](CONFIGURATION.md) | `satdump_cfg.json`, `settings.json`, каталоги, TLE и логирование |
| [Плашки и легенды](PRESENTATION.md) | Схема `presentation`, RGB, температуры, категории, темы |
| [Два макета и ориентация](PRESENTATION_LAYOUTS.md) | Minimal/Presentation, вертикальный и горизонтальный кадр, север сверху |
| [Карта, города и пункт приёма](MAP_OVERLAYS.md) | Русские подписи, генерализация, цвета, прозрачность и QTH |
| [Развёртывание](DEPLOYMENT.md) | Установка на рабочую станцию/сервер, обновления и откат |
| [Диагностика](TROUBLESHOOTING.md) | Ошибки CMake, C++17, библиотек, GUI и обработки |

## Два режима сборки Astra

### Нативный

Для установки на ту же машину, где выполняется сборка:

```bash
bash scripts/astra/install-deps.sh --profile headless --bootstrap-missing
bash scripts/astra/build.sh --mode native --profile headless --install
bash scripts/astra/run.sh -- version
```

Параметр `--mode native` можно опустить: он используется по умолчанию.

### Переносимый glibc 2.24

Для сборки один раз и развёртывания на нескольких Astra Linux 1.6/1.7:

```bash
bash scripts/astra/build.sh \
  --mode portable-glibc224 \
  --profile reference
```

Этот режим создаёт изолированный Debian Stretch chroot, фиксирует toolchain,
устанавливает результат в чистый staging и формирует самодостаточный tar.gz.
Подробно: [Portable glibc 2.24](PORTABLE_ASTRA.md).

## Графическая рабочая станция

```bash
bash scripts/astra/install-deps.sh --profile desktop --bootstrap-missing
bash scripts/astra/build.sh --mode native --profile desktop --sdr rtl --install
bash scripts/astra/run.sh --ui
```

GUI и локальные SDR-драйверы относятся к native-профилю. Эталонный portable-бандл
является CLI-only.

## Какие файлы создаются

Для оформленного продукта по умолчанию сохраняются связанные результаты:

```text
<имя>.png / <имя>.tif                    исходный продукт SatDump
<имя>_annotated_minimal.png              компактное оперативное оформление
<имя>_annotated_minimal.json             паспорт компактного оформления
<имя>_annotated_presentation.png         расширенное презентационное оформление
<имя>_annotated_presentation.json        паспорт презентационного оформления
```

Совместимый файл `<имя>_annotated.png` можно включить отдельно параметром
`presentation.outputs.legacy_alias`.

Геопривязанный научный растр не расширяется плашками и остаётся пригодным для
ГИС и количественного анализа. Ориентация исправляется только в презентационных
копиях; исходный продукт остаётся нетронутым.

## Правило достоверности

Плашка показывает только сведения, которые действительно присутствуют в
продукте или метаданных сеанса. Частота приёма, SNR, максимальная высота пролёта
и другие параметры не подставляются по названию спутника. Если направление или
географическую ориентацию подтвердить нельзя, это явно записывается в JSON-паспорт.
