#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Synthetic transport/product regressions using the real built SatDump CLI."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from PIL import Image


def cbor(value):
    def head(kind, size):
        if size < 24:
            return bytes([(kind << 5) | size])
        if size < 256:
            return bytes([(kind << 5) | 24, size])
        if size < 65536:
            return bytes([(kind << 5) | 25]) + struct.pack('>H', size)
        return bytes([(kind << 5) | 26]) + struct.pack('>I', size)
    if value is False: return b'\xf4'
    if value is True: return b'\xf5'
    if isinstance(value, int): return head(0, value) if value >= 0 else head(1, -1 - value)
    if isinstance(value, str):
        data = value.encode('utf-8'); return head(3, len(data)) + data
    if isinstance(value, list): return head(4, len(value)) + b''.join(cbor(v) for v in value)
    if isinstance(value, dict): return head(5, len(value)) + b''.join(cbor(k) + cbor(v) for k, v in value.items())
    raise TypeError(type(value))


def hrpt(scans):
    data = bytearray()
    for value, skipped in scans:
        for counter in range(2, 27):
            if counter == skipped: continue
            frame = bytearray(248)
            frame[:4] = bytes.fromhex('fb386a45')
            frame[4], frame[5] = 255, counter
            for offset in (8, 128):
                for n in range(59):
                    sample = 32768 + value + counter * 9 + n
                    frame[offset + n * 2:offset + n * 2 + 2] = struct.pack('<H', sample)
            data.extend(frame)
    data.extend(bytes(256))
    result = bytearray()
    for start in range(0, len(data), 32):
        fragment = data[start:start + 32]; fragment.extend(bytes(32 - len(fragment)))
        frame = bytearray(1024)
        for i, offset in enumerate((14, 270, 526, 782)):
            frame[offset:offset + 8] = fragment[i * 8:i * 8 + 8]
        result.extend(frame)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    engine = str(Path(args.engine).resolve())
    root = Path(args.output).resolve(); root.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy(); env.update(HOME=str(root / 'home'), XDG_CONFIG_HOME=str(root / 'home/.config'), TZ='UTC', OMP_NUM_THREADS='1')
    patch = root / 'processing.json'
    patch.write_text(json.dumps({'satdump_general': {'tle_update_interval': {'value': 'Never'},
        'log_to_file': {'value': False}, 'auto_process_products': {'value': True},
        'image_format': {'value': 'png'}, 'product_format': {'value': 'png'},
        'presentation_enabled': {'value': True}, 'presentation': {'enabled': True,
            'save_presentation': True, 'save_minimal': True, 'north_up': False}}}))
    results = []
    def run(name, argv, success):
        with (root / (name + '.log')).open('wb') as out:
            process = subprocess.run([engine] + argv, cwd=str(root), env=env,
                stdout=out, stderr=subprocess.STDOUT, timeout=120)
        assert (process.returncode == 0) == success, (name, process.returncode)
        results.append({'case': name, 'exit_code': process.returncode})
    def decode(name, payload, success, pipeline='meteor_hrpt', level='cadu', options=None):
        raw = root / (name + '.bin'); raw.write_bytes(payload)
        output = root / name
        run(name, [pipeline, level, str(raw), str(output)] + (options or []) +
            ['--offline', '--processing_config', str(patch)], success)
        return output
    empty = decode('no-data', bytes(4 * 1024) + b'partial', False)
    assert not list(empty.rglob('product.cbor'))
    assert json.loads((empty / 'decode-status.json').read_text())['trailing_bytes'] == 7
    valid = decode('recovered-scans', hrpt([(111, 0), (999, 13), (222, 0)]), True)
    product = valid / 'MTVZA'
    assert len(list(product.glob('MTVZA-*.png'))) == 30
    with Image.open(str(product / 'MTVZA-1.png')) as image:
        assert image.size == (100, 2), image.size
    presentations = list(product.glob('*_annotated_presentation.png'))
    assert len(presentations) >= 5, len(presentations)
    report = json.loads((product / 'processing-status.json').read_text())
    assert report['generated'] >= 5 and report['instrument'] == 'mtvza', report
    decoded = json.loads((valid / 'decode-status.json').read_text())
    microwave = next(v for v in decoded['instruments'] if v['instrument'] == 'mtvza')
    assert microwave['lines'] == 2 and microwave['incomplete_scans'] == 1, microwave
    for name in ('Rainfall', 'Sea_Ice', 'Vegetation', 'Soil_Moisture', 'Microwave_Airmass'):
        files = list(product.glob('*' + name + '*_annotated_presentation.json'))
        assert files, name
        passport = json.loads(files[0].read_text())
        assert not passport['legend'].get('categories'), passport
        assert 'Эксперимент' in json.dumps(passport, ensure_ascii=False), passport
    # Incomplete product and absent files must yield non-zero, not false success.
    missing = root / 'missing-channels'; missing.mkdir()
    metadata = {'instrument': 'mtvza', 'type': 'image', 'bit_depth': 16,
        'has_timestamps': False, 'needs_correlation': False,
        'images': [{'file': 'MTVZA-1.png', 'name': '1', 'ifov_x': -1, 'ifov_y': -1}]}
    (missing / 'product.cbor').write_bytes(cbor(metadata))
    run('missing-channels', ['reprocess', str(missing), str(patch)], False)
    assert json.loads((missing / 'processing-status.json').read_text())['status'] == 'no_products'
    # A repeated terminator cannot create an X-band line, including at EOF.
    terminator = bytearray(380); terminator[4] = 51
    dump = decode('dump-no-data', bytes(terminator) * 2 + b'tail', False,
        'meteor_m_mtvza_dump', 'frm', ['--satellite_number', 'M2-4'])
    assert not list(dump.rglob('product.cbor'))
    dump_data = bytearray()
    for counter in range(52):
        frame = bytearray(380); frame[4] = counter
        for n in range(184): frame[10 + 2 * n:12 + 2 * n] = struct.pack('>H', 12000 + n)
        dump_data.extend(frame)
    full_dump = decode('dump-valid', dump_data, True, 'meteor_m_mtvza_dump', 'frm', ['--satellite_number', 'M2-2'])
    assert len(list((full_dump / 'MTVZA').glob('MTVZA-*.png'))) == 46
    assert json.loads((full_dump / 'dataset.json').read_text())['timestamp'] == -1
    (root / 'MTVZA-TEST.json').write_text(json.dumps({'success': True, 'data': 'synthetic', 'cases': results}, indent=2))
    print('MTVZA native regressions passed:', len(results))


if __name__ == '__main__':
    main()
