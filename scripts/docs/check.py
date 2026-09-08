#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Offline checks for the curated Markdown set. Bash examples are NEVER executed."""
from __future__ import print_function
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import unicodedata
from urllib.parse import unquote, urlsplit

LINK = re.compile(r'\]\(\s*(?P<url><[^>\n]+>|[^\s)]+)')
HTML_LINK = re.compile(r'(?:href|src)=[\"\'](?P<url>[^\"\']+)[\"\']')
FENCE = re.compile(r'^ {0,3}(`{3,}|~{3,})(.*)$')


def split_blocks(text):
    """Separate fenced examples from prose without interpreting their commands."""
    prose, blocks, opened, code = [], [], None, []
    for number, line in enumerate(text.splitlines(), 1):
        marker = FENCE.match(line)
        if opened:
            if marker and marker.group(1)[0] == opened[0][0] and len(marker.group(1)) >= len(opened[0]) and not marker.group(2).strip():
                blocks.append((opened[1], '\n'.join(code), opened[2]))
                opened, code = None, []
            else:
                code.append(line)
            prose.append('')
        elif marker:
            opened = (marker.group(1), marker.group(2).strip(), number)
            prose.append('')
        else:
            prose.append(line)
    if opened:
        raise ValueError('Unclosed fence at line ' + str(opened[2]))
    return '\n'.join(prose), blocks


def slug(text):
    text = re.sub(r'<[^>]+>', '', text).lower()
    text = re.sub(r'\[([^]]+)\]\([^)]*\)', r'\1', text)
    text = ''.join(c for c in text if c in ' -_' or unicodedata.category(c)[0] in 'LNM')
    return text.replace(' ', '-')


def anchors(text):
    prose, unused = split_blocks(text)
    result = set(re.findall(r'<a\s+(?:id|name)=[\"\']([^\"\']+)', prose))
    seen = {}
    for line in prose.splitlines():
        match = re.match(r'^#{1,6}\s+(.+?)(?:\s+#+)?$', line)
        if match:
            name = slug(match.group(1))
            count = seen.get(name, 0)
            result.add(name if count == 0 else name + '-' + str(count))
            seen[name] = count + 1
    return result


def local_target(root, origin, url):
    parsed = urlsplit(url.strip('<>'))
    if parsed.scheme or parsed.netloc:
        return None
    relative = unquote(parsed.path)
    target = root / relative.lstrip('/') if relative.startswith('/') else origin.parent / relative
    if not relative:
        target = origin
    target = Path(os.path.realpath(str(target)))
    if os.path.commonpath([str(root), str(target)]) != str(root):
        raise ValueError('Link escapes repository: ' + url)
    return target, unquote(parsed.fragment)


def validate(root, files):
    root = Path(root).resolve()
    errors, totals = [], {'documents': 0, 'links': 0, 'json': 0, 'bash': 0}
    for relative in files:
        path = root / relative
        try:
            if not path.is_file():
                raise ValueError('Document missing')
            text = path.read_text(encoding='utf-8')
            prose, blocks = split_blocks(text)
            if len(re.findall(r'^#\s+', prose, re.M)) != 1:
                raise ValueError('Expected exactly one H1')
            if not text.endswith('\n'):
                raise ValueError('Missing final newline')
            totals['documents'] += 1
            for match in list(LINK.finditer(prose)) + list(HTML_LINK.finditer(prose)):
                totals['links'] += 1
                url = match.group('url')
                target = local_target(root, path, url)
                if target is None:
                    continue  # External links are not fetched or asserted healthy.
                destination, fragment = target
                if not destination.exists():
                    raise ValueError('Broken local link: ' + url)
                if fragment and destination.suffix.lower() == '.md':
                    if fragment not in anchors(destination.read_text(encoding='utf-8')):
                        raise ValueError('Unknown anchor: ' + url)
            for language, example, number in blocks:
                if language == 'json':
                    json.loads(example)
                    totals['json'] += 1
                elif language == 'bash':
                    proc = subprocess.Popen(['bash', '-n'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    unused, error = proc.communicate(example.encode('utf-8'), timeout=10)
                    if proc.returncode:
                        raise ValueError('Bash syntax at line {0}: {1}'.format(number, error.decode('utf-8', 'replace').strip()))
                    totals['bash'] += 1
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            errors.append('{0}: {1}'.format(relative, error))
    return totals, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', default=str(Path(__file__).resolve().parents[2]))
    args = parser.parse_args()
    root = Path(args.root).resolve()
    with open(str(root / 'docs/navigation.json'), encoding='utf-8') as stream:
        config = json.load(stream)
    totals, errors = validate(root, config['checked'])
    print(json.dumps(totals, ensure_ascii=False, sort_keys=True))
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
