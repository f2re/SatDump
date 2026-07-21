# 🛡️ Установка в Astra Linux Special Edition 1.6 и 1.7

## 1. Назначение

Документ описывает подготовку среды для ветки `release/1.2.2` форка
`f2re/SatDump` на Astra Linux Special Edition 1.6 и 1.7.

Поддерживаются два основных варианта:

- **headless** — сервер или автоматизированное рабочее место без GUI;
- **desktop** — графический интерфейс, OpenGL, звук и выбранный SDR.

Профиль `full` предназначен для стенда разработки и требует большего количества
библиотек.

## 2. Что отличается между 1.6 и 1.7

| Компонент | Astra Linux 1.6 | Astra Linux 1.7 |
|---|---|---|
| C++ | нужно проверить наличие GCC/G++ 8+ из средств разработки | штатный GCC 8 обычно обеспечивает C++17 |
| CMake | системная версия часто ниже требуемой; используется локальный CMake 3.18.6 | используется системный CMake, если он не ниже 3.18; иначе тот же bootstrap |
| Репозитории | нужны main/update и **repository-dev/dev-update** своего обновления | main и update своего обновления; расширенные репозитории — только при необходимости |
| Рекомендуемый профиль | `headless` | `headless` или `desktop` |

Сценарии не доверяют только номеру пакета: компилятор проверяется фактической
компиляцией программы C++17.

## 3. Проверка версии ОС

```bash
cat /etc/astra/build_version
```

Если файл отсутствует:

```bash
cat /etc/os-release
```

Диагностика проекта:

```bash
bash scripts/astra/check-system.sh
```

Строгий режим с ненулевым кодом при критических ошибках:

```bash
bash scripts/astra/check-system.sh --strict
```

## 4. Репозитории

> ⚠️ На сертифицированной системе состав репозиториев должен быть согласован с
> администратором. Не подключайте Debian/Ubuntu-репозитории поверх Astra Linux.

Проект не изменяет `/etc/apt/sources.list` автоматически. Шаблоны:

```text
scripts/astra/repos/astra-1.6.list.example
scripts/astra/repos/astra-1.7.list.example
```

### 4.1 Astra Linux 1.6

Для сборки нужны репозитории установленного обновления:

```text
repository
repository-update
repository-dev
repository-dev-update
```

Пример для 1.6.15 приведён в шаблоне. Для другой версии замените номер на
фактически установленный и утверждённый.

После изменения источников:

```bash
sudo apt update
```

### 4.2 Astra Linux 1.7

Базовая строка:

```text
deb https://dl.astralinux.ru/astra/stable/1.7_x86-64/repository-main/ 1.7_x86-64 main contrib non-free
```

Репозиторий обновления должен соответствовать конкретной версии `1.7.x` и UU.
Не используйте условные `1.7.X` и `uu/N` без замены.

## 5. Установка зависимостей

Не запускайте весь сценарий через `sudo`: он сам повышает права только для APT.
Так локальные инструменты останутся в домашнем каталоге пользователя.

### 5.1 Headless

```bash
bash scripts/astra/install-deps.sh \
  --profile headless \
  --bootstrap-missing
```

### 5.2 Desktop

```bash
bash scripts/astra/install-deps.sh \
  --profile desktop \
  --bootstrap-missing
```

### 5.3 Предварительный просмотр

```bash
bash scripts/astra/install-deps.sh \
  --profile headless \
  --bootstrap-missing \
  --dry-run
```

## 6. CMake для Astra Linux 1.6

SatDump использует C++17 и конструкции, для которых в этом форке принят
минимальный рабочий CMake **3.18**. Если подходящей версии нет:

```bash
bash scripts/astra/bootstrap-cmake.sh
```

Установка выполняется без root:

```text
~/.local/opt/satdump-astra/cmake-3.18.6
```

Системный `/usr/bin/cmake` не удаляется и не заменяется.

Проверка:

```bash
~/.local/opt/satdump-astra/cmake-3.18.6/bin/cmake --version
```

### 6.1 Офлайн-архив CMake

На машине с доступом в Интернет загрузите официальный файл:

```text
cmake-3.18.6.tar.gz
```

Перенесите через утверждённый канал и выполните:

```bash
bash scripts/astra/bootstrap-cmake.sh \
  --archive /mnt/approved/cmake-3.18.6.tar.gz
```

Сценарий проверяет SHA-256 до распаковки.

## 7. NNG и VOLK

`libnng-dev` и подходящий пакет VOLK могут отсутствовать в конкретном наборе
репозиториев. В этом случае:

```bash
bash scripts/astra/bootstrap-thirdparty.sh --component all
```

Префикс:

```text
~/.local/opt/satdump-astra/deps
```

Компоненты:

- NNG 1.5.2;
- VOLK 2.5.2.

Они собираются как локальные библиотеки и не заменяют системные пакеты.

### 7.1 Офлайн-режим

Подготовьте каталог:

```text
/mnt/approved/satdump-sources/
├── nng-1.5.2.tar.gz
└── volk-2.5.2.tar.gz
```

Запуск:

```bash
bash scripts/astra/bootstrap-thirdparty.sh \
  --archive-dir /mnt/approved/satdump-sources
```

## 8. Сборка

### 8.1 Сервер

```bash
bash scripts/astra/build.sh \
  --profile headless \
  --install
```

### 8.2 Рабочая станция

```bash
bash scripts/astra/build.sh \
  --profile desktop \
  --sdr rtl \
  --install
```

Пользовательский префикс по умолчанию:

```text
~/.local/opt/satdump-1.2.2
```

Системный вариант:

```bash
bash scripts/astra/build.sh \
  --profile headless \
  --prefix /opt/satdump \
  --install
```

Для `/opt` потребуется разрешение администратора на этапе установки.

## 9. Проверка

```bash
bash scripts/astra/run.sh -- version
```

Диагностика SDR-средств в desktop-профиле:

```bash
bash scripts/astra/run.sh -- sdr_probe
```

Smoke-тест оформления создаёт:

```text
build/astra-*/presentation-test-output/continuous.png
build/astra-*/presentation-test-output/categorical.png
build/astra-*/presentation-test-output/composite.png
```

Откройте их и проверьте:

- отсутствие обрезки текста;
- корректную кириллицу;
- совпадение цветов и подписей;
- отсутствие масштабирования спутникового растра;
- читаемость на целевом мониторе.

## 10. Закрытый контур

Рекомендуемый процесс:

1. На машине подготовки зафиксировать SHA коммита ветки.
2. Скачать исходники SatDump, CMake, NNG и VOLK.
3. Проверить контрольные суммы.
4. Сформировать ведомость файлов и зависимостей.
5. Перенести комплект через утверждённый канал.
6. Использовать локальное зеркало APT.
7. Выполнить `check-system.sh --strict`.
8. Собрать профиль `headless`.
9. Сохранить `CMakeCache.txt`, журнал сборки и тестовые PNG.
10. Проверить бинарник на той же версии ОС и обновления, где он будет работать.

## 11. Не рекомендуется

- смешивать пакеты Astra 1.6 и 1.7;
- подключать Ubuntu PPA;
- переносить бинарники, собранные с `-march=native`, на другой процессор;
- собирать `full` как первый производственный вариант;
- запускать GUI под root;
- изменять системный CMake вручную;
- игнорировать несовпадение контрольных сумм.

## 12. Официальные справочные материалы Astra Linux

- Интернет-репозитории Astra Linux Special Edition:
  https://wiki.astralinux.ru/kb/internet-repozitorii-astra-linux-special-edition-187794658.html
- Пример репозиториев и обновления 1.6.15:
  https://wiki.astralinux.ru/astra-linux-se16-bulletin-20241017SE16
- Навигатор официальных пакетов:
  https://packages.astralinux.ru/

Следующий документ: [Сборка](BUILD.md).
