# 🩺 Диагностика и устранение неисправностей

## 1. Сначала соберите сведения

```bash
bash scripts/astra/check-system.sh --strict
cat /etc/astra/build_version 2>/dev/null || true
uname -a
g++ --version
cmake --version
pkg-config --modversion volk 2>/dev/null || true
git rev-parse HEAD
```

Сохраните полный вывод configure/build:

```bash
bash scripts/astra/build.sh --profile headless 2>&1 | tee build-astra.log
```

## 2. `CMake 3.12 or higher is required`

Системный CMake слишком старый.

```bash
bash scripts/astra/bootstrap-cmake.sh
```

Проверка:

```bash
~/.local/opt/satdump-astra/cmake-3.18.6/bin/cmake --version
```

Сценарий `build.sh` найдёт его автоматически.

## 3. `find_library ... REQUIRED` или неизвестный аргумент

Для совместимости этой ветки принят CMake 3.18+. Не пытайтесь продолжать с
CMake 3.12–3.17, даже если корневой проект формально объявляет более низкий
минимум.

```bash
bash scripts/astra/bootstrap-cmake.sh
```

## 4. Нет поддержки C++17

Симптомы:

```text
std::optional is not a member of std
filesystem: No such file or directory
CXX compiler does not support C++17
```

Проверка:

```bash
source scripts/astra/common.sh
compiler_supports_cxx17 g++ && echo OK
```

Для Astra 1.6 подключите штатный `repository-dev` своего обновления и установите
доступный GCC/G++ 8 или новее. Можно задать явно:

```bash
export CC=/usr/bin/gcc-8
export CXX=/usr/bin/g++-8
bash scripts/astra/build.sh --profile headless
```

## 5. `Could NOT find VOLK`

Проверка:

```bash
pkg-config --modversion volk
```

Если пакет отсутствует:

```bash
bash scripts/astra/bootstrap-thirdparty.sh --component volk
```

Затем:

```bash
export PKG_CONFIG_PATH="$HOME/.local/opt/satdump-astra/deps/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
```

`build.sh` делает это автоматически.

## 6. `NNG_LIBRARY-NOTFOUND` или `nng/nng.h: No such file`

```bash
bash scripts/astra/bootstrap-thirdparty.sh --component nng
```

Проверка:

```bash
test -f "$HOME/.local/opt/satdump-astra/deps/include/nng/nng.h" && echo OK
```

## 7. Не найдены PNG/TIFF/FFTW/jemalloc/curl

```bash
bash scripts/astra/install-deps.sh \
  --profile headless \
  --bootstrap-missing
```

Проверка отдельных заголовков:

```bash
ls /usr/include/png.h
ls /usr/include/tiffio.h
ls /usr/include/jemalloc/jemalloc.h
pkg-config --modversion fftw3f
pkg-config --modversion libcurl
```

Имена TIFF/curl-пакетов различаются между наборами репозиториев; установщик
выбирает первый доступный вариант.

## 8. GUI: не найден OpenGL или GLFW

```bash
bash scripts/astra/install-deps.sh --profile desktop --bootstrap-missing
```

Проверка:

```bash
pkg-config --modversion glfw3
ldconfig -p | grep -E 'libGL\.so|libOpenGL\.so'
```

Если GUI не нужен, используйте:

```bash
bash scripts/astra/build.sh --profile headless
```

## 9. GUI не запускается по SSH

Проверьте:

```bash
echo "$DISPLAY"
echo "$XAUTHORITY"
```

Для серверной обработки GUI не нужен. Используйте CLI. X11-forwarding должен
быть отдельно разрешён политикой системы и SSH.

## 10. SDR не найден

```bash
bash scripts/astra/run.sh -- sdr_probe
lsusb
```

Проверьте:

- соответствующий плагин включён при сборке;
- установлен runtime-драйвер;
- есть правило udev;
- пользователь входит в нужную группу;
- устройство не занято другим процессом.

Не запускайте SatDump от root как постоянное решение.

## 11. `satdump_cfg.json` или `pipelines` не найдены

Запускайте установленное дерево:

```bash
bash scripts/astra/build.sh --profile headless --install
bash scripts/astra/run.sh -- version
```

Проверьте:

```bash
PREFIX="$HOME/.local/opt/satdump-1.2.2"
ls "$PREFIX/share/satdump/satdump_cfg.json"
ls "$PREFIX/share/satdump/pipelines"
ls "$PREFIX/share/satdump/resources"
```

## 12. Ошибка JSON в настройках

```bash
python3 -m json.tool ~/.config/satdump/settings.json
```

Временно отложить пользовательский diff:

```bash
mv ~/.config/satdump/settings.json \
   ~/.config/satdump/settings.json.disabled
```

После запуска SatDump будет использовать основную конфигурацию.

## 13. Плашка не создаётся

Проверьте:

1. включена автоматическая обработка продуктов;
2. существует готовый композит;
3. в preset нет `"presentation": false`;
4. доступен шрифт;
5. renderer не сообщил ошибку в журнале.

```bash
grep -Ei 'presentation|annotated|font' satdump.log
```

Глобальное включение:

```json
{
  "satdump_general": {
    "presentation_enabled": {
      "value": true
    }
  }
}
```

## 14. Кириллица отображается квадратами

Проверьте файл:

```bash
ls -l resources/fonts/Roboto-Medium.ttf
```

После установки:

```bash
ls -l "$HOME/.local/opt/satdump-1.2.2/share/satdump/resources/fonts/Roboto-Medium.ttf"
```

Запустите smoke-тест и откройте его PNG.

## 15. Footer слишком высокий

Причины:

- длинные формулы;
- много категорий;
- большое количество notes;
- очень узкий исходный растр.

Решения:

- сократить экспертное описание без потери смысла;
- объединить второстепенные сведения в JSON-паспорт;
- использовать понятные короткие названия классов;
- не включать полную техническую формулу в каждую note, если она уже показана в
  компоненте.

Не уменьшайте шрифт до нечитаемого размера.

## 16. Неправильные каналы в RGB

Автоматический анализ сопоставляет токены с реальными именами каналов продукта.
Для сложного Lua/C++-алгоритма задайте экспертный блок `components` вручную.

Проверьте JSON-паспорт:

```bash
python3 -m json.tool *_annotated.json | less
```

## 17. Цвет шкалы не совпадает с изображением

Это ошибка конфигурации продукта. Изображение и легенда должны использовать одну
и ту же палитру и один диапазон.

Проверьте:

- min/max;
- инверсию;
- порядок color stops;
- эквализацию после окрашивания;
- единицы;
- no-data;
- underflow/overflow.

Не исправляйте только картинку легенды — исправьте общий `ColorMapSpec`/preset.

## 18. GeoTIFF смещён после добавления плашки

Presentation renderer не должен применяться к единственному GeoTIFF. Если плашка
оказалась внутри геопривязанного файла, используется сторонний или изменённый
путь сохранения.

Правильно:

```text
scientific.tif
scientific_annotated.png
scientific_annotated.json
```

## 19. Проекция пустая

Проверьте:

- наличие TLE для времени пролёта;
- timestamps;
- параметры проекции width/height;
- корректность геометрической конфигурации;
- соответствие канала и timestamps;
- журнал `Reprojecting composite`.

Можно временно оценить базовый/геометрически исправленный композит без проекции.

## 20. Сборка завершается из-за памяти

Уменьшите параллелизм:

```bash
bash scripts/astra/build.sh --profile headless --jobs 1
```

Очистите старый build:

```bash
bash scripts/astra/build.sh --profile headless --clean --jobs 1
```

Проверьте:

```bash
free -h
df -h
```

## 21. Бинарник падает на другой машине

Вероятная причина — аппаратная оптимизация или несовместимая библиотека.

Собирайте через Astra-сценарий: он отключает автоматический `-march=native` и
архитектурные SIMD-плагины базового профиля.

Сравните:

```bash
ldd /path/to/satdump
lscpu
```

Предпочтительно собирать на самой старой целевой версии Astra и тестировать на
всех поддерживаемых машинах.

## 22. Что приложить к отчёту об ошибке

```text
Astra build_version
архитектура и lscpu
gcc/g++ --version
cmake --version
SHA ветки
aстра-профиль и параметры build.sh
CMakeCache.txt
полный build log
satdump log
settings.json без секретов
presentation JSON
пример входных данных или минимальный фрагмент
скриншот/PNG дефекта
```

Не публикуйте пароли, закрытые адреса репозиториев и персональные координаты без
согласования.
