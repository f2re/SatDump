# Проверка сборки в Astra Linux 1.6 и 1.7

## Уровни проверки

Совместимость проверяется на трёх уровнях. Их нельзя смешивать в одном утверждении.

### 1. Матрица совместимой сборки

GitHub Actions дважды запускает Astra-профиль сборки с:

```text
ASTRA_VERSION_OVERRIDE=1.6
ASTRA_VERSION_OVERRIDE=1.7
```

Проверяются:

- логика выбора компилятора C++17;
- CMake-профиль без `-march=native`;
- набор Meteor/NOAA/APT-плагинов;
- компиляция presentation renderer;
- портретные и альбомные smoke-тесты;
- ориентация север-сверху;
- создание тестовых PNG.

Эта матрица работает на Ubuntu runner и подтверждает переносимый профиль проекта, но не заменяет нативную проверку ABI и пакетов конкретного обновления Astra.

### 2. Официальный Astra Linux UBI 1.7

Workflow имеет ручной параметр `run_astra_ubi17`. При его включении выполняется сборка в официальном контейнере:

```text
registry.astralinux.ru/library/astra/ubi17:1.7.5
```

Запуск:

1. Откройте GitHub Actions.
2. Выберите `SatDump 1.2.2 Presentation CI`.
3. Нажмите `Run workflow`.
4. Включите `Дополнительно проверить официальный Astra Linux UBI 1.7`.

Если реестр требует авторизацию, сначала настройте разрешённый доступ или внутреннее зеркало. В закрытом контуре образ следует импортировать по процедуре организации.

### 3. Нативная Astra Linux SE

Перед выпуском тега требуется сборка на реальных системах:

- Astra Linux SE 1.6 с фактически используемым оперативным обновлением;
- Astra Linux SE 1.7 с фактически используемым оперативным обновлением;
- архитектура и уровень защищённости, совпадающие с рабочим стендом.

Публичного контейнера недостаточно для проверки Fly, USB/udev, OpenGL, драйверов SDR, мандатного контроля и утверждённых frozen-репозиториев.

## Нативная процедура

```bash
cat /etc/astra/build_version
bash scripts/astra/check-system.sh --strict

bash scripts/astra/install-deps.sh \
  --profile headless \
  --bootstrap-missing

bash scripts/astra/build.sh \
  --profile headless \
  --clean
```

Для рабочей станции:

```bash
bash scripts/astra/install-deps.sh \
  --profile desktop \
  --bootstrap-missing

bash scripts/astra/build.sh \
  --profile desktop \
  --sdr rtl \
  --clean
```

## Проверка тестовых изображений

После сборки должны существовать:

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
- совпадение цветов непрерывной шкалы с продуктом;
- аккуратную компоновку вертикального кадра;
- отсутствие лишнего поворота географической проекции.

## Реальный пролёт

Минимальный приёмочный набор:

1. Нисходящий Meteor-M LRPT.
2. Восходящий Meteor-M LRPT.
3. NOAA APT или AVHRR.
4. Температурный продукт с непрерывной шкалой.
5. RGB-композит с разностями каналов.
6. Категориальный продукт типа облачности.

Для каждого случая сравниваются:

- исходный продукт;
- минималистичный PNG;
- презентационный PNG;
- JSON-паспорта;
- северная и южная части по береговым линиям или координатной сетке.

## Сбор диагностического манифеста

```bash
bash scripts/astra/collect-build-info.sh \
  --build-dir build/astra-1.7-headless \
  --output build/astra-1.7-headless/astra-build-manifest.txt
```

К отчёту о проверке прикладываются:

- `astra-build-manifest.txt`;
- `CMakeCache.txt`;
- тестовые PNG;
- один реальный обработанный пролёт;
- журнал SatDump;
- точное содержимое `/etc/astra/build_version`.

## Критерий выпуска

Новый presentation-тег создаётся только после того, как:

- матрица CI зелёная для 1.6 и 1.7;
- UBI 1.7 проходит либо задокументирована причина недоступности реестра;
- нативная сборка выполнена хотя бы на одной 1.6 и одной 1.7;
- реальный восходящий и нисходящий пролёт визуально проверены;
- исходные геопривязанные файлы побитово или содержательно не испорчены.
