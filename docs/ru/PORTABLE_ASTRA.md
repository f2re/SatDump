# 📦 Переносимая сборка для Astra Linux: glibc 2.24

## 1. Назначение

Режим `portable-glibc224` предназначен для случая, когда SatDump собирается один
раз, а затем развёртывается на нескольких рабочих станциях Astra Linux 1.6/1.7.
Он воспроизводит проверенную технологию из ветки `astra`, но использует чистое
дерево текущей ветки `release/1.2.2` с плашками, легендами и ориентацией снимков.

Результат — самодостаточный CLI-бандл:

```text
satdump-1.2.2-presentation-reference-glibc224-x86_64/
satdump-1.2.2-presentation-reference-glibc224-x86_64.tar.gz
satdump-1.2.2-presentation-reference-glibc224-x86_64.tar.gz.sha256
```

Бандл содержит:

- `satdump` и `libsatdump_core.so`;
- плагины выбранного профиля;
- pipelines и resources;
- non-glibc runtime-библиотеки;
- `libstdc++.so.6`, `libgcc_s.so.1`, `libgomp.so.1`;
- relocatable wrapper;
- `PORTABLE-MANIFEST.txt`;
- контрольные суммы всех файлов;
- журналы проверки версии и плагинов.

## 2. Почему это отдельный режим

Нативная сборка:

```text
собрать на Astra → установить на эту же Astra
```

Переносимая сборка:

```text
чистый SatDump 1.2.2
  → изолированный Debian Stretch chroot
  → glibc 2.24
  → GCC 9.5.0
  → CMake 3.27.9
  → NNG 1.8.0
  → чистый staging
  → сбор runtime-зависимостей
  → tar.gz
```

Старый `scripts/astra/build.sh` не удалён. Он стал диспетчером и по умолчанию
выбирает `native`, поэтому существующие команды продолжают работать.

## 3. Выбор режима

### Нативная сборка

```bash
bash scripts/astra/build.sh \
  --mode native \
  --profile headless
```

Краткая совместимая запись:

```bash
bash scripts/astra/build.sh --profile headless
```

### Переносимый бандл

```bash
bash scripts/astra/build.sh \
  --mode portable-glibc224 \
  --profile reference
```

Режим можно задать переменной окружения:

```bash
export SATDUMP_BUILD_MODE=portable-glibc224
bash scripts/astra/build.sh --profile reference
```

## 4. Профили плагинов

### `reference`

Максимально повторяет рабочую сборку ветки `astra`:

```cmake
PLUGINS_ALL=ON
```

Собираются все плагины SatDump 1.2.2, зависимости которых доступны в Debian
Stretch chroot. GUI, локальные SDR-драйверы и OpenCL не входят в эталонный
portable-бандл.

Команда:

```bash
bash scripts/astra/build.sh \
  --mode portable-glibc224 \
  --profile reference
```

### `meteor`

Уменьшенный профиль для основной метеорологической обработки:

- Meteor-M;
- NOAA/MetOp;
- NOAA APT и аналоговые протоколы;
- стандартные C++-композиты;
- presentation renderer и smoke-тесты.

Команда:

```bash
bash scripts/astra/build.sh \
  --mode portable-glibc224 \
  --profile meteor
```

## 5. Подготовка хоста

Portable-профиль выполняется на x86_64 Linux-хосте. Подходит отдельная build-машина,
WSL2 или техническая Astra-система, на которой разрешены `chroot` и bind mounts.

Необходимы:

```bash
sudo apt-get install \
  debootstrap \
  debian-archive-keyring \
  rsync \
  xz-utils \
  binutils
```

Сценарий:

- не подключает Debian-репозитории к Astra Linux;
- не меняет `/etc/apt/sources.list` хоста;
- не заменяет системный GCC или CMake;
- создаёт отдельный rootfs;
- монтирует исходное дерево в chroot только для чтения.

## 6. Первая полная сборка

```bash
bash scripts/astra/build.sh \
  --mode portable-glibc224 \
  --profile reference \
  --rootfs /var/lib/satdump-build/stretch-amd64 \
  --work-dir "$PWD/build/portable-glibc224" \
  --output-dir "$PWD/dist/astra-portable" \
  --cache-dir "$HOME/.cache/satdump-portable" \
  --jobs 2 \
  --clean-rootfs
```

Первая сборка долгая, поскольку внутри rootfs компилируется GCC 9.5.0. Повторная
сборка без `--clean-rootfs` использует уже подготовленный toolchain.

## 7. Зафиксированный toolchain

Версии находятся в:

```text
scripts/astra/portable/lock.env
```

Фиксируются:

| Компонент | Значение |
|---|---|
| базовая ОС | Debian 9 Stretch |
| архитектура | x86_64/amd64 |
| glibc среды | 2.24 |
| GCC | 9.5.0 |
| CMake | 3.27.9 |
| NNG | 1.8.0, точный Git commit |
| SatDump | 1.2.2 |

GCC проверяется SHA-256. CMake проверяется по официальному SHA-256 manifest.
NNG извлекается по зафиксированному commit, а не по плавающей ветке.

## 8. Чистый staging

Установка выполняется так:

```text
DESTDIR=<новый пустой каталог> cmake --install ...
```

Бандл никогда не собирается непосредственно из старого `/opt/satdump`. Это
предотвращает попадание чужих `.so` от другой версии SatDump.

Дополнительно упаковщик блокирует известные плагины 2.x, которые ранее попадали
в рабочий Astra-бандл из загрязнённого install prefix.

## 9. Проверки при упаковке

Сборка завершается ошибкой, если:

- дерево не является SatDump 1.2.2;
- найдены признаки смешивания с SatDump 2.x;
- не разрешается хотя бы одна ELF-зависимость;
- требуемая версия glibc выше заданного baseline;
- `satdump version` не возвращает 1.2.2;
- плагин выдаёт `undefined symbol`;
- плагин не находит shared library;
- количество плагинов явно ниже ожидаемого;
- presentation smoke-test не проходит.

В `PORTABLE-MANIFEST.txt` записываются:

- commit и ветка исходников;
- профиль плагинов;
- версии GCC/CMake/NNG;
- максимальные `GLIBC_*` и `GLIBCXX_*`;
- список библиотек;
- список плагинов;
- количество плагинов.

## 10. Relocatable wrapper

Запускать следует корневой wrapper:

```bash
./satdump-1.2.2-presentation-reference-glibc224-x86_64/satdump version
```

Wrapper выставляет относительные пути:

```text
SATDUMP_RESOURCES_PATH=<bundle>/share/satdump/
SATDUMP_LIBRARIES_PATH=<bundle>/lib/satdump/
LD_LIBRARY_PATH=<bundle>/lib:<bundle>/lib/satdump/plugins
```

Благодаря этому архив можно распаковать в `/opt`, `/srv`, домашний каталог или
другой утверждённый путь без пересборки.

## 11. Проверка на целевой Astra Linux

После переноса архива:

```bash
sha256sum -c \
  satdump-1.2.2-presentation-reference-glibc224-x86_64.tar.gz.sha256

tar -xzf \
  satdump-1.2.2-presentation-reference-glibc224-x86_64.tar.gz

bash scripts/astra/portable/validate-bundle.sh \
  satdump-1.2.2-presentation-reference-glibc224-x86_64
```

Проверяются:

- версия системной glibc;
- все ELF-зависимости;
- запуск SatDump 1.2.2;
- ресурсы и pipelines;
- загрузка плагинов;
- отсутствие `undefined symbol`.

После технической проверки нужен реальный прогон:

```bash
./satdump-1.2.2-presentation-reference-glibc224-x86_64/satdump \
  meteor_m2x_lrpt \
  baseband \
  /data/input/meteor.cs16 \
  /data/output/meteor-pass \
  --samplerate 240000 \
  --baseband_format cs16
```

Параметры должны соответствовать фактической записи.

## 12. Закрытый контур

Для offline-сборки подготовьте каталог:

```text
approved-sources/
├── gcc-9.5.0.tar.xz
├── cmake-3.27.9-linux-x86_64.tar.gz
├── cmake-3.27.9-SHA-256.txt
└── nng-v1.8.0.bundle
```

Git bundle NNG создаётся на разрешённой машине:

```bash
git clone https://github.com/nanomsg/nng.git
cd nng
git bundle create nng-v1.8.0.bundle \
  29b73962b939a6fbbf6ea8d5d7680bb06d0eeb99
```

Сборка:

```bash
bash scripts/astra/build.sh \
  --mode portable-glibc224 \
  --profile reference \
  --offline-dir /mnt/approved/approved-sources
```

APT-пакеты для rootfs должны поступать с утверждённого внутреннего зеркала:

```bash
bash scripts/astra/build.sh \
  --mode portable-glibc224 \
  --profile reference \
  --mirror http://mirror.internal/debian-archive
```

## 13. Проверка подписей архива

По умолчанию debootstrap использует `debian-archive-keyring` и не отключает
проверку подписей.

Legacy-режим:

```bash
--allow-unsigned-archive
```

Он допускается только для:

- изолированного build-chroot;
- утверждённого внутреннего зеркала;
- документированной процедуры организации.

Этот параметр не должен использоваться как обычный способ обхода ошибок APT.

## 14. CI

Workflow:

```text
.github/workflows/astra-portable-glibc224.yml
```

На каждом PR выполняются:

- `bash -n`;
- ShellCheck уровня error;
- проверка lock-файла;
- проверка чистоты дерева 1.2.2;
- проверка dispatcher.

Полный build запускается вручную через GitHub Actions, поскольку компиляция GCC
существенно тяжелее обычного CI. Результатом является готовый tar.gz и manifest.

## 15. Что portable-профиль не решает

Эталонный бандл является CLI-only. Он не подтверждает:

- Fly/GUI;
- OpenGL/GLFW;
- локальный RTL-SDR/Airspy/HackRF;
- USB/udev;
- OpenCL;
- мандатные политики конкретной Astra-системы.

Для рабочей станции с GUI и SDR используйте `--mode native --profile desktop`,
а portable-бандл применяйте для серверной и файловой обработки.

## 16. Критерий выпуска

Тег portable-релиза ставится только после:

1. успешной полной GitHub Actions сборки `portable-glibc224`;
2. проверки бандла на реальной Astra Linux 1.6;
3. проверки бандла на реальной Astra Linux 1.7;
4. реального Meteor-M прогона;
5. визуальной проверки Minimal и Presentation изображений;
6. отсутствия ошибок загрузки плагинов;
7. сохранения `PORTABLE-MANIFEST.txt` и SHA-256 рядом с релизом.
