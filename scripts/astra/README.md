# 🛡️ Сборка SatDump 1.2.2 в Astra Linux

Этот каталог содержит сценарии для **Astra Linux Special Edition 1.6 и 1.7**.
Они не меняют системные репозитории автоматически и не заменяют системный
компилятор или CMake.

> После клонирования можно один раз выполнить:
>
> ```bash
> chmod +x scripts/astra/*.sh
> ```
>
> Либо всегда запускать сценарии через `bash scripts/astra/<имя>.sh`.

## 🚦 Быстрый запуск

### Сервер обработки без GUI

```bash
bash scripts/astra/check-system.sh
bash scripts/astra/install-deps.sh --profile headless --bootstrap-missing
bash scripts/astra/build.sh --profile headless --install
bash scripts/astra/run.sh -- version
```

### Рабочая станция с GUI и RTL-SDR

```bash
bash scripts/astra/install-deps.sh --profile desktop --bootstrap-missing
bash scripts/astra/build.sh --profile desktop --sdr rtl --install
bash scripts/astra/run.sh --ui
```

## 📁 Состав каталога

| Файл | Назначение |
|---|---|
| `check-system.sh` | Диагностика версии ОС, C++17, CMake и обязательных библиотек |
| `install-deps.sh` | Установка доступных пакетов из подключённых Astra-репозиториев |
| `bootstrap-cmake.sh` | Локальная сборка CMake 3.18.6 с проверкой SHA-256 |
| `bootstrap-thirdparty.sh` | Локальная сборка NNG 1.5.2 и VOLK 2.5.2 |
| `build.sh` | Конфигурация и сборка по профилю |
| `run.sh` | Проверка установленного дерева и запуск CLI/GUI |
| `repos/*.example` | Примеры официальных репозиториев без автоматического применения |

## 🧱 Профили

### `headless`

Минимальный переносимый профиль:

- CLI без графического интерфейса;
- Meteor-M LRPT/HRPT;
- NOAA/MetOp;
- NOAA APT и аналоговые протоколы;
- стандартные C++-композиты;
- плашки, легенды и JSON-паспорта;
- без SDR-драйверов, OpenCL и аудиовыхода.

Он предпочтителен для серверов, пакетной обработки и закрытых контуров.

### `desktop`

Добавляет:

- графический интерфейс;
- OpenGL/GLFW;
- PortAudio;
- RTL-SDR по умолчанию.

Другой SDR-профиль можно выбрать через `--sdr`.

### `full`

Включает все протокольные плагины SatDump и расширенный набор устройств. Требует
существенно больше библиотек. Этот профиль не считается минимальным профилем
совместимости Astra и должен собираться под конкретную рабочую станцию.

## 🔧 Astra Linux 1.6

Основные особенности:

1. Нужен подключённый `repository-dev`, соответствующий установленному обновлению.
2. Требуется компилятор с рабочим C++17 — обычно GCC/G++ 8 или новее.
3. Системный CMake может быть слишком старым. Используйте:

```bash
bash scripts/astra/bootstrap-cmake.sh
```

CMake устанавливается в:

```text
~/.local/opt/satdump-astra/cmake-3.18.6
```

Системный `/usr/bin/cmake` не изменяется.

## 🔧 Astra Linux 1.7

GCC 8 с C++17 обычно доступен штатно. Сценарий всё равно проверяет компилятор
не по номеру версии, а реальной компиляцией тестовой программы.

Если CMake старше/младше требуемого порога, используется тот же локальный
bootstrap, что и для 1.6.

## 📦 Если NNG или VOLK отсутствуют

```bash
bash scripts/astra/bootstrap-thirdparty.sh --component all
```

Локальный префикс:

```text
~/.local/opt/satdump-astra/deps
```

Архивы проверяются контрольными суммами. Для офлайн-сборки:

```bash
bash scripts/astra/bootstrap-thirdparty.sh \
  --archive-dir /mnt/approved/satdump-sources
```

В каталоге должны находиться:

```text
nng-1.5.2.tar.gz
volk-2.5.2.tar.gz
```

## 🌐 Репозитории

Сценарии **не записывают** `/etc/apt/sources.list`. Примеры:

- `repos/astra-1.6.list.example`;
- `repos/astra-1.7.list.example`.

Перед применением:

1. проверьте `/etc/astra/build_version`;
2. выберите frozen-репозиторий именно своего обновления;
3. согласуйте изменения с администратором;
4. не подключайте Debian/Ubuntu-репозитории поверх Astra Linux;
5. в изолированной сети используйте внутреннее зеркало.

## 🧪 Проверка результата

При обычной сборке создаётся `satdump-presentation-test`. Он формирует три PNG:

```text
build/astra-*/presentation-test-output/continuous.png
build/astra-*/presentation-test-output/categorical.png
build/astra-*/presentation-test-output/composite.png
```

Проверяются:

- непрерывная температурная шкала;
- категориальная легенда;
- описание RGB-композита;
- сохранение исходной ширины спутникового растра.

## 🧭 Пользовательские параметры CMake

Дополнительные параметры передаются после `--`:

```bash
bash scripts/astra/build.sh --profile headless -- \
  -DPLUGIN_FY3=ON \
  -DPLUGIN_EOS=ON
```

## 🛰️ Пример обработки файла

```bash
bash scripts/astra/run.sh -- \
  meteor_m2x_lrpt \
  baseband \
  /data/input/meteor.cs16 \
  /data/output/meteor-pass \
  --samplerate 240000 \
  --baseband_format cs16
```

Подставьте фактический pipeline, формат и частоту дискретизации своей записи.

## 🔒 Замечания для защищённой среды

- Не запускайте скрипт установки целиком от `root`: он сам использует `sudo`
  только для APT. Иначе локальный CMake попадёт в `/root/.local`.
- Проверяйте исходные архивы и переносите их через утверждённый канал.
- Не снижайте уровень целостности системы ради сборки.
- Сохраняйте версию ОС, список пакетов, SHA коммита и параметры CMake вместе с
  эксплуатационным комплектом.
- Проверяйте собранный бинарник на точной версии Astra Linux, где он будет работать.

Полная документация: [`docs/ru/INSTALL_ASTRA.md`](../../docs/ru/INSTALL_ASTRA.md).
