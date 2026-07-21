# 🔨 Сборка SatDump 1.2.2 Presentation

## 1. Рекомендуемый способ

Используйте профильный сценарий:

```bash
bash scripts/astra/build.sh --profile headless
```

Он выполняет:

1. определение Astra Linux 1.6/1.7;
2. выбор реального C++17-компилятора;
3. выбор CMake 3.18+;
4. подключение локальных NNG/VOLK;
5. формирование переносимого набора плагинов;
6. сборку `satdump`, при необходимости `satdump-ui`;
7. smoke-тест плашек и легенд;
8. создание `astra-env.sh` в каталоге сборки.

## 2. Профили

### 2.1 `headless`

```bash
bash scripts/astra/build.sh --profile headless
```

Включено:

- CLI;
- Meteor-M;
- NOAA/MetOp;
- NOAA APT/аналоговые протоколы;
- стандартные C++-композиты;
- presentation renderer;
- тесты оформления.

Отключено:

- GUI;
- SDR-драйверы;
- аудио;
- OpenCL;
- ZIQ;
- архитектурные SIMD-плагины.

VOLK остаётся включён и самостоятельно выбирает подходящие оптимизированные
ядра во время выполнения.

### 2.2 `desktop`

```bash
bash scripts/astra/build.sh --profile desktop --sdr rtl
```

Дополнительно:

- `satdump-ui`;
- OpenGL/GLFW;
- PortAudio;
- RTL-SDR.

### 2.3 `full`

```bash
bash scripts/astra/build.sh --profile full --sdr common
```

Включает все протокольные плагины через `PLUGINS_ALL=ON`. Возможны дополнительные
требования HDF5, OpenCL, библиотек SDR и производителей оборудования.

Для производственной Astra-системы сначала доведите до стабильности `headless`
или `desktop`, затем расширяйте набор плагинов по одному.

## 3. SDR-профили

| Значение | Плагины |
|---|---|
| `none` | без локальных SDR-драйверов |
| `rtl` | RTL-SDR |
| `common` | RTL-SDR, Airspy, AirspyHF, HackRF |
| `all` | расширенный набор поддерживаемых драйверов |
| `auto` | `none` для headless, `rtl` для desktop, `common` для full |

Пример:

```bash
bash scripts/astra/build.sh \
  --profile desktop \
  --sdr common
```

Каждый включённый драйвер требует соответствующего `-dev` пакета.

## 4. Каталоги

По умолчанию:

```text
build/astra-<версия>-<профиль>/
```

Например:

```text
build/astra-1.7-headless/
```

Пользовательский каталог:

```bash
bash scripts/astra/build.sh \
  --profile headless \
  --build-dir /data/build/satdump
```

## 5. Тип сборки

Производственный вариант:

```bash
bash scripts/astra/build.sh \
  --profile headless \
  --build-type Release
```

Для диагностики без полного отказа от оптимизации:

```bash
bash scripts/astra/build.sh \
  --profile headless \
  --build-type RelWithDebInfo
```

`Debug` предназначен только для разработки.

## 6. Переносимость

В оригинальном SatDump 1.2.2 при локальной Unix-сборке автоматически добавляется
`-march=native`. Это может сделать бинарник непереносимым на другой процессор.

Astra-сценарий конфигурирует проект с переменной окружения `CI=astra-linux`,
которая отключает эту ветку исходного CMake. Архитектурные SIMD-плагины также
отключены в базовых профилях.

Не удаляйте это поведение, если бинарник должен работать на нескольких машинах.

## 7. OpenCL и ZIQ

OpenCL:

```bash
bash scripts/astra/build.sh \
  --profile desktop \
  --with-opencl
```

Требуется работоспособный OpenCL ICD и драйвер устройства.

ZIQ:

```bash
bash scripts/astra/build.sh \
  --profile headless \
  --with-ziq
```

Требуется `libzstd-dev`.

## 8. Дополнительные плагины

Параметры CMake передаются после `--`:

```bash
bash scripts/astra/build.sh \
  --profile headless \
  -- \
  -DPLUGIN_FY3=ON \
  -DPLUGIN_EOS=ON
```

Полный список находится в `plugins/CMakeLists.txt`.

## 9. Установка

Пользовательская:

```bash
bash scripts/astra/build.sh \
  --profile headless \
  --prefix "$HOME/.local/opt/satdump-1.2.2" \
  --install
```

Системная:

```bash
bash scripts/astra/build.sh \
  --profile headless \
  --prefix /opt/satdump \
  --install
```

Системная установка потребует прав только на этапе `cmake --install`.

## 10. Ручная сборка

Ручной режим полезен для отладки. Сначала:

```bash
source scripts/astra/common.sh
select_compiler
export CMAKE_BIN="$(find_cmake 3.18.0)"
export CMAKE_PREFIX_PATH="$ASTRA_DEPS_PREFIX:${CMAKE_PREFIX_PATH:-}"
export PKG_CONFIG_PATH="$ASTRA_DEPS_PREFIX/lib/pkgconfig:$ASTRA_DEPS_PREFIX/lib64/pkgconfig:${PKG_CONFIG_PATH:-}"
```

Конфигурация минимального профиля:

```bash
env CI=astra-linux CC="$CC" CXX="$CXX" "$CMAKE_BIN" \
  -S . \
  -B build/manual-astra \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/opt/satdump-1.2.2" \
  -DBUILD_GUI=OFF \
  -DBUILD_TESTING=ON \
  -DBUILD_OPENCL=OFF \
  -DBUILD_ZIQ=OFF \
  -DPLUGINS_ALL=OFF \
  -DPLUGIN_METEOR=ON \
  -DPLUGIN_NOAA_METOP=ON \
  -DPLUGIN_ANALOG=ON \
  -DPLUGIN_STANDARD_CPP_COMPOS=ON \
  -DPLUGIN_RTLSDR_SDR_SUPPORT=OFF \
  -DPLUGIN_PORTAUDIO_SINK=OFF
```

Сборка:

```bash
"$CMAKE_BIN" --build build/manual-astra --parallel "$(nproc)"
```

Установка:

```bash
"$CMAKE_BIN" --install build/manual-astra
```

## 11. Smoke-тест presentation renderer

```bash
LD_LIBRARY_PATH="build/astra-1.7-headless:build/astra-1.7-headless/plugins:${LD_LIBRARY_PATH:-}" \
  build/astra-1.7-headless/satdump-presentation-test \
  resources/fonts/Roboto-Medium.ttf \
  build/astra-1.7-headless/presentation-test-output
```

Ожидаются три изображения:

```text
continuous.png
categorical.png
composite.png
```

## 12. Что сохранять для воспроизводимости

После успешной сборки архивируйте:

```text
CMakeCache.txt
cmake_install.cmake
astra-env.sh
полный журнал configure/build
вывод gcc/g++ --version
вывод cmake --version
вывод cat /etc/astra/build_version
вывод dpkg-query по зависимостям
SHA коммита Git
тестовые PNG
```

SHA текущего коммита:

```bash
git rev-parse HEAD
```

Список CMake-параметров:

```bash
grep -E '^(BUILD_|PLUGIN_|CMAKE_BUILD_TYPE|CMAKE_INSTALL_PREFIX)' \
  build/astra-*/CMakeCache.txt
```

Следующий документ: [Запуск и обработка](RUN.md).
