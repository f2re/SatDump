# Нативная проверка на Astra Linux 1.6 и 1.7

CI в GitHub проверяет сборочную логику профилей `1.6` и `1.7` и отдельно собирает проект в официальном контейнере Astra Linux UBI 1.7. Для сертификационной или эксплуатационной приёмки рекомендуется дополнительно использовать self-hosted runner на точном оперативном обновлении ОС.

## Требования к runner

- x86_64;
- Astra Linux Special Edition нужной линии;
- GitHub Actions Runner, зарегистрированный для репозитория;
- метки `self-hosted`, `linux`, `x64` и одна из `astra-1.6` / `astra-1.7`;
- доступ к утверждённому APT-зеркалу;
- не менее 4 ГБ ОЗУ и 20 ГБ свободного места.

## Команды проверки на runner

```bash
bash scripts/astra/check-system.sh --strict
bash scripts/astra/install-deps.sh --profile headless --bootstrap-missing
bash scripts/astra/build.sh --profile headless --clean

build_dir="build/astra-$(cat /etc/astra/build_version | cut -c1-3)-headless"
export LD_LIBRARY_PATH="$PWD/${build_dir}:$PWD/${build_dir}/plugins:${LD_LIBRARY_PATH:-}"

"${build_dir}/satdump-presentation-test" \
  resources/fonts/Roboto-Medium.ttf \
  "${build_dir}/presentation-test-output"

"${build_dir}/satdump-presentation-output-test" \
  resources/fonts/Roboto-Medium.ttf \
  "${build_dir}/presentation-output-test"

bash scripts/astra/collect-build-info.sh --build-dir "${build_dir}"
```

## Что приложить к протоколу приёмки

- `/etc/astra/build_version`;
- `uname -a`;
- `astra-build-manifest.txt`;
- журнал CMake и компиляции;
- PNG из `presentation-test-output`;
- PNG и JSON из `presentation-output-test`;
- один реальный восходящий и один нисходящий пролёт.

## Ограничение

Профиль совместимости с `ASTRA_VERSION_OVERRIDE` не подменяет нативную ОС. Он проверяет параметры CMake, набор плагинов и сценарии ветвления, но не доказывает ABI-совместимость с конкретным обновлением Astra Linux 1.6/1.7.
