#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ORBItA display adapter for Station publications (Python 3.5+, no new daemon).

Ports the satellite-only rules from SatBoardRepository.php: UTC history window,
local assets, bounded preview/ambient and chronological passes. Publication stays
in the existing worker. HTTP GET never processes inputs or deletes their files.
"""
from __future__ import print_function
import copy
import datetime
import re
import os
import tempfile

DISPLAY_DEFAULTS = {
    'windowHours': 24, 'pollSeconds': 90, 'minimumDisplaySeconds': 25,
    'displaySeconds': 30, 'complexDisplaySeconds': 40, 'latestSeconds': 45,
    'preloadSeconds': 10, 'transitionMilliseconds': 300,
    'imageMode': 'original', 'ambientOpacityPercent': 82,
    'portraitTourEnabled': True, 'portraitTourTransitionMilliseconds': 900,
    'performanceMode': False, 'ambient': True, 'sourceDelayHours': 3,
    'imageTimeoutSeconds': 20, 'dailyReload': True, 'reloadHoursUTC': [3, 5],
    'complexProducts': ['cloudtopir', 'cloud_top_ir', 'mcir', 'night_microphysics', 'rgb'],
    'productPriority': ['night_microphysics', 'natural_color', 'cloudtopir', 'mcir', 'msa', 'vis']
}
DISPLAY_RANGES = {
    'ambientOpacityPercent': (0, 100), 'windowHours': (1, 48), 'pollSeconds': (30, 3600), 'minimumDisplaySeconds': (25, 600),
    'displaySeconds': (25, 600), 'complexDisplaySeconds': (25, 600), 'latestSeconds': (25, 600),
    'preloadSeconds': (1, 60), 'transitionMilliseconds': (0, 1000),
    'portraitTourTransitionMilliseconds': (400, 2000), 'sourceDelayHours': (1, 48),
    'imageTimeoutSeconds': (5, 120)
}
ASSET = re.compile(r'^items/[0-9a-f]{64}/[0-9]{3}(?:-preview|-thumb|-ambient(?:-2)?)?\.(?:png|jpg|json)$')
UTC = datetime.timezone.utc


def display_settings(value=None):
    """Unknown keys, booleans masquerading as numbers and invalid intervals fail."""
    value = {} if value is None else value
    if not isinstance(value, dict) or set(value) - set(DISPLAY_DEFAULTS):
        raise ValueError('display: неизвестные поля или не объект')
    result = copy.deepcopy(DISPLAY_DEFAULTS)
    for key, item in value.items():
        if key in DISPLAY_RANGES:
            low, high = DISPLAY_RANGES[key]
            if type(item) is not int or not low <= item <= high:
                raise ValueError('display.{0}: целое число {1}..{2}'.format(key, low, high))
        elif key == 'imageMode':
            if item not in ('original', 'preview'):
                raise ValueError('display.imageMode: original или preview')
        elif type(DISPLAY_DEFAULTS[key]) is bool:
            if type(item) is not bool:
                raise ValueError('display.' + key + ': требуется true/false')
        elif key == 'reloadHoursUTC':
            if not (isinstance(item, list) and len(item) == 2 and
                    all(type(x) is int for x in item) and 0 <= item[0] < item[1] <= 24):
                raise ValueError('display.reloadHoursUTC: [начало, конец], 0..24 UTC')
        elif not (isinstance(item, list) and len(item) <= 100 and
                  all(isinstance(x, str) and 0 < len(x) <= 240 and not any(ord(c) < 32 for c in x) for x in item)):
            raise ValueError('display.' + key + ': список названий продуктов')
        result[key] = copy.deepcopy(item)
    for key in ('displaySeconds', 'complexDisplaySeconds', 'latestSeconds'):
        if result[key] < result['minimumDisplaySeconds']:
            raise ValueError('display.' + key + ': меньше minimumDisplaySeconds')
    if result['preloadSeconds'] >= min(result[k] for k in ('displaySeconds', 'complexDisplaySeconds', 'latestSeconds')):
        raise ValueError('preloadSeconds должен быть меньше времени показа')
    return result


def utc_seconds(value):
    """Only explicitly zoned ISO timestamps. Never filesystem mtime/filename."""
    if not isinstance(value, str):
        return None
    match = re.match(r'^(\d{4}-\d{2}-\d{2})T(\d{2}:\d{2}:\d{2})(\.\d{1,6})?(Z|[+-]\d{2}:\d{2})$', value)
    if not match:
        return None
    try:
        date = datetime.datetime.strptime(match.group(1) + 'T' + match.group(2), '%Y-%m-%dT%H:%M:%S')
        if match.group(3):
            date = date.replace(microsecond=int(match.group(3)[1:].ljust(6, '0')))
        zone = match.group(4)
        offset = 0
        if zone != 'Z':
            hours, minutes = int(zone[1:3]), int(zone[4:6])
            if hours > 23 or minutes > 59:
                return None
            offset = (hours * 60 + minutes) * (1 if zone[0] == '+' else -1)
        return date.replace(tzinfo=datetime.timezone(datetime.timedelta(minutes=offset))).timestamp()
    except (ValueError, OverflowError):
        return None


def iso(value):
    return datetime.datetime.fromtimestamp(value, UTC).isoformat().replace('+00:00', 'Z')


def observation_interval(metadata):
    """Recognize explicit machine fields and the exact native Presentation v1/v2 UTC labels.

    The legacy labels already encode UTC. Parsing them is not inference of
    observation time; unrecognized labels are deliberately left unknown.
    """
    blocks = [metadata]
    if isinstance(metadata.get('pass'), dict):
        blocks.append(metadata['pass'])
    if isinstance(metadata.get('acquisition'), dict):
        blocks.append(metadata['acquisition'])
    for block in blocks:
        for start_key, end_key in (('acquisition_start_utc', 'acquisition_end_utc'), ('start', 'end'), ('start_utc', 'end_utc')):
            if start_key in block:
                start = utc_seconds(block[start_key])
                end = utc_seconds(block.get(end_key, block[start_key]))
                return (start, end) if start is not None and end is not None and end >= start else (None, None)
    label = blocks[1].get('acquisition_time', '') if len(blocks) > 1 and isinstance(metadata.get('pass'), dict) else metadata.get('acquisition_time', '')
    start = utc_seconds(label)
    if start is not None:
        return start, start
    if not isinstance(label, str):
        return None, None
    patterns = (
        (r'^(\d{2}\.\d{2}\.\d{4}) · (\d{2}:\d{2}:\d{2})(?:–(\d{2}:\d{2}:\d{2}))? UTC$', False),
        (r'^(\d{2}\.\d{2}\.\d{4} \d{2}:\d{2}:\d{2}) – (\d{2}\.\d{2}\.\d{4} \d{2}:\d{2}:\d{2}) UTC$', True)
    )
    for pattern, across_dates in patterns:
        match = re.match(pattern, label)
        if not match:
            continue
        try:
            first = match.group(1) if across_dates else match.group(1) + ' ' + match.group(2)
            last = match.group(2) if across_dates else match.group(1) + ' ' + (match.group(3) or match.group(2))
            start = datetime.datetime.strptime(first, '%d.%m.%Y %H:%M:%S').replace(tzinfo=UTC).timestamp()
            end = datetime.datetime.strptime(last, '%d.%m.%Y %H:%M:%S').replace(tzinfo=UTC).timestamp()
            return (start, end) if end >= start else (None, None)
        except (ValueError, OverflowError):
            return None, None
    return None, None


def public_asset(value):
    return '/' + value if isinstance(value, str) and ASSET.fullmatch(value) else None


ASSET_REVISION = 'satellite/2'


def enrich(entry, passport, destination, index):
    """Worker-only derivative creation. Dimensions describe actual files, never a viewport.

    Original image and sidecar bytes are preserved. The smaller preview and blurred
    ambient are separately identified; metadata mismatches are explicit warnings.
    """
    from PIL import Image, ImageEnhance, ImageFilter
    stem = '{0:03d}'.format(index)
    with Image.open(str(destination / (stem + '-preview.jpg'))) as preview:
        entry['display_width'], entry['display_height'] = preview.size
        ambient = preview.convert('RGB')
        ambient.thumbnail((640, 360))
        # Previously 27% brightness was dimmed by CSS a second time: almost black.
        ambient = ImageEnhance.Color(ambient).enhance(1.35)
        ambient = ImageEnhance.Brightness(ambient).enhance(0.85)
        ambient = ambient.filter(ImageFilter.GaussianBlur(radius=18))
        entry['ambient_width'], entry['ambient_height'] = ambient.size
        fd, name = tempfile.mkstemp(prefix='.ambient-', dir=str(destination))
        try:
            with os.fdopen(fd, 'wb') as stream:
                os.fchmod(stream.fileno(), 0o644)
                ambient.save(stream, 'JPEG', quality=78)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(name, str(destination / (stem + '-ambient-2.jpg')))
        finally:
            if os.path.exists(name):
                os.unlink(name)
    entry['ambient'] = entry['preview'].replace('-preview.jpg', '-ambient-2.jpg')
    # Do not trust stale item.json dimensions during migration of existing products.
    with Image.open(str(destination / os.path.basename(entry['original']))) as original:
        entry['width'], entry['height'] = original.size
    start, end = observation_interval(passport)
    entry['acquisition_start_utc'] = iso(start) if start is not None else None
    entry['acquisition_end_utc'] = iso(end) if end is not None else None
    entry['legend'] = copy.deepcopy(passport.get('legend', {})) if isinstance(passport.get('legend'), dict) else {}
    orientation = passport.get('orientation', {})
    entry['orientation'] = copy.deepcopy(orientation) if isinstance(orientation, dict) else {}
    entry['pass_direction'] = entry['orientation'].get('pass_direction', '')
    info = passport.get('pass', {})
    info = info if isinstance(info, dict) else {}
    product = passport.get('product', {})
    product = product if isinstance(product, dict) else {}
    entry['product'] = {
        'code': product.get('code', passport.get('product_code', entry.get('title', ''))),
        'title': info.get('product', product.get('title', entry.get('title', ''))),
        'description': product.get('description', passport.get('description', entry['legend'].get('subtitle', ''))),
        'purpose': product.get('purpose', passport.get('purpose', ''))
    }
    entry['details'] = copy.deepcopy(info.get('details', [])) if isinstance(info.get('details', []), list) else []
    entry['quality'] = {'label': info.get('quality', ''), 'detail': info.get('quality_detail', '')}
    layers = passport.get('layers', entry['legend'].get('components', []))
    entry['layers'] = copy.deepcopy(layers) if isinstance(layers, list) else []
    entry['metadata_warnings'] = []
    output = passport.get('output', {})
    if isinstance(output, dict) and ('width' in output or 'height' in output):
        if (output.get('width'), output.get('height')) != (entry['width'], entry['height']):
            entry['metadata_warnings'].append('Размер в паспорте не совпадает с файлом; показан фактический размер файла.')
    entry['asset_revision'] = ASSET_REVISION
    return entry


def frame(entry, mode='original'):
    """Publication is the image authority. No upper width/height limit in the UI contract."""
    start, end = utc_seconds(entry.get('acquisition_start_utc')), utc_seconds(entry.get('acquisition_end_utc'))
    known = start is not None and end is not None and end >= start
    preview_mode = mode == 'preview'
    width = entry.get('display_width' if preview_mode else 'width')
    height = entry.get('display_height' if preview_mode else 'height')
    display = public_asset(entry.get('preview' if preview_mode else 'original'))
    if not display or type(width) is not int or type(height) is not int or width <= 0 or height <= 0:
        return None, 'invalid_asset'
    product = entry.get('product', {'code': entry.get('title', ''), 'title': entry.get('title', ''),
                                  'description': entry.get('legend', {}).get('subtitle', ''), 'purpose': ''})
    return {
        'id': entry['id'], 'start': iso(start) if known else None, 'end': iso(end) if known else None,
        'timeKnown': known, 'publishedAt': iso(entry['published_at']),
        'eventId': str(entry.get('satellite', '')) + ':' + (iso(start) if known else entry['id']),
        'satellite': entry.get('satellite', ''), 'instrument': entry.get('instrument', ''),
        'product': product, 'display': display, 'ambient': public_asset(entry.get('ambient')),
        'image': {'url': display, 'width': width, 'height': height, 'mode': mode},
        'originalImage': {'url': public_asset(entry.get('original')), 'width': entry.get('width'), 'height': entry.get('height')},
        'previewImage': {'url': public_asset(entry.get('preview')), 'width': entry.get('display_width'), 'height': entry.get('display_height')},
        'ambientImage': {'url': public_asset(entry.get('ambient')), 'width': entry.get('ambient_width'), 'height': entry.get('ambient_height')},
        'width': width, 'height': height, 'pass': entry.get('pass_direction', ''),
        'layout': entry.get('layout', 'external'), 'legend': entry.get('legend', {}),
        'layers': entry.get('layers', []), 'details': entry.get('details', []),
        'quality': entry.get('quality', {}), 'orientation': entry.get('orientation', {}),
        'metadataWarnings': entry.get('metadata_warnings', []),
        'metadata': public_asset(entry.get('metadata')), 'original': public_asset(entry.get('original'))
    }, None


def publication(entries, board_settings, revision, now):
    frames, rejected = [], {'invalid_asset': 0}
    display = display_settings(board_settings.get('display'))
    for entry in entries:
        item, error = frame(entry, display['imageMode'])
        if error:
            rejected[error] += 1
        else:
            frames.append(item)
    frames.sort(key=lambda x: (x['start'] or x['publishedAt'], x['id']))
    return {'schema': 'satdump.satellite.publication/1', 'updated_at': now,
            'settings_revision': revision, 'display': display, 'frames': frames, 'excluded': rejected}


def manifest(publication, now):
    """Server chooses the period and order. Undated images are shown, never placed on a false UTC mark."""
    display = display_settings(publication.get('display'))
    cutoff = now - display['windowHours'] * 3600
    frames = []
    excluded = copy.deepcopy(publication.get('excluded', {}))
    excluded.update(expired=0, future=0)
    for item in publication.get('frames', []):
        start, end = utc_seconds(item.get('start')), utc_seconds(item.get('end'))
        if start is None or end is None or end < start:
            # Publication age is only a retention criterion, never an observation timestamp.
            published = utc_seconds(item.get('publishedAt'))
            if published is not None and published >= cutoff:
                frames.append(copy.deepcopy(item))
        elif end < cutoff:
            excluded['expired'] += 1
        elif start > now:
            excluded['future'] += 1
        else:
            frames.append(copy.deepcopy(item))
    priorities = display['productPriority']
    def rank(item):
        code = str(item.get('product', {}).get('code', '')).lower()
        return priorities.index(code) if code in priorities else len(priorities)
    frames.sort(key=lambda x: (x.get('start') or x.get('publishedAt', ''), x.get('eventId', ''), rank(x), x.get('id', '')))
    newest = max([utc_seconds(f.get('end')) or 0 for f in frames] or [0])
    for item in frames:
        # Rebuild a per-response object; do not mutate publication / disk documents.
        item['latest'] = bool(newest and utc_seconds(item.get('end')) == newest)
    return {'schema': 'meteo-orbit.manifest/1', 'generatedAt': iso(publication['updated_at']),
            'settings_revision': publication.get('settings_revision'), 'windowHours': display['windowHours'],
            'display': display, 'excluded': excluded, 'undatedCount': sum(1 for f in frames if not f.get('start')),
            'frames': frames}
