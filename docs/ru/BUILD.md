# 🔨 Сборка SatDump 1.2.2 Presentation

В ветке `release/1.2.2` предусмотрены два независимых режима сборки для Astra Linux:

```text
native
  └─ сборка, установка и запуск на конкретной машине Astra Linux

portable-glibc224
  └─ сборка в изолированном Debian Stretch/glibc 2.24
     и выпуск переносимого CLI-бандла
```

Режим выбирается единым диспетчером:

```bash
bash scripts/astra/build.sh --mode <режим> [параметры]
```

Без `--mode` используется `native`, поэтому старые команды остаются рабочими.

## 1. Native: рекомендуемый режим для рабочей станции

Native-сборка использует пакеты, компилятор и системные библиотеки фактической Astra Linux. Она обязательна для GUI, локального SDR, OpenGL, USB/udev и проверки конкретного оперативного обновления ОС.

### 1.1 Полный цикл headless

```bash
cat /etc/astra/build_version
bash scripts/astra/check-system.sh --strict

bash scripts/astra/install-deps.sh \
  --profile headless \
  --bootstrap-missing

bash scripts/astra/build.sh \
  --mode native \
  --profile headless \
  --clean \
  --install

bash scripts/astra/run.sh -- version
```

Эта последовательность проверяет не только компиляцию, но и установленное дерево ресурсов, pipelines, библиотек и исполняемый файл.

### 1.2 Desktop с GUI и RTL-SDR

```bash
bash scripts/astra/install-deps.sh \
  --profile desktop \
  --bootstrap-missing

bash scripts/astra/build.sh \
  --mode native \
  --profile desktop \
  --sdr rtl \
  --clean \
  --install

bash scripts/astra/run.sh --ui
```

## 2. Native-профили

### `headless`

Включает:

- CLI;
- Meteor-M LRPT/HRPT;
- NOAA/MetOp;
- NOAA APT и аналоговые протоколы;
- стандартные C++-композиты;
- Minimal и Presentation;
- легенды и JSON-паспорта;
- smoke-тесты оформления.

Отключает GUI, локальные SDR-драйверы, аудио, OpenCL, ZIQ и архитектурные SIMD-плагины.

```bash
bash scripts/astra/build.sh --mode native --profile headless
```

### `desktop`

Дополнительно включает GUI, OpenGL/GLFW, PortAudio и выбранные SDR-плагины.

```bash
bash scripts/astra/build.sh \
  --mode native \
  --profile desktop \
  --sdr rtl
```

### `full`

Включает `PLUGINS_ALL=ON` и расширенный набор устройств. Этот профиль собирается под конкретный стенд и не является минимальным baseline совместимости.

```bash
bash scripts/astra/build.sh \
  --mode native \
  --profile full \
  --sdr common
```

## 3. SDR-профили native

| Значение | Назначение |
|---|---|
| `none` | без локальных SDR-драйверов |
| `rtl` | RTL-SDR |
| `common` | RTL-SDR, Airspy, AirspyHF, HackRF |
| `all` | расширенный набор поддерживаемых драйверов |
| `auto` | `none` для headless, `rtl` для desktop, `common` для full |

Каждый включённый драйвер требует соответствующего `-dev` пакета.

## 4. Каталоги native-сборки

По умолчанию:

```text
build/astra-<версия>-<профиль>/
```

Например:

```text
build/astra-1.7-headless/
```

Пользовательский каталог и prefix:

```bash
bash scripts/astra/build.sh \
  --mode native \
  --profile headless \
  --build-dir /data/build/satdump \
  --prefix /data/opt/satdump-1.2.2 \
  --install
```

## 5. Тип native-сборки

Производственный вариант:

```bash
bash scripts/astra/build.sh \
  --mode native \
  --profile headless \
  --build-type Release
```

Диагностический вариант:

```bash
bash scripts/astra/build.sh \
  --mode native \
  --profile headless \
  --build-type RelWithDebInfo
```

`Debug` предназначен только для разработки.

## 6. Переносимость native-бинарника

В профилях `headless` и `desktop` архитектурные плагины SSE4.1, AVX2 и NEON отключены. Оптимизированные вычислительные ядра выбирает VOLK во время выполнения.

Сам SatDump 1.2.2 в используемом дереве не добавляет глобальный `-march=native`; сценарий всё равно проверяет итоговый `CMakeCache.txt` и сохраняет параметры сборки в манифесте.

Native-установка предназначена прежде всего для той машины, где она собрана. Если локальные NNG/VOLK установлены в пользовательский prefix, установленный SatDump сохраняет путь к этому prefix в RPATH. Для переноса на другую машину используйте `portable-glibc224`, а не копирование native-каталога.

## 7. Native OpenCL и ZIQ

OpenCL:

```bash
bash scripts/astra/build.sh \
  --mode native \
  --profile desktop \
  --with-opencl
```

Требуется работоспособный OpenCL ICD и драйвер устройства.

ZIQ:

```bash
bash scripts/astra/build.sh \
  --mode native \
  --profile headless \
  --with-ziq
```

Требуется `libzstd-dev`.

## 8. Дополнительные CMake-параметры

Параметры передаются после `--`:

```bash
bash scripts/astra/build.sh \
  --mode native \
  --profile headless \
  -- \
  -DPLUGIN_FY3=ON \
  -DPLUGIN_EOS=ON
```

Полный перечень находится в `plugins/CMakeLists.txt`.

## 9. Native-установка и запуск

Пользовательская установка:

```bash
bash scripts/astra/build.sh \
  --mode native \
  --profile headless \
  --prefix "$HOME/.local/opt/satdump-1.2.2" \
  --install

bash scripts/astra/run.sh \
  --prefix "$HOME/.local/opt/satdump-1.2.2" \
  -- version
```

Системная установка:

```bash
bash scripts/astra/build.sh \
  --mode native \
  --profile headless \
  --prefix /opt/satdump-1.2.2 \
  --install

bash scripts/astra/run.sh \
  --prefix /opt/satdump-1.2.2 \
  -- version
```

Сценарий повышает права только для `cmake --install`, когда prefix недоступен текущему пользователю.

## 10. Portable glibc 2.24

Portable-режим повторяет проверенную технологию старой ветки `astra`, но собирает текущее чистое дерево `release/1.2.2`.

```bash
bash scripts/astra/build.sh \
  --mode portable-glibc224 \
  --profile reference
```

Фиксируются:

```text
Debian Stretch / glibc 2.24
GCC 9.5.0
CMake 3.27.9
NNG 1.8.0
SatDump 1.2.2
```

Результат:

```text
dist/astra-portable/
├── satdump-1.2.2-presentation-reference-glibc224-x86_64/
├── satdump-1.2.2-presentation-reference-glibc224-x86_64.tar.gz
└── satdump-1.2.2-presentation-reference-glibc224-x86_64.tar.gz.sha256
```

Профили:

- `reference` — `PLUGINS_ALL=ON`, максимально близко к рабочей сборке `astra`;
- `meteor` — Meteor/NOAA/APT и presentation renderer.

Подробно: [Portable-сборка для Astra Linux](PORTABLE_ASTRA.md).

## 11. Проверка native-результата

После сборки должны существовать семь эталонных PNG в:

```text
build/astra-<версия>-<профиль>/presentation-test-output/
```

После установки обязательно выполните:

```bash
bash scripts/astra/run.sh --prefix <prefix> -- version
```

Для воспроизводимости:

```bash
bash scripts/astra/collect-build-info.sh \
  --build-dir build/astra-1.7-headless \
  --output build/astra-1.7-headless/astra-build-manifest.txt
```

## 12. Что подтверждает CI

CI выполняет для режимов `ASTRA_VERSION_OVERRIDE=1.6` и `1.7`:

- dry-run установщика зависимостей;
- native-конфигурацию и компиляцию;
- `cmake --install` в чистый пользовательский prefix;
- запуск установленного бинарника через `run.sh`;
- smoke-тесты плашек, легенд и ориентации.

Эта матрица работает на Ubuntu runner и не заменяет финальную сборку на реальной Astra Linux соответствующего оперативного обновления. Отдельный portable job действительно собирает бинарники в Debian Stretch/glibc 2.24, а официальный Astra UBI 1.7 может запускаться вручную из workflow.

## 13. Приёмка на реальной Astra

Перед эксплуатационным тегом нужны:

1. native headless build/install/run на фактической Astra Linux 1.6;
2. native headless build/install/run на фактической Astra Linux 1.7;
3. при необходимости desktop-сборка с Fly и реальным SDR;
4. обработка восходящего и нисходящего пролёта;
5. визуальная проверка Minimal и Presentation;
6. проверка исходного геопривязанного продукта.
