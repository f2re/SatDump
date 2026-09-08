<div align="center">

# 🛰️ SatDump · Astra

**От принятого файла — к цветосинтезу, научному паспорту и презентации снимков**

SatDump 1.2.2 · автономная станция обработки · русскоязычная веб-галерея

[![Station CI](https://github.com/f2re/SatDump/actions/workflows/station-astra16.yml/badge.svg?branch=release%2F1.2.2)](https://github.com/f2re/SatDump/actions/workflows/station-astra16.yml?query=branch%3Arelease%2F1.2.2)
[![Presentation CI](https://github.com/f2re/SatDump/actions/workflows/presentation-1.2.2.yml/badge.svg?branch=release%2F1.2.2)](https://github.com/f2re/SatDump/actions/workflows/presentation-1.2.2.yml?query=branch%3Arelease%2F1.2.2)
[![Документация](https://github.com/f2re/SatDump/actions/workflows/documentation.yml/badge.svg?branch=release%2F1.2.2)](https://github.com/f2re/SatDump/actions/workflows/documentation.yml?query=branch%3Arelease%2F1.2.2)

[![Ветка](https://img.shields.io/badge/ветка-release%2F1.2.2-334155?style=flat-square)](https://github.com/f2re/SatDump/tree/release/1.2.2)
[![Лицензия](https://img.shields.io/badge/лицензия-GPL--3.0-334155?style=flat-square)](LICENSE)
[![Релизы](https://img.shields.io/badge/скачать-проверить_релизы-0f766e?style=flat-square)](https://github.com/f2re/SatDump/releases)

[**🚀 Начало работы**](docs/ru/station/INSTALL.md) · [**📚 Справочник**](docs/ru/station/README.md) · [**📦 Выбор пакета**](docs/ru/RELEASES.md) · [**🛠️ Диагностика**](docs/ru/station/TROUBLESHOOTING.md)

</div>

> [!IMPORTANT]
> **Astra 1.6 Station и Astra 1.7 Desktop — разные продукты.** `glibc228` не заменяет `glibc224`. Архив **Source code** не является установочным пакетом. Зелёная проверка документации не означает, что собран и испытан бинарный релиз.

## 🧭 Выберите задачу

| Мне нужно | Куда перейти | Что получится |
|---|---|---|
| Установить воркер и сайт на Astra 1.6 | [Установка станции](docs/ru/station/INSTALL.md) | Две службы systemd, локальная галерея, постоянная очередь |
| Работать с графическим SatDump на Astra 1.7 | [Пакет Astra 1.7](docs/ru/ASTRA17_BUNDLE.md) | Отдельная переносимая Desktop-сборка |
| Подключить папку приёмника | [Входы и маркеры готовности](docs/ru/station/INPUTS.md) | Автоматическое обнаружение завершённых файлов |
| Настроить синтез и подписи | [Продукты](docs/ru/station/PRODUCTS.md), [настройки](docs/ru/station/CONFIGURATION.md) | Пресеты SatDump, PNG/JSON, отдельный научный архив |
| Собрать, выпустить или развернуть пакет | [Сборка и CI/CD](docs/ru/station/BUILD_RELEASE.md) | Проверяемый пакет и управляемая установка |
| Разобраться с ошибкой | [Таблица диагностики](docs/ru/station/TROUBLESHOOTING.md) | Проверки от входного каталога до браузера |

## ✨ Что входит

**Обработка.** Штатный SatDump декодирует запись или повторно обрабатывает сохранённые `product.cbor` с каналами. Калибровку, выражения RGB, LUT, геометрию и научные легенды выполняет движок, а не веб-сервер.

**Автоматизация.** Воркер отслеживает каталоги, проверяет завершённость передачи, ведёт очередь SQLite и журнал каждого задания. Обработка идёт на рабочей копии. Готовые материалы публикуются атомарно.

**Презентация.** Русскоязычная галерея показывает снимки без обрезки, миниатюры, подписи и JSON-паспорта. Есть фильтры, поиск, полноэкранный режим и автоматическое перелистывание.

**Эксплуатация.** Готовый пакет содержит отдельное окружение Python/Pillow. На целевой станции не нужны Docker, Node.js, pip или компилятор. Код, настройки и наблюдения хранятся раздельно.

```mermaid
flowchart LR
    A["📥 Файлы приёмника"] --> B["Проверка готовности"]
    B --> C["Очередь SQLite"]
    C --> D["Рабочая копия + SatDump"]
    D --> E["🔬 Приватный научный архив"]
    D --> F["PNG / JSON / превью"]
    F --> G["🖥️ Галерея и презентация"]
```

Текстовый маршрут: **вход → готовность → очередь → обработка → архив + публикация → сайт**. [Подробная архитектура и границы ответственности →](docs/ru/station/DATAFLOW.md)

## 📦 Что скачивать

| Вариант | Отличительный признак | Сайт и воркер |
|---|---|---|
| Astra 1.6 Station | `astra16-station-…-x86_64.tar.gz` | Да, в составе пакета станции |
| Astra 1.6, только движок | `presentation-…-glibc224-x86_64.tar.gz` | Нет |
| Astra 1.7 Desktop | `astra17-desktop-glibc228-x86_64.tar.gz` | Нет станции; это Desktop/CLI |
| GitHub Source code | Автоматические ZIP / tar.gz исходников | Не готовая установка |

Проверенный опубликованный Desktop-релиз: [v1.2.2-astra17-presentation](https://github.com/f2re/SatDump/releases/tag/v1.2.2-astra17-presentation). Статус станции и границы тестирования: [матрица релизов](docs/ru/RELEASES.md). В ней зафиксирован результат проверки на 8 сентября 2026 года, а не обещание успешного следующего запуска.

## 🚀 Начать с готового пакета

**Где выполнять:** на целевой Astra 1.6, в каталоге доверенного скачанного архива. Замените имя в переменной на фактическое имя пакета станции.

```bash
ARCHIVE='satdump-1.2.2-astra16-station-ВАШ_COMMIT-x86_64.tar.gz'
sha256sum -c "$ARCHIVE.sha256"
tar -xzf "$ARCHIVE"
cd "${ARCHIVE%.tar.gz}"
./station.sh help
sudo ./install.sh
```

После успешной установки сайт доступен по адресу `http://127.0.0.1:8090/`. Для доступа из доверенной ЛВС адрес задаётся явно:

```bash
sudo ./install.sh --listen 0.0.0.0 --port 8090
```

> [!WARNING]
> Сайт предназначен для локальной станции или доверенной ЛВС. Установщик не открывает межсетевой экран, не меняет мандатные политики Astra и не настраивает публичный TLS-доступ. [Права и сетевые ограничения →](docs/ru/station/OPERATIONS.md)

## ⌨️ Полезные команды

| Где | Команда | Назначение |
|---|---|---|
| Исходный репозиторий | `./station.sh build --jobs 2` | Собрать движок, окружение и пакет станции |
| Исходный репозиторий | `./build.sh` | Прежний сборщик только движка; не сайта |
| Готовый пакет | `sudo ./install.sh --dry-run` | Показать план установки; не полный тест совместимости |
| Установленная станция | `sudo satdump-station status` | Последние 30 заданий и ошибки |
| Установленная станция | `sudo satdump-station logs` | Наблюдать журналы обеих служб |
| Установленная станция | `sudo satdump-station restart` | Перечитать настройки через перезапуск |
| Установленная станция | `sudo satdump-station rollback` | Вернуть предыдущий код; данные не откатываются |

[Полный справочник команд, параметров и кодов состояния →](docs/ru/station/REFERENCE.md)

## 🗂️ Карта репозитория

| Путь | Содержимое |
|---|---|
| [`station.sh`](station.sh), [`install.sh`](install.sh) | Основные точки входа станции |
| [`services/station/`](services/station/README.md) | Воркер, очередь, HTTP-сервер и браузерный интерфейс |
| [`config/station/`](config/station/README.md) | Примеры конфигурации, не активные настройки установленной станции |
| [`scripts/station/`](scripts/station/README.md) | Сборка, упаковка, установка, SSH-развёртывание |
| [`scripts/astra/`](scripts/astra/README.md) | Нативные и переносимые сборщики движка |
| [`src-cli/`](src-cli/), [`src-core/`](src-core/), [`plugins/`](plugins/) | Исходники SatDump; структура CMake сохранена |
| [`docs/`](docs/README.md) | Руководства по задачам, научная документация и правила оформления |
| [`tests/station/`](tests/station/README.md) | Контрактные тесты станции, не замена проверки реального приёма |
| `build/` | Промежуточные результаты; не устанавливать на рабочую станцию |
| `dist/station/` | Результат успешной сборки полного пакета |

## 🔎 Что важно понимать заранее

Готовый PNG не содержит автоматически восстанавливаемых спектральных каналов. Для пересчёта нужны каналы или корректно описанная запись. `mtime` файла не равен времени наблюдения. `north_up` — запрос ориентации, а подтверждение находится в паспорте. `max_items` ограничивает ленту, **не удаляет архив**. Офлайн-установка не означает первичную сборку без интернета. [Все ограничения и частые вопросы →](docs/ru/station/TROUBLESHOOTING.md)

## 🤝 Разработка и сопровождение

[Как внести изменение](CONTRIBUTING.md) · [История изменений](CHANGELOG.md) · [Стиль документации](docs/STYLE_GUIDE.md) · [Сообщить об ошибке](https://github.com/f2re/SatDump/issues)

Форк основан на SatDump; лицензия — [GPL-3.0](LICENSE). Уведомления зависимостей среды исполнения входят в `runtime/licenses/`. Не включайте в публичные обращения закрытые записи, SSH-ключи, внутренние адреса и сведения ограниченного доступа.
