# Проверка сборки в Astra Linux 1.6 и 1.7

Совместимость проверяется на нескольких уровнях. Их нельзя объединять в одно утверждение: успешная компиляция на Ubuntu с выбранной конфигурацией не равна нативной проверке на конкретном обновлении Astra Linux.

## 1. Матрица native-совместимости

GitHub Actions дважды запускает native-профиль с:

```text
ASTRA_VERSION_OVERRIDE=1.6
ASTRA_VERSION_OVERRIDE=1.7
```

Для каждого варианта выполняются:

1. dry-run `install-deps.sh`;
2. выбор C++17-компилятора;
3. конфигурация headless-профиля;
4. полная компиляция SatDump и presentation renderer;
5. `cmake --install` в отдельный чистый prefix;
6. проверка resources, pipelines и `satdump_cfg.json`;
7. запуск установленного бинарника через `scripts/astra/run.sh`;
8. создание семи тестовых PNG;
9. формирование манифеста воспроизводимости.

Команды CI эквивалентны:

```bash
bash scripts/astra/install-deps.sh \
  --profile headless \
  --no-update \
  --dry-run

bash scripts/astra/build.sh \
  --mode native \
  --profile headless \
  --build-dir <build-dir> \
  --prefix <install-prefix> \
  --install

bash scripts/astra/run.sh \
  --prefix <install-prefix> \
  -- version
```

Матрица работает на Ubuntu runner. Она подтверждает корректность сценариев, CMake-профиля, кода, установки и launcher, но не ABI и набор пакетов конкретного frozen-обновления Astra.

## 2. Точный portable baseline glibc 2.24

Отдельный job выполняет реальную сборку внутри изолированного Debian Stretch:

```text
Debian Stretch
amd64
glibc 2.24
GCC 9.5.0
CMake 3.27.9
NNG 1.8.0
```

Проверяются:

- чистота исходного дерева 1.2.2;
- контрольные суммы toolchain;
- чистый `DESTDIR` staging;
- отсутствие чужих плагинов 2.x;
- все ELF-зависимости через `ldd`;
- требуемые версии GLIBC и GLIBCXX;
- запуск SatDump 1.2.2;
- загрузка плагинов без `undefined symbol`;
- renderer и его тестовые изображения;
- переносимый tar.gz и SHA-256.

Этот уровень подтверждает переносимый CLI baseline, но не Fly, USB, OpenGL и локальные SDR-драйверы.

## 3. Официальный Astra Linux UBI 1.7

Workflow `SatDump 1.2.2 Presentation CI` имеет ручной параметр `run_astra_ubi17`. При включении выполняются установка зависимостей, native build, install и запуск в официальном контейнере:

```text
registry.astralinux.ru/library/astra/ubi17:1.7.5
```

Запуск:

1. Откройте GitHub Actions.
2. Выберите `SatDump 1.2.2 Presentation CI`.
3. Нажмите `Run workflow`.
4. Включите `Дополнительно проверить официальный Astra Linux UBI 1.7`.

Если реестр требует авторизацию, настройте разрешённый доступ или внутреннее зеркало. Контейнер не проверяет Fly, USB/udev и политики уровня защищённости.

## 4. Нативная Astra Linux SE

Перед эксплуатационным тегом требуется сборка на реальных системах:

- Astra Linux SE 1.6 с фактически используемым оперативным обновлением;
- Astra Linux SE 1.7 с фактически используемым оперативным обновлением;
- архитектура и уровень защищённости, соответствующие рабочему стенду.

### 4.1 Headless

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

### 4.2 Desktop и RTL-SDR

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

На реальной станции дополнительно проверяются:

- Fly и OpenGL;
- USB/udev;
- доступ пользователя к SDR;
- PortAudio;
- мандатный контроль;
- утверждённые frozen-репозитории;
- запуск после перезагрузки и из сервисной учётной записи.

## 5. Проверка установленного дерева

Для выбранного prefix должны существовать:

```text
<prefix>/bin/satdump
<prefix>/share/satdump/resources/
<prefix>/share/satdump/pipelines/
<prefix>/share/satdump/satdump_cfg.json
<prefix>/lib/libsatdump_core.so
<prefix>/lib/satdump/plugins/
```

Проверка launcher:

```bash
bash scripts/astra/run.sh \
  --prefix <prefix> \
  -- version
```

В журнале не должно быть:

```text
undefined symbol
cannot open shared object file
not found
```

## 6. Тестовые изображения

После native-сборки должны существовать:

```text
continuous_editorial_landscape.png
continuous_minimal_landscape.png
categorical_editorial_landscape.png
composite_editorial_landscape.png
composite_minimal_landscape.png
continuous_editorial_portrait.png
continuous_minimal_portrait.png
```

Каталог:

```text
build/astra-<версия>-<профиль>/presentation-test-output/
```

Проверьте визуально:

- отсутствие наложений текста;
- сохранение ширины исходного растра;
- читаемость кириллицы;
- правильные подписи R/G/B;
- совпадение цветов шкалы с продуктом;
- адаптивную компоновку вертикального кадра;
- отсутствие лишнего поворота географической проекции.

## 7. Реальный пролёт

Минимальный приёмочный набор:

1. нисходящий Meteor-M LRPT;
2. восходящий Meteor-M LRPT;
3. NOAA APT или AVHRR;
4. температурный продукт с непрерывной шкалой;
5. RGB-композит с разностями каналов;
6. категориальный продукт типа облачности.

Для каждого случая сравниваются:

- исходный продукт;
- Minimal PNG;
- Presentation PNG;
- JSON-паспорта;
- северная и южная части по береговым линиям или координатной сетке;
- время пролёта и аппарат;
- спектральный диапазон и частота приёма как разные поля;
- соответствие цветов легенды реальной LUT.

## 8. Диагностический манифест

```bash
bash scripts/astra/collect-build-info.sh \
  --build-dir build/astra-1.7-headless \
  --output build/astra-1.7-headless/astra-build-manifest.txt
```

К отчёту прикладываются:

- `astra-build-manifest.txt`;
- `CMakeCache.txt`;
- журнал build/install/run;
- семь тестовых PNG;
- один реальный обработанный пролёт;
- точное содержимое `/etc/astra/build_version`.

## 9. Критерий выпуска

Новый тег создаётся только после того, как:

- матрица native build/install/run зелёная для 1.6 и 1.7;
- точный glibc-2.24 portable job прошёл;
- UBI 1.7 прошёл либо задокументирована причина недоступности реестра;
- native-сборка выполнена хотя бы на одной реальной 1.6 и одной реальной 1.7;
- восходящий и нисходящий пролёты визуально проверены;
- исходные геопривязанные файлы не испорчены;
- в эксплуатационном журнале нет ABI-ошибок плагинов.
