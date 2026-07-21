# SatDump 1.2.2 → портируемый бандл под glibc 2.24 (Astra)

Инструкция агенту: как собрано то, что лежит в `SatDump/`, и как это воспроизвести.

Выгружено из WSL (`Ubuntu`) 2026-07-21. Всё, что помечено **[проверено]**, прочитано
из артефактов сборки (CMakeCache, логи, dpkg, ELF). Помеченное **[восстановлено]** —
реконструкция: сам вызов в истории не сохранился, но значения взяты из кэша/логов.

---

## 0. Главное, что нужно знать сразу

1. **Это НЕ форк Owl.** Здесь upstream `SatDump/SatDump`, тег **1.2.2**, дерево чистое.
   mappi на станции линкуется с другим деревом — `~/SRC/SatDump/Owl/SatDump`
   (см. `source/mappi/thematic/app_calc/app.pro`, `msugs_fulldisk.py`). Эти две сборки
   между собой никак не связаны; не подменять одну другой. **[проверено]**
2. **«Кросс-компиляция» здесь — по glibc, не по архитектуре.** x86_64 → x86_64.
   Смысл chroot'а: слинковаться с glibc 2.24, чтобы бинарь запускался на целевой
   машине со старой libc. **[проверено]**
3. **В готовом бандле есть дефект** — 11 чужих плагинов от предыдущей сборки 2.x,
   которые не грузятся (`undefined symbol`). См. §7. При пересборке чинится одной
   строкой (чистый prefix).

---

## 1. Происхождение исходников **[проверено]**

| | |
|---|---|
| upstream | `https://github.com/SatDump/SatDump.git` |
| тег | `1.2.2` |
| commit | `7aef0fe8441bc3eb440b1b6ba053556da5e40991` (2024-11-29) |
| состояние дерева | чистое, ровно на теге (git status пуст) |
| откуда выгружено | `WSL Ubuntu:/home/yurii/build_src/SatDump` |

При выгрузке **исключены**: `.git` (168 МБ), `build/` (артефакты), `docs/res/`
(75 МБ нетрекаемых картинок, к сборке отношения не имеют), `compile_commands.json`
(остался от предыдущей сборки 2.x, вводит в заблуждение).
Итог: 3430 файлов, 81 МБ.

Восстановить git-историю при необходимости:

```bash
git clone https://github.com/SatDump/SatDump.git
git -C SatDump checkout 7aef0fe8441bc3eb440b1b6ba053556da5e40991   # == тег 1.2.2
```

Подмодуль `android/deps` не инициализирован и для Linux не нужен.

---

## 2. Стенд, как он был **[проверено]**

| Компонент | Значение |
|---|---|
| Хост | WSL2 Ubuntu (`DESKTOP-5HVUJN9`), ядро 6.18 |
| chroot | `/srv/stretch` — Debian **9.13 stretch**, **glibc 2.24** |
| apt-источники | `archive.debian.org/debian stretch` + `stretch-backports`, оба `[trusted=yes]` |
| bind-mount | хост `~/build_src` ↔ chroot `/build` |
| Компилятор | GCC **9.5.0**, собран из исходников в `/opt/gcc9` (штатный stretch-GCC 6.3 не тянет C++17) |
| CMake | **3.27.9**, распакованный тарбол, вызывался как `/build/cmake-3.27.9-linux-x86_64/bin/cmake` |
| make | системный `/usr/bin/make` |
| nng | **1.8.0**, собран из исходников в `/usr/local` (собирался ещё системным gcc 6.3) |
| prefix установки | `/opt/satdump` |
| итоговый артефакт | `~/build_src/satdump-astra/` (174 МБ) и `satdump-astra-glibc224.tar.gz` (98 МБ) |

---

## 3. Воспроизведение с нуля

### 3.1 Создать chroot **[восстановлено]**

```bash
sudo debootstrap --arch=amd64 --no-check-gpg stretch /srv/stretch \
     http://archive.debian.org/debian
```

`/srv/stretch/etc/apt/sources.list` **[проверено]**:

```
deb [trusted=yes] http://archive.debian.org/debian stretch main contrib
deb [trusted=yes] http://archive.debian.org/debian stretch-backports main
```

Вход в chroot **[восстановлено]** (schroot не настроен, использовался обычный chroot):

```bash
sudo mount --bind /proc /srv/stretch/proc
sudo mount --bind /sys  /srv/stretch/sys
sudo mount --bind /dev  /srv/stretch/dev
sudo mkdir -p /srv/stretch/build
sudo mount --bind ~/build_src /srv/stretch/build     # ключевой момент: /build == ~/build_src
sudo chroot /srv/stretch /bin/bash
```

### 3.2 Пакеты в chroot **[проверено — список сверен с dpkg]**

```bash
apt-get update
# для сборки GCC 9.5:
apt-get install -y build-essential flex bison libgmp-dev libmpfr-dev libmpc-dev libisl-dev
# для сборки SatDump:
apt-get install -y pkg-config ca-certificates zlib1g-dev \
    libfftw3-dev libpng-dev libtiff5-dev libjemalloc-dev libcurl4-openssl-dev \
    libvolk1-dev libzstd-dev libhdf5-dev libsqlite3-dev libomp-dev
```

Чего в chroot **нет** и что поэтому не собралось: `libnng-dev` (нет в stretch → из
исходников, §3.4), OpenCL, glfw/GUI, portaudio, armadillo и все SDR-библиотеки
(rtlsdr/hackrf/airspy/bladerf/limesuite/uhd/…). Полный список пакетов —
`build-reference/chroot-dpkg-list.txt`.

### 3.3 GCC 9.5.0 **[проверено — строка взята из `gcc -v` собранного компилятора]**

Исходники распаковываются в `/build/gcc-9.5.0`, сборка вне дерева:

```bash
cd /build/gcc-build
/build/gcc-9.5.0/configure --prefix=/opt/gcc9 --enable-languages=c,c++ \
    --disable-multilib --disable-bootstrap --disable-libsanitizer --disable-werror
make -j"$(nproc)" && make install
```

`--disable-bootstrap` — сознательный размен: сборка в разы быстрее, но GCC 9
собирается старым GCC 6. Для этой задачи приемлемо.

### 3.4 nng 1.8.0 **[проверено]**

```bash
cd /build/nng/build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DNNG_TESTS=OFF
make -j"$(nproc)" && make install     # → /usr/local/lib/libnng.so.1.8.0
```

Собиралось системным gcc 6.3 (`/usr/bin/cc`) — это видно в `logs/nng_cfg.log`
и это нормально: C-библиотека, ABI-конфликта с gcc9 нет.

### 3.5 CMake 3.27.9

Распакованный официальный тарбол в `/build/cmake-3.27.9-linux-x86_64/`.
Штатный cmake stretch (3.7) не годится — SatDump требует новее.

### 3.6 Сборка SatDump **[значения проверены по CMakeCache; сам вызов восстановлен]**

```bash
export PATH=/build/cmake-3.27.9-linux-x86_64/bin:$PATH
export LD_LIBRARY_PATH=/opt/gcc9/lib64:$LD_LIBRARY_PATH

rm -rf /opt/satdump                 # ВАЖНО, см. §7
cd /build/SatDump && rm -rf build && mkdir build && cd build

cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/opt/satdump \
  -DCMAKE_PREFIX_PATH=/usr/local \
  -DCMAKE_C_COMPILER=/opt/gcc9/bin/gcc \
  -DCMAKE_CXX_COMPILER=/opt/gcc9/bin/g++ \
  -DCMAKE_C_FLAGS="-include stdint.h" \
  -DCMAKE_CXX_FLAGS="-include cstdint" \
  -DBUILD_GUI=OFF \
  -DBUILD_TOOLS=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_DOCS=OFF \
  -DBUILD_ZIQ=OFF \
  -DBUILD_ZIQ2=OFF \
  -DBUILD_OPENCL=OFF \
  -DBUILD_OPENMP=ON \
  -DENABLE_INSTALL=ON

make -j"$(nproc)"
make install
```

Пояснения:

* `-include stdint.h` / `-include cstdint` — обход отсутствующих транзитивных
  включений при этой связке компилятора и заголовков. Флаги реально были
  применены **[проверено, CMakeCache]**.
* `-DCMAKE_PREFIX_PATH=/usr/local` — чтобы нашёлся собранный вручную nng.
* `BUILD_ZIQ=OFF` — при этом `libzstd-dev` в chroot всё равно стоит (нашёлся,
  но не использован).
* Плагины **не** перечисляются: в 1.2.2 действует `PLUGINS_ALL=ON`, собирается
  всё, что нашло свои зависимости. Собралось **46 плагинов**. SDR-плагины
  отвалились сами («Airspy Library could not be found! Not building.» и т.д.) —
  это ожидаемо, live-приём в этой сборке не поддерживается, только файловые и
  сетевые источники.
* `-march=native` **не применялся** — в 1.2.2 такого блока в CMakeLists нет,
  бандл переносим между CPU. (Сообщение «Building for native architecture» в
  логах `satdump_cfg*.log` относится к более ранней попытке на ветке 2.x.)
* При install CMake обнуляет RPATH («Set runtime path … to ""») — отсюда
  необходимость wrapper-скрипта с `LD_LIBRARY_PATH` (§4).

### 3.7 Сборка бандла

`build-scripts/make_bundle.sh` (запускать в chroot, оригинал без изменений):

```bash
bash /build/make_bundle.sh      # SD=/opt/satdump → OUT=/build/satdump-astra
```

Что он делает: копирует `satdump`, `libsatdump_core.so`, плагины и `share/satdump`;
затем в 4 прохода рекурсивно собирает через `ldd` все нужные `.so`, **исключая
семейство glibc** (`ld-linux*, libc, libm, libpthread, libdl, librt, libresolv,
libnsl, libnss_*, libutil, libcrypt, libanl, libBrokenLocale`) — их берёт целевая
система. Отдельно кладёт `libstdc++.so.6`, `libgcc_s.so.1`, `libgomp.so.1`
из `/opt/gcc9/lib64`. Пишет wrapper `satdump` и проверяет `ldd`-разрешимость.

Результат: 50 библиотек, 174 МБ (из них 105 МБ — `share/satdump/resources`).

---

## 4. Установка на целевой машине

Бандл самодостаточен, ставится распаковкой куда угодно. Запуск — только через
wrapper (он выставляет `LD_LIBRARY_PATH`, потому что RPATH обнулён):

```bash
tar xzf satdump-astra-glibc224.tar.gz
./satdump-astra/satdump <аргументы>
```

Требования к целевой системе **[проверено по ELF]**: glibc **≥ 2.22**
(максимальная затребованная версия символа по всем объектам бандла — `GLIBC_2.22`;
у самого `bin/satdump` и `libsatdump_core.so` — `GLIBC_2.14`). Имя артефакта
(`glibc224`) отражает окружение сборки, а не нижнюю границу.

`libstdc++` в бандле даёт `GLIBCXX_3.4.28`.

---

## 5. Проверка сборки

1. **Разрешимость линковки** — уже встроена в `make_bundle.sh` (ожидается
   «OK: бинарь разрешён» и «OK: плагины разрешены»).
2. **Баннер версии**: в логе должно быть `Starting SatDump v1.2.2`.
3. **Загрузка плагинов** — запустить с трассировкой и убедиться, что нет строк
   `Error loading … undefined symbol`. На текущем бандле такие строки ЕСТЬ, см. §7.
4. **Оффлайн-прогон** — эталон уже есть: `logs/cleantest.log`. Прогонялся
   Meteor-датасет (`~/build_src/test_meteor.dat`, 62 МБ), на выходе продукты
   `MSU-MR` и `MTVZA` + RGB-композиты, финал — `Done! Goodbye`.
5. **Нижняя граница glibc**:
   ```bash
   find . -type f \( -name '*.so*' -o -name satdump \) -exec objdump -T {} \; \
     | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1     # ожидается GLIBC_2.22
   ```

---

## 6. Что лежит рядом

```
source/satdump/
├── SatDump/                       исходники (upstream 1.2.2, чистое дерево)
├── BUILD_AGENT.md                 этот файл
├── build-scripts/
│   └── make_bundle.sh             сборщик переносимого бандла (оригинал)
└── build-reference/
    ├── PROVENANCE.txt             upstream/тег/commit/дата выгрузки
    ├── CMakeCache.txt             кэш реальной сборки  ⚠ загрязнён, см. §7
    ├── install_manifest.txt       что именно поставилось в /opt/satdump
    ├── chroot-dpkg-list.txt       все пакеты chroot с версиями
    ├── chroot-sources.list        apt-источники chroot
    └── logs/
        ├── sd122_cfg.log          configure сборки 1.2.2
        ├── sd122_make.log         make + install (хвост: INSTALL_EXIT=0, ALL_DONE)
        ├── nng_cfg.log            configure nng 1.8.0
        └── cleantest.log          эталонный оффлайн-прогон бандла
```

Осталось в WSL и сюда **не** копировалось: сам бандл
`~/build_src/satdump-astra/` + `satdump-astra-glibc224.tar.gz` (98 МБ),
chroot `/srv/stretch`, тестовый поток `test_meteor.dat`, исходники GCC 9.5 и nng.

---

## 7. Известные дефекты и ловушки

### 7.1 Бандл содержит 11 чужих плагинов от сборки 2.x — **не грузятся**

Prefix `/opt/satdump` **не очищался** между двумя сборками. Сначала (02:46–03:01)
собиралась ветка 2.x, потом дерево переключили на тег 1.2.2 и собрали заново
(03:18–03:26) — install перезаписал только свои 46 плагинов, а 11 чужих остались
и попали в бандл. **[проверено: mtime + отсутствие каталогов в дереве 1.2.2 + ошибки в `cleantest.log`]**

Лишние (у них нет каталогов в исходниках 1.2.2, кроме `bitview_app` — тот только для GUI):

```
libaaronia_sdr_support.so   libearthcare_support.so   libinsat_support.so
libbitview_app.so           libexperimental_devices_support.so
libkanopus_support.so       libmetopsg_support.so     libradiosonde_support.so
libseawifs_support.so       libuvsq_support.so        libxrit_support.so
```

Симптом в логе:

```
(E) Error loading …/libxrit_support.so! Error : … undefined symbol: _ZN7satdump11satdump_cfgE
(E) Error loading …/libseawifs_support.so! … undefined symbol: _ZTIN7satdump8pipeline4base28FileStreamToFileStreamModuleE
```

Фатальным это не оказалось — SatDump просто пропускает такой плагин и работает
дальше (прогон `cleantest` прошёл до конца). Но это мусор, и он маскирует
настоящие ошибки загрузки.

**Как чинить:** `rm -rf /opt/satdump` перед `make install` (уже вписано в §3.6),
либо удалить эти 11 файлов из `lib/satdump/plugins/` в готовом бандле.
Корректное число плагинов для 1.2.2 — **46**.

### 7.2 `build-reference/CMakeCache.txt` загрязнён

Каталог `build/` переиспользовался между 2.x и 1.2.2, поэтому в кэше осели
переменные от 2.x. В первую очередь — вся россыпь `PLUGIN_*:BOOL=OFF`
(`PLUGIN_METEOR=OFF`, `PLUGIN_ELEKTRO_ARKTIKA=OFF`, …). **Они не действовали**:
в 1.2.2 условие `if(PLUGIN_X OR PLUGINS_ALL)`, а `PLUGINS_ALL=ON` по умолчанию,
и соответствующие `.so` реально собрались. Читая кэш, не принимать эти строки
за конфигурацию сборки. Достоверны: компиляторы, флаги, prefix, `BUILD_*`.

### 7.3 Сборка 2.x из этого дерева невоспроизводима

`git checkout 1.2.2` затёр рабочее дерево ветки 2.x. Если понадобятся те плагины
(`xrit`, `metopsg`, `kanopus`, `earthcare`, …) — их надо собирать отдельно из
master/2.x, и тогда это отдельный бандл: с ядром 1.2.2 они по ABI несовместимы.

### 7.4 Live-приём в этой сборке отсутствует

Ни одной SDR-библиотеки в chroot нет, все `*_sdr_support` плагины пропущены на
configure. Доступны только файловые источники и сетевые (`net_source`,
`rtltcp`, `spyserver`, `sdrpp_server`, `remote_sdr` — они собраны, но требуют
внешнего сервера). Если нужен приём с железа — доставить в chroot
соответствующие `-dev` пакеты (в stretch их частично нет, придётся из исходников)
и пересобрать.

### 7.5 Прочее

* GUI не собран (`BUILD_GUI=OFF`) — CLI-only.
* OpenCL выключен: заметно медленнее reprojection/композиты.
* Плагин AVX2 собран, но на старте сам проверяет CPU и отключается, если
  инструкций нет (`CPU Does not support AVX2. Extension plugin NOT loading!`).
  Это штатное поведение, не ошибка.
* `libsatdump_core.so` — 11 МБ; `share/satdump/resources` — 105 МБ, основная
  масса бандла. Урезать можно только осознанно: там карты и таблицы, нужные
  для географической привязки.
