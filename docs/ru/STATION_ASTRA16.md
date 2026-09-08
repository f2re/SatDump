# 🚀 SatDump Station · начните здесь

[📚 Полный справочник](station/README.md) · [📦 Релизы](RELEASES.md) · [🛠️ Диагностика](station/TROUBLESHOOTING.md)

**Назначение:** автономная обработка файлов на Astra Linux 1.6 x86_64, очередь, цветосинтез и локальный сайт. Это не Desktop-пакет Astra 1.7 и не установщик из Source code ZIP.

## Три шага

| Шаг | Что сделать | Подробности |
|---|---|---|
| 1 | Выбрать реальный бинарный пакет Station, проверить `.sha256`, распаковать | [Установка](station/INSTALL.md) |
| 2 | Из корня пакета выполнить `sudo ./install.sh` | [Права, адрес сайта, первый снимок](station/INSTALL.md) |
| 3 | Настроить источник и подать завершённые файлы | [PNG, CBOR, IQ и `.ready`](station/INPUTS.md) |

Сайт по умолчанию: `http://127.0.0.1:8090/`. Для доверенной ЛВС из корня пакета: `sudo ./install.sh --listen 0.0.0.0 --port 8090`.

## Что находится где

| Путь | Назначение |
|---|---|
| `/opt/satdump-station/current` | Активный код |
| `/etc/satdump-station` | Активные настройки |
| `/var/lib/satdump-station/inbox` | Входы по умолчанию |
| `/var/lib/satdump-station/archive` | Приватный научный архив |
| `/var/lib/satdump-station/public` | Только материалы сайта |

```bash
sudo systemctl status satdump-worker satdump-web --no-pager
sudo satdump-station status
sudo satdump-station logs
```

> [!IMPORTANT]
> Для полноценного пересчёта нужны каналы/CBOR или корректная запись. Готовая картинка не заменяет измерения. `max_items` не очищает архив; `rollback` не возвращает исторические данные; проверка в Debian не заменяет приёмку на Astra.

[⚙️ Все настройки](station/CONFIGURATION.md) · [🖥️ Управление сайтом](station/WEBSITE.md) · [🛡️ Обновление и резервирование](station/OPERATIONS.md) · [📦 Сборка и деплой](station/BUILD_RELEASE.md) · [⌨️ Все команды](station/REFERENCE.md)

В новых пакетах этот документ экспортируется в `START_HERE.ru.md`, полный справочник — в `docs/`. Старые опубликованные архивы изменением документации ветки не переупаковываются.
