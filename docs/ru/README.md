# 📚 Документация SatDump 1.2.2 Presentation

Эта документация относится к ветке **`release/1.2.2`** форка `f2re/SatDump`.
Форк сохраняет возможности SatDump 1.2.2 и добавляет оформление готовых
спутниковых изображений: верхнюю информационную панель, легенды и JSON-паспорт.

## С чего начать

| Документ | Для чего |
|---|---|
| [Установка в Astra Linux](INSTALL_ASTRA.md) | Astra Linux SE 1.6/1.7, репозитории, зависимости, офлайн-контур |
| [Сборка](BUILD.md) | Профили, параметры CMake, переносимость и тесты |
| [Запуск и обработка](RUN.md) | CLI, GUI, офлайн- и live-обработка |
| [Настройка](CONFIGURATION.md) | `satdump_cfg.json`, `settings.json`, каталоги, TLE и логирование |
| [Плашки и легенды](PRESENTATION.md) | Схема `presentation`, RGB, температуры, категории, темы |
| [Развёртывание](DEPLOYMENT.md) | Установка на рабочую станцию/сервер, обновления и откат |
| [Диагностика](TROUBLESHOOTING.md) | Ошибки CMake, C++17, библиотек, GUI и обработки |

## Быстрый маршрут

```bash
git clone --branch release/1.2.2 https://github.com/f2re/SatDump.git
cd SatDump
chmod +x scripts/astra/*.sh

bash scripts/astra/install-deps.sh --profile headless --bootstrap-missing
bash scripts/astra/build.sh --profile headless --install
bash scripts/astra/run.sh -- version
```

Для графической рабочей станции:

```bash
bash scripts/astra/install-deps.sh --profile desktop --bootstrap-missing
bash scripts/astra/build.sh --profile desktop --sdr rtl --install
bash scripts/astra/run.sh --ui
```

## Какие файлы создаются

Для оформленного продукта сохраняются связанные результаты:

```text
<имя>.png / <имя>.tif          исходный продукт SatDump
<имя>_annotated.png            изображение с плашкой и легендой
<имя>_annotated.json           паспорт отображения и метаданных
```

Геопривязанный научный растр не расширяется плашками и остаётся пригодным для
ГИС и количественного анализа.

## Правило достоверности

Плашка показывает только сведения, которые действительно присутствуют в
продукте или метаданных сеанса. Частота приёма, SNR, максимальная высота пролёта
и другие параметры не подставляются по названию спутника.
