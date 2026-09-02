#!/usr/bin/env python3
"""Audit every SatDump image composite against presentation semantic rules.

The audit is deliberately offline. It reads the repository JSONC configuration,
finds every ``rgb_composites`` collection, identifies the active handler
(equation/LUT/Lua/C++), applies the same matching rules as the C++ renderer and
writes a complete Markdown/JSON coverage report.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, Iterable


HANDLER_KEYS = ("equation", "lut", "lua", "cpp")
TEXT_RULES = {
    "name_contains": "name",
    "description_contains": "description",
    "lut_contains": "lut",
    "lua_contains": "lua",
    "cpp_contains": "cpp",
    "instrument_contains": "instrument",
}


@dataclass(frozen=True)
class CompositeRecord:
    instrument: str
    name: str
    handler: str
    description: str
    profile: str
    legend: str
    source: str
    explicit_legend: bool
    warnings: tuple[str, ...]


def strip_json_comments(text: str) -> str:
    """Remove // and /* */ comments without touching quoted strings."""
    output: list[str] = []
    index = 0
    quoted = False
    quote = ""
    escaped = False
    while index < len(text):
        char = text[index]
        if quoted:
            output.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quoted = False
            index += 1
            continue
        if char in {'"', "'"}:
            quoted = True
            quote = char
            output.append(char)
            index += 1
            continue
        if char == "/" and index + 1 < len(text) and text[index + 1] == "/":
            index += 2
            while index < len(text) and text[index] not in "\r\n":
                index += 1
            continue
        if char == "/" and index + 1 < len(text) and text[index + 1] == "*":
            index += 2
            while index + 1 < len(text) and text[index : index + 2] != "*/":
                index += 1
            index = min(len(text), index + 2)
            continue
        output.append(char)
        index += 1
    cleaned = "".join(output)
    # SatDump's file is JSONC; tolerate occasional trailing commas as well.
    previous = None
    while cleaned != previous:
        previous = cleaned
        cleaned = re.sub(r",\s*([}\]])", r"\1", cleaned)
    return cleaned


def load_jsonc(path: Path) -> Any:
    try:
        return json.loads(strip_json_comments(path.read_text(encoding="utf-8")))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"Не удалось прочитать {path}: {error}") from error


def walk_composite_collections(value: Any, path: tuple[str, ...] = ()) -> Iterable[tuple[str, dict[str, Any]]]:
    if isinstance(value, dict):
        composites = value.get("rgb_composites")
        if isinstance(composites, dict):
            instrument = path[-1] if path else "unknown"
            yield instrument, composites
        for key, child in value.items():
            if key != "rgb_composites":
                yield from walk_composite_collections(child, path + (str(key),))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from walk_composite_collections(child, path + (str(index),))


def normalized(value: Any) -> str:
    return str(value or "").lower().replace("\\", "/")


def handler_for(preset: dict[str, Any]) -> tuple[str, list[str]]:
    handlers = [key for key in HANDLER_KEYS if isinstance(preset.get(key), str) and preset[key].strip()]
    if len(handlers) == 1:
        return handlers[0], []
    if not handlers:
        return "unknown", ["не найден equation/LUT/Lua/C++ обработчик"]
    return "+".join(handlers), ["одновременно заданы несколько обработчиков: " + ", ".join(handlers)]


def output_count(expression: str) -> int:
    expression = expression.rsplit(";", 1)[-1]
    depth = 0
    quoted = False
    quote = ""
    escaped = False
    count = 1 if expression.strip() else 0
    for char in expression:
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quoted = False
            continue
        if char in {'"', "'"}:
            quoted = True
            quote = char
        elif char in "([{":
            depth += 1
        elif char in ")]}" and depth:
            depth -= 1
        elif char == "," and depth == 0:
            count += 1
    return count


def profile_matches(profile: dict[str, Any], fields: dict[str, str], handler: str) -> bool:
    match = profile.get("match")
    if not isinstance(match, dict):
        return False
    handler_any = match.get("handler_any")
    if isinstance(handler_any, list) and handler_any:
        if handler.lower() not in {normalized(item) for item in handler_any}:
            return False
    has_text_rule = False
    matched = False
    for rule, field in TEXT_RULES.items():
        needles = match.get(rule)
        if not isinstance(needles, list) or not needles:
            continue
        has_text_rule = True
        haystack = normalized(fields.get(field, ""))
        if any(normalized(needle) in haystack for needle in needles if str(needle).strip()):
            matched = True
    return matched if has_text_rule else True


def select_profile(profiles: list[dict[str, Any]], fields: dict[str, str], handler: str) -> dict[str, Any] | None:
    for profile in profiles:
        if profile_matches(profile, fields, handler):
            return profile
    return None


def explicit_legend(preset: dict[str, Any]) -> bool:
    presentation = preset.get("presentation")
    return isinstance(presentation, dict) and isinstance(presentation.get("legend"), dict)


def calibrated_scale(preset: dict[str, Any]) -> bool:
    calib = preset.get("calib_cfg")
    if not isinstance(calib, dict):
        return False
    valid = 0
    for item in calib.values():
        if not isinstance(item, dict):
            continue
        minimum = item.get("min")
        maximum = item.get("max")
        if isinstance(minimum, (int, float)) and isinstance(maximum, (int, float)) and maximum > minimum:
            valid += 1
    return valid == 1


def legend_strategy(preset: dict[str, Any], handler: str, profile: dict[str, Any] | None) -> tuple[str, str]:
    if explicit_legend(preset):
        kind = normalized(preset["presentation"]["legend"].get("kind", "explicit")) or "explicit"
        return kind, "явная настройка presentation.legend"
    if profile:
        definition = profile.get("legend", {})
        kind = normalized(definition.get("kind", "auto")) or "auto"
        return kind, f"профиль {profile.get('id', 'unknown')}"
    if handler == "equation" and output_count(str(preset.get("equation", ""))) in (3, 4):
        return "composite", "автоматический состав R/G/B"
    if calibrated_scale(preset):
        return "continuous", "автоматическая физическая шкала calib_cfg"
    if handler == "lut":
        return "explanation", "описание LUT и её входного диапазона"
    if handler == "lua":
        return "explanation", "описание Lua-алгоритма и ограничений"
    if handler == "cpp":
        return "explanation", "описание C++-алгоритма и ограничений"
    return "explanation", "безопасное общее описание без выдуманной шкалы"


def validate_profiles(document: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if document.get("schema") != "satdump.presentation.semantic-profiles/1":
        errors.append("неверная schema каталога профилей")
    profiles = document.get("profiles")
    if not isinstance(profiles, list):
        return errors + ["profiles должен быть массивом"]
    ids: set[str] = set()
    for index, profile in enumerate(profiles):
        prefix = f"profiles[{index}]"
        if not isinstance(profile, dict):
            errors.append(f"{prefix}: профиль не является объектом")
            continue
        profile_id = str(profile.get("id", "")).strip()
        if not profile_id:
            errors.append(f"{prefix}: отсутствует id")
        elif profile_id in ids:
            errors.append(f"{prefix}: повторный id {profile_id}")
        ids.add(profile_id)
        if not isinstance(profile.get("match"), dict):
            errors.append(f"{prefix}: отсутствует match")
        if not str(profile.get("title", "")).strip():
            errors.append(f"{prefix}: отсутствует русское название")
        if not str(profile.get("purpose", "")).strip():
            errors.append(f"{prefix}: отсутствует назначение")
        legend = profile.get("legend")
        if not isinstance(legend, dict):
            errors.append(f"{prefix}: отсутствует legend")
            continue
        kind = normalized(legend.get("kind", ""))
        if kind == "categorical" and not isinstance(legend.get("categories"), list):
            errors.append(f"{prefix}: категориальная легенда без categories")
        if kind == "continuous":
            has_range = isinstance(legend.get("min"), (int, float)) and isinstance(legend.get("max"), (int, float))
            if not has_range and not legend.get("scale_from_calibration"):
                errors.append(f"{prefix}: continuous без диапазона или scale_from_calibration")
    return errors


def build_records(config: dict[str, Any], profiles: list[dict[str, Any]], resources_root: Path) -> list[CompositeRecord]:
    records: list[CompositeRecord] = []
    for instrument, composites in walk_composite_collections(config):
        for name, raw_preset in composites.items():
            if not isinstance(raw_preset, dict):
                continue
            handler, warnings = handler_for(raw_preset)
            description = str(raw_preset.get("description", ""))
            if description and not (resources_root / description).is_file():
                warnings.append("файл описания не найден")
            fields = {
                "name": str(name),
                "description": description,
                "lut": str(raw_preset.get("lut", "")),
                "lua": str(raw_preset.get("lua", "")),
                "cpp": str(raw_preset.get("cpp", "")),
                "instrument": instrument,
            }
            profile = select_profile(profiles, fields, handler)
            legend, source = legend_strategy(raw_preset, handler, profile)
            records.append(
                CompositeRecord(
                    instrument=instrument,
                    name=str(name),
                    handler=handler,
                    description=description,
                    profile=str(profile.get("id")) if profile else "generic",
                    legend=legend,
                    source=source,
                    explicit_legend=explicit_legend(raw_preset),
                    warnings=tuple(warnings),
                )
            )
    return sorted(records, key=lambda item: (item.instrument.lower(), item.name.lower()))


def markdown_report(records: list[CompositeRecord], profile_count: int) -> str:
    handlers = Counter(record.handler for record in records)
    profiles = Counter(record.profile for record in records)
    legends = Counter(record.legend for record in records)
    warning_count = sum(bool(record.warnings) for record in records)
    curated = sum(record.profile != "generic" for record in records)
    lines = [
        "# Аудит семантики presentation-композитов",
        "",
        "Отчёт сформирован автоматически из `satdump_cfg.json` и каталога",
        "`resources/presentation/composite_profiles_ru.json`.",
        "",
        "## Сводка",
        "",
        f"- Композитов: **{len(records)}**",
        f"- Курируемых семантических профилей: **{profile_count}**",
        f"- Композитов с курируемым профилем: **{curated}**",
        f"- Композитов с безопасным универсальным fallback: **{len(records) - curated}**",
        f"- Композитов с предупреждениями структуры: **{warning_count}**",
        "",
        "### Обработчики",
        "",
    ]
    lines.extend(f"- `{name}`: {count}" for name, count in sorted(handlers.items()))
    lines.extend(["", "### Типы легенд", ""])
    lines.extend(f"- `{name}`: {count}" for name, count in sorted(legends.items()))
    lines.extend([
        "",
        "## Полный перечень",
        "",
        "| Прибор | Композит | Обработчик | Профиль | Легенда | Источник решения | Предупреждения |",
        "|---|---|---|---|---|---|---|",
    ])
    for record in records:
        warning = "; ".join(record.warnings) if record.warnings else "—"
        values = [
            record.instrument,
            record.name,
            record.handler,
            record.profile,
            record.legend,
            record.source,
            warning,
        ]
        escaped = [str(value).replace("|", "\\|").replace("\n", " ") for value in values]
        lines.append("| " + " | ".join(escaped) + " |")
    lines.extend([
        "",
        "## Правило достоверности",
        "",
        "Если точное соответствие цвета физическому явлению не зафиксировано в профиле",
        "или в явной `presentation.legend`, renderer показывает состав каналов, формулы,",
        "тип алгоритма и ограничение интерпретации. Он не выдумывает категориальную или",
        "количественную шкалу.",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, default=Path("satdump_cfg.json"))
    parser.add_argument(
        "--profiles",
        type=Path,
        default=Path("resources/presentation/composite_profiles_ru.json"),
    )
    parser.add_argument("--resources-root", type=Path, default=Path("resources"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--json-output", type=Path)
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()

    config = load_jsonc(args.config)
    profile_document = load_jsonc(args.profiles)
    errors = validate_profiles(profile_document)
    profiles = profile_document.get("profiles", []) if isinstance(profile_document, dict) else []
    records = build_records(config, profiles, args.resources_root)
    if not records:
        errors.append("в конфигурации не найдено ни одного rgb_composites")
    if any(not record.legend for record in records):
        errors.append("не для всех композитов определена стратегия легенды")

    report = markdown_report(records, len(profiles))
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report + "\n", encoding="utf-8")
    else:
        print(report)
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(
            json.dumps(
                {
                    "schema": "satdump.presentation.composite-audit/1",
                    "errors": errors,
                    "records": [asdict(record) for record in records],
                },
                ensure_ascii=False,
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        if args.strict:
            return 1
    print(
        f"Проверено композитов: {len(records)}; профилей: {len(profiles)}; ошибок: {len(errors)}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
