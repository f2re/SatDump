#!/usr/bin/env python3
"""Offline integrity gate for source/installed city data and the actual fonts.

Python 3.5+; no dependencies or network access. Never modifies resources.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

FILES = ('maps/ne_10m_populated_places_simple.json', 'maps/city_names_ru.report.json',
         'fonts/font.ttf', 'fonts/Roboto-Medium.ttf')


def verify(source, installed):
    source, installed = Path(source), Path(installed)
    hashes = {}
    for relative in FILES:
        a, b = (source / relative).read_bytes(), (installed / relative).read_bytes()
        if not a or a != b:
            raise ValueError('Missing, empty or stale installed resource: ' + relative)
        hashes[relative] = hashlib.sha256(b).hexdigest()
    data = json.loads((installed / FILES[0]).read_text(encoding='utf-8'))
    report = json.loads((installed / FILES[1]).read_text(encoding='utf-8'))
    payload = json.dumps(data, ensure_ascii=False, sort_keys=True,
                         separators=(',', ':'), allow_nan=False).encode('utf-8')
    if hashlib.sha256(payload).hexdigest() != report['output_document_sha256']:
        raise ValueError('Catalogue content differs from its verified source report')
    features = data['features']
    if data['type'] != 'FeatureCollection' or len(features) != report['target_features']:
        raise ValueError('Invalid catalogue or lost city records')
    if any('name_ru' not in f['properties'] or not isinstance(f['properties']['name_ru'], (str, type(None))) for f in features):
        raise ValueError('Missing or invalid name_ru property')
    russian = sum(isinstance(f['properties']['name_ru'], str) and bool(f['properties']['name_ru'].strip()) for f in features)
    if not russian or russian != report['russian_names'] or report['failures']:
        raise ValueError('Russian coverage differs from the verified report')
    return {'features': len(features), 'russian_names': russian,
            'fallback_names': len(features) - russian, 'sha256': hashes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_resources')
    parser.add_argument('installed_resources')
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.source_resources, args.installed_resources), ensure_ascii=False, indent=2))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print('City resource validation FAIL: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
