# 📚 Документация SatDump 1.2.2 Presentation / Astra Linux 1.7

Эта документация относится к ветке **`release/1.2.2`** форка `f2re/SatDump`.
Форк сохраняет возможности SatDump 1.2.2 и добавляет оформление готовых
спутниковых изображений: два варианта плашек, физические/RGB-легенды,
нормализацию ориентации «север сверху», JSON-паспорт и полноценный Astra Linux
1.7 release-бандл.

## Главный документ для Astra Linux 1.7

Начинайте с:

**[Полное руководство Astra Linux 1.7](ASTRA17_COMPLETE_GUIDE.md)**

В нём собраны:

- скачивание GitHub Release;
- проверка SHA-256;
- запуск без установки;
- постоянная установка;
- обновление и rollback;
- GUI, CLI, live-приём и запись IQ;
- конфигурация;
- RTL-SDR;
- полный перечень presentation-возможностей;
- `minimal` и `editorial`;
- подписи, метаданные, branding;
- continuous/categorical/RGB-легенды;
- автоматический разбор каналов и формул;
- north-up и ручная ориентация;
- JSON-паспорт;
- проверка и диагностика.

## Release-пакет Astra Linux 1.7

Основной готовый пакет:

```text
satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz
```

Он собирается **непосредственно внутри официальной Astra Linux 1.7**, а не в
Debian/Buster compatibility-rootfs. В архив включается полный обнаруженный ELF
runtime closure, включая Astra glibc/loader, C++ runtime, GUI/audio/RTL-SDR и
транзитивные библиотеки.

После сборки тот же скачанный artifact запускается в свежем Astra Linux UBI 1.7
без `apt-get install`. Только после этой проверки workflow публикует GitHub
Release из ветки `release/1.2.2`.

## Остальные документы

| Документ | Для чего |
|---|---|
| [Полное руководство Astra 1.7](ASTRA17_COMPLETE_GUIDE.md) | Установка, запуск, настройки, release bundle и все реализованные функции |
| [Astra 1.7 bundle](ASTRA17_BUNDLE.md) | Техническое устройство native/full bundle и CI/CD |
| [Установка в Astra Linux](INSTALL_ASTRA.md) | Нативная разработческая установка и зависимости сборочной машины |
| [Сборка](BUILD.md) | Профили CMake и тесты |
| [Проверка Astra Linux](ASTRA_VALIDATION.md) | CI, UBI, эталонные изображения и приёмка |
| [Запуск и обработка](RUN.md) | CLI, GUI, offline/live/record |
| [Настройка](CONFIGURATION.md) | `satdump_cfg.json`, `settings.json`, TLE, логирование |
| [Плашки и легенды](PRESENTATION.md) | Presentation renderer, metadata, RGB и научные шкалы |
| [Два макета и ориентация](PRESENTATION_LAYOUTS.md) | Minimal/editorial, portrait/landscape, north-up |
| [Level-1C / SATPROF](LEVEL1C_SATPROF.md) | Контур Level-1C/SATPROF |
| [Развёртывание](DEPLOYMENT.md) | Рабочие станции, сервер, обновление и откат |
| [Диагностика](TROUBLESHOOTING.md) | Ошибки запуска, GUI, библиотек и обработки |
| [Legacy portable glibc 2.24](PORTABLE_ASTRA.md) | Старый compatibility-профиль; не является текущим Astra 1.7 release |

## Быстрый старт из GitHub Release

```bash
sha256sum -c satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz.sha256

tar -xzf satdump-1.2.2-astra17-desktop-full-x86_64.tar.gz
cd satdump-1.2.2-astra17-desktop-full-x86_64

./satdump version
./satdump-ui
```

Постоянная установка:

```bash
./install.sh
```

Для SatDump не требуется дополнительно устанавливать runtime `.deb`/APT-пакеты.
Аппаратные kernel drivers, доступ к USB и графическая сессия являются частью
самой Astra Linux и конфигурации рабочего места.

## Какие файлы создаёт presentation renderer

Для оформленного продукта сохраняются связанные результаты:

```text
<имя>.png / <имя>.tif                    исходный научный продукт
<имя>_annotated_minimal.png              простой оперативный дизайн
<имя>_annotated_minimal.json             JSON-паспорт minimal
<имя>_annotated_presentation.png         расширенный editorial-дизайн
<имя>_annotated_presentation.json        JSON-паспорт editorial
```

При необходимости совместимости можно включить:

```text
<имя>_annotated.png
<имя>_annotated.json
```

Исходный научный растр не расширяется плашками, не заменяется оформленной
копией и остаётся пригодным для ГИС и количественного анализа.

## Правило достоверности

Плашка показывает только фактически доступные сведения. Частота приёма, SNR,
высота пролёта, направление и прочие параметры не подставляются по одному
названию спутника. Если географическую ориентацию нельзя подтвердить, это
фиксируется в JSON-паспорте.
