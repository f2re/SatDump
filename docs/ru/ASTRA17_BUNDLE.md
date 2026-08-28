# SatDump 1.2.2: native/full bundle для Astra Linux 1.7

Текущий release-профиль ветки `release/1.2.2` — `portable-astra17`.
Несмотря на историческое имя `portable`, сборка выполняется **непосредственно в
Astra Linux 1.7**, а не в Debian/Buster chroot.

## Гарантии release-профиля

Сборочный скрипт прекращает работу, если фактическая ОС не определяется как
Astra Linux 1.7 или если `getconf GNU_LIBC_VERSION` не возвращает `2.28`.
`ASTRA_VERSION_OVERRIDE` для release-сборки запрещён.

Готовый пакет содержит:

- SatDump CLI и GUI;
- плагины release-профиля;
- pipelines/resources/fonts/config;
- полный рекурсивный `DT_NEEDED` closure;
- glibc Astra 1.7;
- `ld-linux-x86-64.so.2`;
- NSS-модули, которые загружаются glibc динамически;
- `libstdc++`, `libgcc_s`, OpenMP;
- GUI/OpenGL/GLFW runtime;
- PortAudio/ALSA runtime;
- RTL-SDR/libusb/udev runtime;
- curl/TLS, TIFF, PNG, FFTW, VOLK, jemalloc и транзитивные зависимости.

Launcher использует bundled dynamic loader с `--library-path`, поэтому установка
дополнительных runtime-пакетов SatDump на целевой Astra 1.7 не требуется.

Аппаратные kernel modules/firmware, права USB и конкретный graphics driver
остаются частью самой ОС и аппаратной конфигурации рабочего места.

## Локальная сборка на Astra Linux 1.7

На самой сборочной Astra Linux 1.7:

```bash
bash scripts/astra/build.sh \
  --mode portable-astra17 \
  --profile desktop \
  --prepare-build-env
```

`--prepare-build-env` устанавливает build-зависимости только на сборочную
машину. Они не требуются рабочей станции, куда разворачивается готовый archive.

Результат:

```text
dist/astra17/satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz
dist/astra17/satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz.sha256
```

## Как собирает GitHub Actions

GitHub runner используется только как host для официального контейнера:

```text
registry.astralinux.ru/library/astra/ubi17:1.7.5
```

Внутри него выполняются:

1. подготовка build toolchain;
2. CMake configure/compile;
3. smoke-test presentation renderer;
4. чистый `DESTDIR` install;
5. recursive ELF closure;
6. включение glibc/loader/NSS;
7. формирование manifest/checksum/archive.

Это означает, что compiler, headers, linker и системные runtime-библиотеки,
попадающие в release, принадлежат Astra Linux 1.7 build environment.

## Zero-install runtime gate

После сборки GitHub Actions скачивает сформированный artifact и разворачивает
его в новом чистом Astra Linux UBI 1.7 контейнере.

В этом runtime job **нет**:

```text
apt-get update
apt-get install
```

Проверяются bundled loader/glibc, запуск `satdump version` и разрешение
зависимостей CLI/GUI через вложенный loader.

Если zero-install gate не прошёл, GitHub Release не создаётся.

## GitHub Releases

Каждый успешный push в `release/1.2.2` после всех gates создаёт immutable release:

```text
v1.2.2-astra17-r<run-number>
```

и помечает его latest.

Release содержит:

```text
satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz
satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz.sha256
ASTRA17-MANIFEST.txt
RUNTIME-LIBRARIES.txt
ASTRA17_COMPLETE_GUIDE.md
```

При повторном запуске того же workflow assets обновляются через `--clobber`, а
новая ревизия получает новый versioned tag.

## Манифест

`ASTRA17-MANIFEST.txt` содержит, в частности:

```text
profile=astra17-native-full
satdump_version=1.2.2
release_revision=...
bundle_profile=desktop
architecture=x86_64
build_os=...
glibc_build=2.28
glibc_bundled=yes
glibc_required=...
glibc_allowed_max=2.28
glibcxx_required=...
runtime_closure=complete
runtime_library_count=...
plugin_count=...
source_commit=...
```

`RUNTIME-LIBRARIES.txt` позволяет проверить, какие реальные `.so` были включены
и из каких путей/пакетов сборочной Astra они взяты.

## RPATH и loader

Абсолютные пути build-host не используются для запуска release. У ELF SatDump
задаются относительные `$ORIGIN` RPATH, а launcher выполняет:

```text
<bundle>/lib/ld-linux-x86-64.so.2
  --library-path <bundle>/lib:<bundle>/lib/satdump/plugins
  <bundle>/bin/satdump
```

Аналогично запускается `satdump-ui`.

## Проверка скачанного release

```bash
sha256sum -c satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz.sha256

tar -xzf satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz
cd satdump-1.2.2-astra17-desktop-full-x86_64

sha256sum -c SHA256SUMS
./satdump version
./satdump-ui
```

Полная эксплуатационная документация:

[ASTRA17_COMPLETE_GUIDE.md](ASTRA17_COMPLETE_GUIDE.md)
