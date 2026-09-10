#!/usr/bin/env python3
"""Copy Natural Earth NAME_RU verbatim; never translate or guess a place name.

Python 3.6+ / standard library only. See README.md in this directory.
"""
import argparse
import collections
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import stat
import sys
import tempfile
import urllib.request

SOURCE_COMMIT = '90266457d82d3717e47313e72a921e31a087d2c4'
SOURCE_URL = ('https://raw.githubusercontent.com/martynafford/natural-earth-geojson/'
              + SOURCE_COMMIT + '/10m/cultural/ne_10m_populated_places.json')
SOURCE_SHA256 = '536ed7d8b1618e999d0569f69300e642b8b6a62234fb516fedfee0bb2901609f'
OVERRIDES = Path(__file__).with_name('city_names_ru.matches.json')
ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / 'resources/maps/ne_10m_populated_places_simple.json'
REPORT = ROOT / 'resources/maps/city_names_ru.report.json'
MAX_BYTES = 128 * 1024 * 1024
MAX_DISTANCE_M = 100.0


class SyncError(ValueError):
    pass


def prop(properties, key, default=None):
    values = [v for k, v in properties.items() if k.lower() == key.lower()]
    if len(values) > 1 and any(v != values[0] for v in values[1:]):
        raise SyncError('Conflicting case variants of property ' + key)
    return values[0] if values else default


def country(properties):
    for key in ('adm0_a3', 'sov_a3', 'iso_a2', 'adm0name'):
        value = prop(properties, key)
        if value is not None and str(value).strip() not in ('', '-99'):
            return str(value).strip().casefold()
    return ''


def names(properties):
    return {str(value).strip().casefold()
            for key in ('name', 'nameascii', 'name_en', 'ls_name')
            for value in [prop(properties, key)] if isinstance(value, str) and value.strip()}


def place_id(properties):
    value = prop(properties, 'ne_id')
    try:
        number = float(value)
        return str(int(number)) if math.isfinite(number) and number > 0 and number.is_integer() else ''
    except (TypeError, ValueError, OverflowError):
        return ''


def point(feature):
    geometry = feature.get('geometry') or {}
    if not isinstance(geometry, dict):
        raise SyncError('Invalid geometry object')
    coordinates = geometry.get('coordinates', [])
    if not isinstance(coordinates, list):
        raise SyncError('Invalid coordinates array')
    if feature.get('type') != 'Feature' or geometry.get('type') != 'Point' or len(coordinates) < 2:
        raise SyncError('Expected a Point feature')
    if any(isinstance(v, bool) or not isinstance(v, (int, float)) for v in coordinates[:2]):
        raise SyncError('Non-numeric coordinates')
    lon, lat = coordinates[:2]
    if not math.isfinite(lon) or not math.isfinite(lat) or not -180 <= lon <= 180 or not -90 <= lat <= 90:
        raise SyncError('Invalid coordinates')
    return float(lon), float(lat)


def distance(a, b):
    lon1, lat1, lon2, lat2 = map(math.radians, a + b)
    value = math.sin((lat2-lat1)/2)**2 + math.cos(lat1)*math.cos(lat2)*math.sin((lon2-lon1)/2)**2
    return 6371008.8 * 2 * math.asin(math.sqrt(min(1.0, max(0.0, value))))


def features(document):
    if not isinstance(document, dict) or document.get('type') != 'FeatureCollection':
        raise SyncError('Expected a GeoJSON FeatureCollection')
    result = document.get('features')
    if not isinstance(result, list) or not result:
        raise SyncError('Empty or invalid features array')
    for feature in result:
        if not isinstance(feature, dict) or not isinstance(feature.get('properties'), dict):
            raise SyncError('Invalid feature properties')
        point(feature)
    return result


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('utf-8')


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def without_ru(document):
    result = copy.deepcopy(document)
    for feature in result['features']:
        properties = feature['properties']
        for key in list(properties):
            if key.lower() == 'name_ru':
                del properties[key]
    return result


def historical_point(properties):
    """Older Natural Earth coordinates retained in the simple properties."""
    lon, lat = prop(properties, 'longitude'), prop(properties, 'latitude')
    if any(isinstance(v, bool) or not isinstance(v, (int, float)) for v in (lon, lat)):
        return None
    if not math.isfinite(lon) or not math.isfinite(lat) or not -180 <= lon <= 180 or not -90 <= lat <= 90:
        return None
    return float(lon), float(lat)


def feature_digest(feature):
    return digest(without_ru({'features': [feature]})['features'][0])


def synchronize(target, source, overrides=None):
    """Return (new_document, deterministic_report). Errors never mutate inputs."""
    targets, sources = features(target), features(source)
    target_ids = collections.Counter(place_id(f['properties']) for f in targets)
    source_ids = collections.defaultdict(list)
    by_coordinates = collections.defaultdict(list)
    by_name = collections.defaultdict(set)
    for index, feature in enumerate(sources):
        properties = feature['properties']
        source_ids[place_id(properties)].append(index)
        by_coordinates[tuple(round(v, 6) for v in point(feature))].append(index)
        for name in names(properties):
            by_name[(country(properties), name)].add(index)
    if not any(any(k.lower() == 'name_ru' for k in f['properties']) for f in sources):
        raise SyncError('Source has no NAME_RU/name_ru field')

    reviewed = {}
    if overrides:
        if overrides.get('schema_version') != 1 or overrides.get('source_document_sha256') != digest(source):
            raise SyncError('Reviewed matches belong to a different source snapshot')
        for entry in overrides.get('entries', []):
            key = str(entry['target_ne_id'])
            if key in reviewed or not key or target_ids[key] != 1:
                raise SyncError('Reviewed target identifier is missing or not unique: ' + key)
            reviewed[key] = entry

    result = copy.deepcopy(target)
    used = set()
    used_reviewed = set()
    absent = []
    methods = collections.Counter()
    missing = []
    failures = []
    mapping = []
    for index, feature in enumerate(targets):
        properties, coordinates = feature['properties'], point(feature)
        nation = country(properties)
        def compatible(candidate):
            other = country(sources[candidate]['properties'])
            return not nation or not other or nation == other
        candidates = [i for i in by_coordinates[tuple(round(v, 6) for v in coordinates)] if compatible(i)]
        method = 'coordinates_6dp'
        if len(candidates) > 1:
            candidates = [i for i in candidates if names(properties) & names(sources[i]['properties'])]
            method = 'coordinates_and_name'
        if not candidates:
            identifier = place_id(properties)
            if identifier and target_ids[identifier] == 1 and len(source_ids[identifier]) == 1:
                candidates = [i for i in source_ids[identifier]
                              if compatible(i) and distance(coordinates, point(sources[i])) <= MAX_DISTANCE_M]
                method = 'unique_id_and_distance'
        if not candidates:
            possible = set()
            if nation:
                for name in names(properties):
                    possible.update(by_name[(nation, name)])
            candidates = [i for i in sorted(possible)
                          if distance(coordinates, point(sources[i])) <= MAX_DISTANCE_M]
            method = 'country_name_and_distance'
        if not candidates:
            # A newer geometry must never overwrite an older place's identity.
            # Check the retained historical coordinates only with exact names
            # and country. No nearest-neighbour or fuzzy-name assignment.
            historical = historical_point(properties)
            if historical is not None:
                candidates = [i for i in sorted(possible)
                              if distance(historical, point(sources[i])) <= 1.0]
                method = 'historical_coordinates_country_name'
        override = reviewed.get(place_id(properties))
        if override is not None:
            if override.get('target_feature_sha256') != feature_digest(feature):
                raise SyncError('Reviewed target changed: ' + str(index))
            used_reviewed.add(place_id(properties))
            match = override.get('source_index')
            if match is None:
                if candidates:
                    raise SyncError('A supposedly absent place now has a match: ' + str(index))
                destination = result['features'][index]['properties']
                for key in list(destination):
                    if key.lower() == 'name_ru':
                        del destination[key]
                destination['name_ru'] = None
                absent.append({'target_index': index, 'name': prop(properties, 'name'),
                               'ne_id': prop(properties, 'ne_id'), 'reason': override['reason']})
                mapping.append([index, None, None])
                continue
            if isinstance(match, bool) or not isinstance(match, int) or not 0 <= match < len(sources):
                raise SyncError('Invalid reviewed source index')
            if override.get('source_feature_sha256') != digest(sources[match]):
                raise SyncError('Reviewed source changed: ' + str(match))
            if not compatible(match) or not names(properties) & names(sources[match]['properties']):
                raise SyncError('Reviewed pair has no common name/country')
            if candidates and candidates != [match]:
                raise SyncError('Reviewed match conflicts with automatic candidates')
            candidates = [match]
            method = 'reviewed_version_pair'
        if len(candidates) != 1 or candidates[0] in used:
            failures.append({'target_index': index, 'name': prop(properties, 'name'),
                             'country': nation, 'coordinates': list(coordinates),
                             'source_candidates': candidates,
                             'reason': 'unmatched' if not candidates else 'ambiguous_or_reused'})
            continue
        match = candidates[0]
        russian = prop(sources[match]['properties'], 'name_ru')
        if russian is not None and not isinstance(russian, str):
            raise SyncError('Source NAME_RU must be a string or null at index ' + str(match))
        destination = result['features'][index]['properties']
        for key in list(destination):
            if key.lower() == 'name_ru':
                del destination[key]
        destination['name_ru'] = russian
        used.add(match)
        methods[method] += 1
        mapping.append([index, match, russian])
        if not isinstance(russian, str) or not russian.strip():
            missing.append({'target_index': index, 'name': prop(properties, 'name'), 'source_index': match})
    report = {
        'schema_version': 2,
        'source_commit': SOURCE_COMMIT,
        'source_url': SOURCE_URL,
        'source_document_sha256': digest(source),
        'target_unmanaged_sha256': digest(without_ru(target)),
        'source_features': len(sources), 'target_features': len(targets),
        'matched_features': len(used), 'russian_names': len(used) - len(missing),
        'missing_in_source': absent,
        'reviewed_matches_sha256': digest(overrides) if overrides else None,
        'missing_russian_names': missing, 'match_methods': dict(sorted(methods.items())),
        'unused_source_features': len(sources) - len(used),
        'duplicate_target_ne_id_values': sum(v > 1 for k, v in target_ids.items() if k),
        'max_fallback_distance_m': MAX_DISTANCE_M,
        'mapping_sha256': digest(mapping), 'failures': failures,
    }
    if used_reviewed != set(reviewed):
        raise SyncError('Unused reviewed matches; inspect the target catalogue')
    if not failures:
        if without_ru(result) != without_ru(target):
            raise SyncError('Unexpected change outside name_ru')
        for target_index, source_index, russian in mapping:
            expected = None if source_index is None else prop(sources[source_index]['properties'], 'name_ru')
            if result['features'][target_index]['properties']['name_ru'] != expected:
                raise SyncError('Source identity verification failed')
        report['output_document_sha256'] = digest(result)
    return result, report


def read_bytes(location):
    if str(location).startswith('https://'):
        request = urllib.request.Request(str(location), headers={'User-Agent': 'SatDump-city-names/1'})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read(MAX_BYTES + 1)
    else:
        with open(str(location), 'rb') as stream:
            data = stream.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise SyncError('Input exceeds 128 MiB')
    return data


def atomic_json(path, document):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o644
    payload = (json.dumps(document, ensure_ascii=False, indent=2, sort_keys=True, allow_nan=False) + '\n').encode('utf-8')
    handle, temporary = tempfile.mkstemp(prefix='.' + path.name + '.', dir=str(path.parent))
    try:
        with os.fdopen(handle, 'wb') as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, str(path))
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', default=SOURCE_URL, help='Pinned HTTPS source or local GeoJSON')
    parser.add_argument('--target', default=str(TARGET), help='Existing SatDump GeoJSON')
    parser.add_argument('--matches', default=str(OVERRIDES), help='Reviewed version-pair manifest for the pinned source')
    parser.add_argument('--output', help='Output path; default: atomically update --target')
    parser.add_argument('--report', default=str(REPORT), help='Deterministic verification report')
    parser.add_argument('--check', action='store_true', help='Verify only; do not write any files')
    args = parser.parse_args(argv)
    try:
        if Path(args.report).resolve() in (Path(args.target).resolve(), Path(args.output or args.target).resolve()):
            raise SyncError('Report and data paths must differ')
        if not str(args.source).startswith('https://'):
            protected = (Path(args.source).resolve(), Path(args.matches).resolve())
            if Path(args.report).resolve() in protected or Path(args.output or args.target).resolve() in protected:
                raise SyncError('Output/report must not overwrite source or reviewed matches')
        source_data = read_bytes(args.source)
        source = json.loads(source_data.decode('utf-8-sig'))
        target = json.loads(read_bytes(args.target).decode('utf-8-sig'))
        source_hash = hashlib.sha256(source_data).hexdigest()
        pinned = source_hash == SOURCE_SHA256
        if args.source == SOURCE_URL and not pinned:
            raise SyncError('Pinned source checksum mismatch')
        overrides = None
        if pinned:
            overrides = json.loads(read_bytes(args.matches).decode('utf-8'))
        output, report = synchronize(target, source, overrides)
        report['source_bytes_sha256'] = source_hash
        if not pinned:
            report['source_url'] = None
            report['source_commit'] = None
            report['source_input'] = str(args.source)
        if report['failures']:
            if not args.check:
                atomic_json(args.report, report)
            print(json.dumps({'matched': report['matched_features'], 'failed': len(report['failures']), 'first_failures': report['failures'][:20]}, ensure_ascii=False, indent=2))
            raise SyncError('Unmatched/ambiguous cities; data file was NOT changed')
        if args.check:
            if output != target:
                raise SyncError('name_ru differs from the source; run without --check')
        else:
            atomic_json(args.output or args.target, output)
            atomic_json(args.report, report)
        print(json.dumps({k: report[k] for k in ('target_features', 'source_features', 'matched_features', 'russian_names', 'match_methods', 'unused_source_features')}, ensure_ascii=False))
        return 0
    except (OSError, ValueError, TypeError, KeyError) as error:
        print('ERROR: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
