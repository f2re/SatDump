# SatDump 1.2.2: переносимый бандл для Astra Linux 1.7

Для Astra Linux Special Edition 1.7 основной переносимый профиль — `portable-astra17`.
Он собирается не на Ubuntu runner и не на текущей машине разработчика, а внутри
изолированного Debian 10 Buster rootfs с glibc 2.28. Это соответствует базовой
ABI-линии Astra Linux 1.7 и не позволяет случайно получить зависимость от более
новых `GLIBC_x.y`.

## Почему предыдущий бандл мог падать

Старый `create-offline-bundle.sh` собирал `.so` по выводу `ldd`, копировал их через
`cp -L` и не нормализовал RPATH/RUNPATH. В результате терялись исходные SONAME-
ссылки, а ELF-файлы могли сохранять абсолютные ссылки на build/deps-каталоги.
Кроме того, сама сборка не гарантировала целевую glibc Astra 1.7.

Новый профиль делает четыре проверки до создания архива:

1. Сборка разрешена только если внутри chroot обнаружена glibc 2.28.
2. Все non-glibc `DT_NEEDED` зависимости рекурсивно копируются в `lib/` с
   сохранением реального файла, SONAME и symlink-имени, которое ожидает ELF.
3. RPATH/RUNPATH каждого бинарника и плагина переписывается на относительный
   `$ORIGIN`; абсолютные `/build`, `/home`, `/opt` и `/usr/local` запрещены.
4. Максимальная требуемая версия `GLIBC_x.y` вычисляется через symbol versions и
   сборка падает, если требуется версия выше 2.28. После этого выполняются `ldd`
   closure-check и CLI smoke-test из пустого каталога.

Системные `libc.so.6`, `ld-linux-x86-64.so.2`, NSS и связанные библиотеки glibc
намеренно не кладутся в архив. Их должен предоставлять сам Astra Linux. Подмена
системного loader/glibc внутри приложения создаёт более опасные несовместимости,
особенно для NSS, DNS, PAM и драйверных библиотек. Все остальные найденные
runtime-библиотеки, включая `libstdc++`, `libgcc_s`, OpenMP, GLFW, PortAudio,
RTL-SDR, curl, TIFF, PNG, FFTW, VOLK, jemalloc и их транзитивные зависимости,
попадают в runtime closure, если они реально нужны собранным ELF-файлам.

## Сборка desktop-бандла

На Debian/Ubuntu build-host нужны `debootstrap`, `debian-archive-keyring`, `rsync`
и `util-linux`. Затем:

```bash
bash scripts/astra/build.sh \
  --mode portable-astra17 \
  --profile desktop \
  --clean-rootfs
```

Готовый архив:

```text
dist/astra17/satdump-1.2.2-astra17-desktop-glibc228-x86_64.tar.gz
```

Для CLI без GUI/аудио/RTL-SDR:

```bash
bash scripts/astra/build.sh --mode portable-astra17 --profile headless
```

## Проверка на целевой Astra Linux 1.7

```bash
tar -xzf satdump-1.2.2-astra17-desktop-glibc228-x86_64.tar.gz
bash scripts/astra/astra17/validate-bundle.sh \
  satdump-1.2.2-astra17-desktop-glibc228-x86_64
```

Либо из самого каталога бандла можно сразу запустить:

```bash
./satdump version
./satdump-ui
```

Для установки в `/opt` от root или в `~/.local/opt` от обычного пользователя:

```bash
./install.sh
```

`ASTRA17-MANIFEST.txt` содержит фактические версии glibc/GLIBCXX, commit исходного
дерева и перечень упакованных библиотек. `SHA256SUMS` проверяет содержимое бандла,
а соседний `.tar.gz.sha256` — сам архив.
