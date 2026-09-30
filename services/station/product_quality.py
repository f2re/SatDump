# -*- coding: utf-8 -*-
"""Per-instrument checks using decoder receipts and actual published assets."""
import re
from pathlib import Path

ID = re.compile(r'^[a-zA-Z0-9_-]{1,64}$')


def validate_required(source):
    required = source.get('required_instruments', [])
    if (not isinstance(required, list) or len(required) > 32 or
            any(not isinstance(v, str) or not ID.match(v) for v in required) or
            len(set(required)) != len(required)):
        raise ValueError('required_instruments: expected unique instrument identifiers')
    if source.get('kind') == 'image' and required:
        raise ValueError('required_instruments needs a product or pipeline source')
    return required


def assess(root, source, images, read_json, native_errors=None):
    required = validate_required(source)
    rows, warnings = {}, []
    if source['kind'] == 'image':
        return {'status': 'external', 'instruments': [], 'warnings': [], 'missing_required': []}

    def row_for(identifier):
        if not isinstance(identifier, str) or not ID.match(identifier):
            raise ValueError('Invalid instrument identifier in processing report')
        return rows.setdefault(identifier, {'id': identifier, 'status': 'ok', 'images': 0})

    def receipt(path, schema):
        if path.is_symlink():
            raise ValueError('Symlink processing report')
        data = read_json(path)
        if not isinstance(data, dict) or data.get('schema') != schema:
            raise ValueError('Invalid processing report schema')
        return data

    for path in sorted(Path(root).rglob('decode-status.json')):
        report = receipt(path, 'satdump.decode-status/1')
        if report.get('trailing_bytes', 0):
            warnings.append('incomplete_input_frame')
        for entry in report.get('instruments', []):
            row = row_for(entry.get('instrument'))
            status = entry.get('status')
            if status not in ('ok', 'partial', 'no_data'):
                raise ValueError('Invalid decoder status')
            lines = entry.get('lines', 0)
            if type(lines) is not int or not 0 <= lines <= 100000000:
                raise ValueError('Invalid decoded line count')
            if status != 'ok':
                row['status'] = status
                warnings.append(row['id'] + ':' + status)
            row['lines'] = row.get('lines', 0) + lines
    for path in sorted(Path(root).rglob('processing-status.json')):
        report = receipt(path, 'satdump.processing-status/1')
        row = row_for(report.get('instrument'))
        row['images'] += sum(p.parent == path.parent for p in images)
        if report.get('status') not in ('ok', 'partial', 'no_products'):
            raise ValueError('Invalid processing status')
        if report['status'] != 'ok' or not report.get('generated', 0):
            row['status'] = 'partial' if row['images'] else 'no_products'
            warnings.append(row['id'] + ':' + row['status'])
    for path in sorted(Path(root).rglob('dataset-processing.json')):
        report = receipt(path, 'satdump.dataset-processing/1')
        if any(v.get('status') == 'failed' for v in report.get('products', [])):
            warnings.append('instrument_processing_failed')
    if native_errors:
        warnings.append('native_processing_error')
    for row in rows.values():
        if not row['images']:
            warnings.append(row['id'] + ':no_presentation')
    missing = sorted(v for v in required if not rows.get(v, {}).get('images'))
    warnings.extend(v + ':required_missing' for v in missing)
    if not rows:
        warnings.append('instrument_completeness_unverified')
    return {'status': 'partial' if warnings else 'ok',
            'instruments': sorted(rows.values(), key=lambda v: v['id']),
            'warnings': sorted(set(warnings)), 'missing_required': missing}


def publication_complete(public, job_id, read_json):
    root = Path(public) / 'items' / job_id
    try:
        if root.is_symlink() or (root / 'item.json').is_symlink():
            return False
        manifest = read_json(root / 'item.json')
        if manifest.get('job_id') != job_id or not manifest.get('entries'):
            return False
        for entry in manifest['entries']:
            for key in ('original', 'preview', 'thumbnail', 'metadata'):
                value = entry[key]
                pattern = r'^items/' + re.escape(job_id) + r'/[0-9]{3}(?:-preview|-thumb)?\.(png|jpg|json)$'
                if not isinstance(value, str) or not re.match(pattern, value):
                    return False
                asset = Path(public) / value
                if asset.is_symlink() or not asset.is_file() or asset.stat().st_size == 0:
                    return False
        return True
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return False
