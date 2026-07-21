# 🚀 Развёртывание SatDump в эксплуатации

## 1. Варианты установки

### Пользовательская рабочая станция

```text
~/.local/opt/satdump-1.2.2
```

Подходит для GUI и индивидуального приёмного места.

### Системная установка

```text
/opt/satdump/releases/<версия>
/opt/satdump/current -> releases/<версия>
```

Подходит для сервера обработки и управляемого отката.

## 2. Рекомендуемая версия каталога

Пример:

```text
/opt/satdump/
├── current -> releases/1.2.2-presentation-50cff751
├── releases/
│   ├── 1.2.2-presentation-50cff751/
│   └── 1.2.2-presentation-<следующий-sha>/
└── var/
    ├── incoming/
    ├── working/
    ├── products/
    ├── archive/
    └── log/
```

Не устанавливайте новую сборку поверх единственного рабочего каталога.

## 3. Сборка версии

```bash
VERSION="1.2.2-presentation-$(git rev-parse --short HEAD)"
PREFIX="/opt/satdump/releases/${VERSION}"

bash scripts/astra/build.sh \
  --profile headless \
  --prefix "${PREFIX}" \
  --install
```

После проверки:

```bash
sudo ln -sfn "${PREFIX}" /opt/satdump/current
```

Переключение симлинка атомарно для новых процессов. Уже запущенные процессы
следует штатно перезапустить.

## 4. Учётная запись сервиса

Пример:

```bash
sudo useradd \
  --system \
  --home /var/lib/satdump \
  --create-home \
  --shell /usr/sbin/nologin \
  satdump
```

Каталоги:

```bash
sudo install -d -o satdump -g satdump -m 0750 \
  /data/satdump/incoming \
  /data/satdump/working \
  /data/satdump/products \
  /data/satdump/archive \
  /var/log/satdump
```

Не выдавайте сервисной учётной записи права на изменение бинарников и
репозиториев.

## 5. Конфигурация сервиса

```text
/var/lib/satdump/.config/satdump/settings.json
/var/lib/satdump/.config/satdump/satdump_tles.txt
```

Подготовка:

```bash
sudo -u satdump install -d -m 0750 \
  /var/lib/satdump/.config/satdump

sudo install -o satdump -g satdump -m 0640 \
  deployment/settings.json \
  /var/lib/satdump/.config/satdump/settings.json
```

## 6. Wrapper для CLI

```bash
sudo tee /usr/local/bin/satdump-1.2.2 >/dev/null <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail

PREFIX=/opt/satdump/current
DEPS=/opt/satdump-deps

export LD_LIBRARY_PATH="${PREFIX}/lib:${PREFIX}/lib64:${DEPS}/lib:${DEPS}/lib64:${LD_LIBRARY_PATH:-}"
exec "${PREFIX}/bin/satdump" "$@"
EOF
sudo chmod 0755 /usr/local/bin/satdump-1.2.2
```

Если NNG/VOLK установлены системными пакетами, отдельный `DEPS` не нужен.

## 7. Пакетная обработка через systemd

SatDump сам не является постоянно работающей очередью. Для автоматизации можно
использовать отдельный сервис/таймер или файловую очередь.

Пример шаблонного сервиса:

```ini
[Unit]
Description=SatDump processing job for %i
After=network.target

[Service]
Type=oneshot
User=satdump
Group=satdump
WorkingDirectory=/data/satdump/working
Environment=HOME=/var/lib/satdump
ExecStart=/usr/local/bin/satdump-1.2.2 \
  meteor_m2x_lrpt baseband \
  /data/satdump/incoming/%i.cs16 \
  /data/satdump/products/%i \
  --samplerate 240000 \
  --baseband_format cs16

PrivateTmp=true
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
ReadOnlyPaths=/data/satdump/incoming
ReadWritePaths=/data/satdump/working /data/satdump/products /var/lib/satdump /var/log/satdump
```

Параметры pipeline приведены как пример. Проверьте поддержку защитных директив
в конкретной версии systemd Astra Linux.

## 8. Обновление

1. Получить изменения в отдельном каталоге.
2. Проверить SHA и diff.
3. Собрать новый versioned prefix.
4. Выполнить smoke-тест.
5. Обработать один эталонный пролёт.
6. Сравнить научные растры с предыдущей версией.
7. Проверить оформленные PNG и JSON.
8. Переключить `/opt/satdump/current`.
9. Сохранить предыдущую версию до завершения приёмки.

## 9. Откат

```bash
sudo ln -sfn \
  /opt/satdump/releases/1.2.2-presentation-<предыдущий-sha> \
  /opt/satdump/current
```

После отката:

- перезапустите задачи;
- не удаляйте продукты новой версии;
- зафиксируйте причину отката;
- приложите `settings.json`, журнал и SHA сборки к инциденту.

## 10. Приёмочные эталоны

Храните небольшой набор исходных данных:

```text
test-data/
├── meteor-day.cs16
├── meteor-night.cs16
├── noaa-apt.wav
└── expected/
```

Для каждого обновления проверяйте:

- количество декодированных строк;
- размеры каналов;
- временной диапазон;
- наличие геопривязки;
- контрольные точки яркостной температуры;
- формулу RGB;
- легенду;
- JSON-паспорт;
- отсутствие падений и утечек диска.

## 11. Хранение данных

Рекомендуется разделить сроки хранения:

| Данные | Типовой подход |
|---|---|
| IQ | ограниченный срок или выборочный архив |
| кадры/CADU | для повторной обработки |
| научные GeoTIFF | долгосрочный архив |
| оформленные PNG | оперативная публикация |
| JSON-паспорта | хранить вместе с PNG |
| логи | ротация и архивирование |

Политика определяется ценностью наблюдений и объёмом диска.

## 12. Ротация журналов

Пример `/etc/logrotate.d/satdump`:

```text
/var/log/satdump/*.log {
    daily
    rotate 14
    compress
    delaycompress
    missingok
    notifempty
    create 0640 satdump satdump
}
```

Фактическое расположение журнала зависит от способа запуска и конфигурации.

## 13. Мониторинг

Контролируйте:

- свободное место;
- длительность обработки;
- код возврата;
- появление `*_annotated.png` и JSON;
- количество ошибок декодера;
- packet loss/SNR, если метаданные доступны;
- возраст TLE;
- рост временных каталогов.

## 14. Резервное копирование

Минимум:

```text
settings.json
локальные пресеты viewer.instruments
TLE/OMM
скрипты запуска
SHA и параметры сборки
эталонные продукты
```

Не включайте секреты и пароли в публичный Git.

## 15. Разделение ролей

- администратор ОС — репозитории, пакеты, права, обновления Astra;
- инженер сборки — toolchain, CMake, зависимости, бинарники;
- инженер-метеоролог — пресеты, физические диапазоны, легенды;
- оператор — приём, запуск pipeline, контроль качества;
- администратор архива — хранение и публикация.

Следующий документ: [Диагностика](TROUBLESHOOTING.md).
