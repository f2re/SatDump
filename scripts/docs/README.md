# 📚 Инструменты документации

[← Главная](../../README.md) · [Правила оформления](../../docs/STYLE_GUIDE.md)

| Файл | Назначение |
|---|---|
| [check.py](check.py) | Проверка навигационного набора из `docs/navigation.json` |
| [export.py](export.py) | Экспорт Markdown-справочника, пересчёт локальных ссылок, закрепление ссылок на код |

Нужны Python 3 и Bash; сторонние Python-пакеты, npm и интернет не требуются. Проверка Bash запускает только `bash -n`, не команды примера. Сетевые ссылки не запрашиваются, доступность внешних сайтов не гарантируется.

```bash
python3 scripts/docs/check.py
python3 -m unittest discover -s tests/docs -v
python3 scripts/docs/export.py --output /tmp/satdump-docs --revision "$(git rev-parse HEAD)"
python3 scripts/docs/check.py --root /tmp/satdump-docs
```

Выполняйте из корня исходников. Результат отдельного экспорта — **не установочный пакет программы**. Упаковщик вызывает экспорт с `--package`, добавляя документы в будущий бинарный архив; это не изменяет старые опубликованные релизы.
