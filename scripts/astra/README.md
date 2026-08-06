# 🛡️ Сборка SatDump 1.2.2 в Astra Linux

Каталог содержит два независимых способа сборки для Astra Linux Special Edition
1.6 и 1.7.

```text
native
  └─ собирается непосредственно на целевой Astra

portable-glibc224
  └─ собирается в изолированном Debian Stretch chroot
     и выпускается как самодостаточный CLI-бандл
```

Сценарии не подключают Debian/Ubuntu-репозитории к хостовой Astra Linux и не
заменяют её системный компилятор или CMake.

Запускать можно через `bash`; executable bit не обязателен:

```bash
bash scripts/astra/build.sh --help
```

## 🚦 Выбор режима

### Native — установка на эту же машину

```bash
bash scripts/astra/check-system.sh
bash scripts/astra/install-deps.sh --profile headless --bootstrap-missing
bash scripts/astra/build.sh --mode native --profile headless --install
bash scripts/astra/run.sh -- version
```

`native` используется по умолчанию, поэтому сохранена совместимость:

```bash
bash scripts/astra/build.sh --profile headless --install
```

### Portable — один бандл для нескольких машин

```bash
bash scripts/astra/build.sh \
  --mode portable-glibc224 \
  --profile reference
```

Результат:

```text
dist/astra-portable/
├── satdump-1.2.2-presentation-reference-glibc224-x86_64/
├── satdump-1.2.2-presentation-reference-glibc224-x86_64.tar.gz
└── satdump-1.2.2-presentation-reference-glibc224-x86_64.tar.gz.sha256
```

Подробно: [`docs/ru/PORTABLE_ASTRA.md`](../../docs/ru/PORTABLE_ASTRA.md).

## 📁 Состав каталога

| Файл | Назначение |
|---|---|
| `check-system.sh` | Диагностика версии ОС, C++17, CMake и обязательных библиотек |
| `install-deps.sh` | Установка пакетов из уже подключённых Astra-репозиториев |
| `bootstrap-cmake.sh` | Локальная сборка CMake 3.18.6 для native-профиля |
| `bootstrap-thirdparty.sh` | Локальная сборка NNG, VOLK, FFTW3f, cURL, TIFF, jemalloc |
| `build.sh` | Безопасный диспетчер `native` / `portable-glibc224` |
| `build-native.sh` | Нативные профили headless/desktop/full |
| `create-offline-bundle.sh` | Сборка переносимого оффлайн-бандла (.tar.gz) со всеми .so и встроенным `install.sh` |
| `run-build-and-bundle.sh` | Скрипт-обёртка для компиляции SatDump и генерации оффлайн-бандла в `dist/` |
| `../build-and-bundle.sh` | Корневая команда единого цикла сборки и упаковывания оффлайн-пакета |
| `run.sh` | Проверка установленного дерева и запуск CLI/GUI |
| `collect-build-info.sh` | Манифест воспроизводимости native-сборки |
| `portable/lock.env` | Зафиксированные версии portable toolchain |
| `portable/prepare-rootfs.sh` | Подготовка Debian Stretch chroot |
| `portable/build.sh` | Host orchestrator portable-сборки |
| `portable/inside-chroot.sh` | Сборка GCC, NNG и SatDump внутри chroot |
| `portable/make-bundle.sh` | Чистый staging, сбор библиотек и tar.gz |
| `portable/validate-bundle.sh` | Проверка бандла на целевой Astra |
| `repos/*.example` | Примеры Astra-репозиториев без автоматического применения |

## 🧱 Native-профили

### `headless`

- CLI;
- Meteor-M LRPT/HRPT;
- NOAA/MetOp;
- NOAA APT;
- стандартные C++-композиты;
- Minimal/Presentation плашки, легенды и JSON-паспорта;
- без GUI, локальных SDR, OpenCL и аудиовыхода.

### `desktop`

Дополнительно включает:

- GUI;
- OpenGL/GLFW;
- PortAudio;
- RTL-SDR по умолчанию.

```bash
bash scripts/astra/install-deps.sh --profile desktop --bootstrap-missing
bash scripts/astra/build.sh --mode native --profile desktop --sdr rtl --install
bash scripts/astra/run.sh --ui
```

### `full`

Включает расширенный набор протокольных и аппаратных плагинов. Этот профиль
собирается под конкретную рабочую станцию и не является переносимым baseline.

## 📦 Portable-профили плагинов

### `reference`

Повторяет рабочий принцип ветки `astra`:

```cmake
PLUGINS_ALL=ON
```

Собирается всё из SatDump 1.2.2, для чего есть зависимости в Stretch. Эталонный
профиль CLI-only, без локальных SDR и OpenCL.

### `meteor`

Минимальный контролируемый набор:

- Meteor-M;
- NOAA/MetOp;
- NOAA APT;
- standard C++ composites;
- presentation renderer.

```bash
bash scripts/astra/build.sh \
  --mode portable-glibc224 \
  --profile meteor
```

## 🔒 Почему portable-сборка безопаснее прямого копирования

Она фиксирует:

```text
Debian Stretch / glibc 2.24
GCC 9.5.0
CMake 3.27.9
NNG 1.8.0
SatDump 1.2.2
```

И выполняет обязательные проверки:

- исходное дерево не содержит маркеров SatDump 2.x;
- GCC и CMake проверяются SHA-256;
- NNG checkout соответствует зафиксированному commit;
- `cmake --install` выполняется в новый пустой `DESTDIR`;
- известные чужие плагины 2.x запрещены;
- все зависимости разрешаются через `ldd`;
- требуемая `GLIBC_*` не выше baseline;
- SatDump возвращает версию 1.2.2;
- плагины не выдают `undefined symbol`;
- renderer проходит smoke-тесты.

## 🧼 Почему обязателен чистый staging

Старая рабочая сборка Astra показала типичную проблему: если `/opt/satdump` не
очистить после другой версии, старые `.so` могут попасть в новый бандл.

Portable-профиль не читает постоянный install prefix. Он устанавливает только в:

```text
build/portable-glibc224/stage-<profile>/opt/satdump
```

Этот каталог удаляется и создаётся заново перед упаковкой.

## 🌐 Репозитории

Native-сценарии не записывают `/etc/apt/sources.list`. Примеры:

- `repos/astra-1.6.list.example`;
- `repos/astra-1.7.list.example`.

Portable-сценарий создаёт отдельный Debian chroot. Debian-пакеты не устанавливаются
в Astra Linux. Для закрытого контура используйте внутреннее зеркало через
`--mirror`.

## 📴 Offline portable-сборка

Подготовьте:

```text
approved-sources/
├── gcc-9.5.0.tar.xz
├── cmake-3.27.9-linux-x86_64.tar.gz
├── cmake-3.27.9-SHA-256.txt
└── nng-v1.8.0.bundle
```

```bash
bash scripts/astra/build.sh \
  --mode portable-glibc224 \
  --profile reference \
  --offline-dir /mnt/approved/approved-sources \
  --mirror http://mirror.internal/debian-archive
```

## 🧪 Проверка бандла

На целевой Astra:

```bash
bash scripts/astra/portable/validate-bundle.sh \
  dist/astra-portable/satdump-1.2.2-presentation-reference-glibc224-x86_64
```

Проверяются системная glibc, ELF-зависимости, запуск версии, resources, pipelines
и ABI плагинов.

## 🛰️ Реальный прогон

```bash
./satdump-1.2.2-presentation-reference-glibc224-x86_64/satdump \
  meteor_m2x_lrpt \
  baseband \
  /data/input/meteor.cs16 \
  /data/output/meteor-pass \
  --samplerate 240000 \
  --baseband_format cs16
```

После обработки проверьте:

- научный исходный продукт;
- Minimal PNG;
- Presentation PNG;
- JSON-паспорта;
- направление «север сверху»;
- соответствие легенды LUT.

## ⚠️ Ограничения

Portable baseline не заменяет native desktop-проверку. В нём не подтверждаются:

- Fly/GUI;
- OpenGL;
- локальный RTL-SDR/Airspy/HackRF;
- USB/udev;
- OpenCL;
- политики конкретного защищённого контура.

Для этого используйте `--mode native --profile desktop` на фактической рабочей
станции.

Полная документация:

- [`docs/ru/PORTABLE_ASTRA.md`](../../docs/ru/PORTABLE_ASTRA.md);
- [`docs/ru/INSTALL_ASTRA.md`](../../docs/ru/INSTALL_ASTRA.md);
- [`docs/ru/ASTRA_VALIDATION.md`](../../docs/ru/ASTRA_VALIDATION.md).
