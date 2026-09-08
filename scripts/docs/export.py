#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Export local Markdown manuals; rewrite omitted source links to a pinned Git SHA."""
from __future__ import print_function
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys
from urllib.parse import quote, unquote, urlsplit
import check as doccheck


def rewrite(text, origin, destination, root, available, revision):
    origin, destination = Path(origin), Path(destination)
    def replace(match):
        value = match.group('url')
        parsed = urlsplit(value.strip('<>'))
        if parsed.scheme or parsed.netloc:
            return match.group(0)
        path = unquote(parsed.path)
        target = os.path.normpath(path.lstrip('/') if path.startswith('/') else os.path.join(str(origin.parent), path)) if path else str(origin)
        if target == '..' or target.startswith('../'):
            raise ValueError('Documentation link escapes source: ' + value)
        target = target.replace(os.sep, '/')
        tail = ('?' + parsed.query if parsed.query else '') + ('#' + parsed.fragment if parsed.fragment else '')
        if target in available:
            new = os.path.relpath(target, str(destination.parent)).replace(os.sep, '/') + tail
        else:
            kind = 'tree' if (root / target).is_dir() else 'blob'
            new = 'https://github.com/f2re/SatDump/{0}/{1}/{2}{3}'.format(kind, revision, quote(target, safe='/'), tail)
        return match.group(0)[:match.start('url') - match.start()] + new + match.group(0)[match.end('url') - match.start():]
    lines, opened = [], None
    for line in text.splitlines(True):
        marker = doccheck.FENCE.match(line.rstrip('\n'))
        if marker:
            if opened is None:
                opened = marker.group(1)
            elif marker.group(1)[0] == opened[0] and len(marker.group(1)) >= len(opened) and not marker.group(2).strip():
                opened = None
            lines.append(line)
        elif opened:
            lines.append(line)
        else:
            lines.append(doccheck.HTML_LINK.sub(replace, doccheck.LINK.sub(replace, line)))
    return ''.join(lines)


def export(root, output, revision, package=False):
    root = Path(root).resolve()
    output = Path(os.path.abspath(str(output)))
    if output == root or os.path.commonpath([str(output), str(root / 'docs')]) == str(root / 'docs'):
        raise ValueError('Export must not overwrite the source documentation')
    if not re.match(r'^[0-9a-f]{40}$', revision):
        raise ValueError('Use a full 40-character Git commit SHA')
    with open(str(root / 'docs/navigation.json'), encoding='utf-8') as stream:
        manifest = json.load(stream)
    selected = set(manifest['checked'])
    selected.update(str(p.relative_to(root)) for p in (root / 'docs').rglob('*.md'))
    selected.update(['LICENSE', 'docs/navigation.json'])
    if (root / 'docs/assets').is_dir():
        selected.update(str(p.relative_to(root)) for p in (root / 'docs/assets').rglob('*')
                        if p.is_file() and p.suffix.lower() in ('.svg', '.png', '.jpg', '.jpeg'))
    available = set(selected)
    for value in selected:
        available.update(str(p) for p in Path(value).parents if str(p) != '.')
    aliases = {'START_HERE.ru.md': 'docs/ru/STATION_ASTRA16.md',
               'OPERATIONS.ru.md': 'docs/ru/STATION_OPERATIONS.md',
               'README.md': 'docs/ru/station/README.md'}
    for relative in sorted(selected):
        source, target = root / relative, output / relative
        if source.is_symlink() or not source.is_file():
            raise ValueError('Missing or symlink documentation file: ' + relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        if source.suffix.lower() == '.md':
            target.write_text(rewrite(source.read_text(encoding='utf-8'), relative, relative, root, available, revision), encoding='utf-8')
        else:
            shutil.copyfile(str(source), str(target))
    for alias, original in aliases.items():
        text = rewrite((root / original).read_text(encoding='utf-8'), original, alias, root, available, revision)
        if alias == 'README.md' and not package:
            text += '\n> [!NOTE]\n> Это отдельный пакет документации, не бинарный установщик. Примеры команд не выполняются автоматически. Ссылки на исходный код и внешние плашки требуют сети; главы справочника доступны локально.\n'
        (output / alias).write_text(text, encoding='utf-8')
    manifest['checked'] = sorted(set(manifest['checked']) | set(aliases))
    with open(str(output / 'docs/navigation.json'), 'w', encoding='utf-8') as stream:
        json.dump(manifest, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    with open(str(output / 'docs/EXPORT.json'), 'w', encoding='utf-8') as stream:
        json.dump({'git_commit': revision, 'kind': 'embedded-manual' if package else 'documentation-only',
                   'external_links': 'pinned source references; not fetched'}, stream, indent=2)
        stream.write('\n')
    print('Exported {0} documents/assets and {1} entry points'.format(len(selected), len(aliases)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', default=str(Path(__file__).resolve().parents[2]))
    parser.add_argument('--output', required=True)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--package', action='store_true')
    args = parser.parse_args()
    export(args.root, args.output, args.revision, args.package)


if __name__ == '__main__':
    main()
