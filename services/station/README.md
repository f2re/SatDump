# 🖥️ Службы SatDump Station

[← Главная](../../README.md) · [Архитектура](../../docs/ru/station/DATAFLOW.md) · [Сайт](../../docs/ru/station/WEBSITE.md)

| Файл | Назначение |
|---|---|
| [timebase.py](timebase.py) | UTC, монотонные интервалы и диагностика часов |
| [board.py](board.py) | Каталог BOARD, применение настроек и встроенная галерея |
| [control.py](control.py) | Авторизованный API настроек |
| [station.py](station.py) | Конфигурация, файловый вход, SQLite-очередь, обработка, публикация, HTTP |
| [web/index.html](web/index.html) | Структура русскоязычного интерфейса |
| [web/app.js](web/app.js) | Каталог, фильтры, предзагрузка, слайд-шоу и статус |
| [web/style.css](web/style.css) | Вёрстка галереи и режима презентации |

На установленной системе запускаются `satdump-worker`, `satdump-web` и `satdump-control` под разными пользователями. Веб-служба читает только публикационные материалы; исходники наблюдений и очередь не являются HTTP-ресурсами.

В бинарном пакете команда `satdump-station` использует поставляемое окружение Python. Дополнительный интерфейс устанавливается через `ui-deploy`; ручные изменения HTML в `current` будут заменены при обновлении. [Команды](../../docs/ru/station/REFERENCE.md) · [Тесты](../../tests/station/README.md).
