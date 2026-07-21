# Portable Astra profile: glibc 2.24

Этот каталог содержит воспроизводимый CLI-профиль SatDump 1.2.2 для переноса
между системами Astra Linux 1.6/1.7 на x86_64.

Основная команда:

```bash
bash scripts/astra/build.sh \
  --mode portable-glibc224 \
  --profile reference
```

Уменьшенный Meteor/NOAA/APT-профиль:

```bash
bash scripts/astra/build.sh \
  --mode portable-glibc224 \
  --profile meteor
```

## Инварианты

Сборка считается успешной только когда одновременно выполнены условия:

1. Исходное дерево соответствует SatDump 1.2.2 и не содержит маркеров 2.x.
2. Rootfs имеет архитектуру amd64 и glibc 2.24.
3. Используются версии из `lock.env`: GCC 9.5.0, CMake 3.27.9 и точный commit NNG 1.8.0.
4. Исходное дерево монтируется в chroot только для чтения.
5. Установка выполняется в заново созданный `DESTDIR`, а не в постоянный `/opt/satdump`.
6. В бандл не попадают известные посторонние плагины от SatDump 2.x.
7. Все ELF-зависимости разрешаются внутри бандла.
8. Требуемая версия `GLIBC_*` не превышает заданный baseline.
9. `satdump version` подтверждает версию 1.2.2.
10. Все плагины загружаются без `undefined symbol`.
11. Smoke-тесты Minimal/Presentation, легенд и ориентации проходят до упаковки.
12. Runtime-проверки выполняются из пустого рабочего каталога, чтобы исходное дерево не могло подменить resources, pipelines или settings.

## Файлы

| Файл | Назначение |
|---|---|
| `lock.env` | Зафиксированные версии и контрольные значения |
| `prepare-rootfs.sh` | Создание изолированного Debian Stretch rootfs |
| `build.sh` | Управление chroot и bind-mount с аварийной очисткой |
| `inside-chroot.sh` | Сборка toolchain, SatDump, тесты и staging |
| `make-bundle.sh` | Сбор runtime-библиотек, ABI-проверки и tar.gz |
| `validate-bundle.sh` | Проверка готового бандла на целевой Astra |

Полная документация: [`docs/ru/PORTABLE_ASTRA.md`](../../../docs/ru/PORTABLE_ASTRA.md).
