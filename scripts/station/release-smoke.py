#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Installed-package UTC and gallery checks on an explicitly disposable CI VM."""
from __future__ import print_function
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time
from urllib.request import urlopen
from urllib.error import URLError


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected-sha', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    require(os.geteuid() == 0 and os.environ.get('SATDUMP_DISPOSABLE_CI') == '1',
            'Run only as root on the disposable acceptance VM')
    require(Path('/run/systemd/system').is_dir(), 'A running systemd is required')
    require(re.match(r'^[0-9a-f]{40}$', args.expected_sha), 'Expected full Git SHA')
    package = Path('/opt/satdump-station/current').resolve()
    manifest = json.loads((package / 'PACKAGE-MANIFEST.json').read_text())
    require(manifest['git_commit'] == args.expected_sha, 'Installed package revision mismatch')
    require(manifest.get('bundled_gallery_included') is True, 'Missing gallery package contract')
    report = {'schema': 'satdump.station.release-smoke/1', 'git_commit': args.expected_sha,
              'success': False, 'checks': [], 'native_radio_decoding': False,
              'target_astra_acceptance': False}

    def passed(name):
        report['checks'].append(name)
        print('PASS: ' + name, flush=True)

    def get(path, decode=True):
        with urlopen('http://127.0.0.1:8090' + path, timeout=5) as response:
            body = response.read(16 * 1024 * 1024)
            return json.loads(body.decode('utf-8')) if decode else body

    def wait_for(predicate, message):
        end = time.monotonic() + 90
        while time.monotonic() < end:
            try:
                value = predicate()
                if value:
                    return value
            except (OSError, ValueError, KeyError, URLError):
                pass
            time.sleep(1)
        raise RuntimeError(message)

    try:
        page = get('/', False).lower()
        require(b'<!doctype html>' in page, 'Root URL is not a gallery page')
        for path in ('/app.js', '/style.css'):
            require(bool(get(path, False)), 'Missing local gallery resource: ' + path)
        script = get('/app.js', False)
        require(b'api/v1/board' in script and b"timeZone: 'UTC'" in script,
                'Gallery does not use BOARD and UTC')
        require(not re.search(br'<(?:script|link)\b[^>]*(?:src|href)=[\"\'](?:https?:)?//', page),
                'Gallery depends on an external script or stylesheet')
        passed('installed_gallery_and_local_resources')
        for unit in ('satdump-worker', 'satdump-web', 'satdump-control'):
            environment = subprocess.check_output(['systemctl', 'show', unit, '-p', 'Environment']).decode()
            require('TZ=UTC' in environment, 'Missing UTC service environment: ' + unit)
        env = os.environ.copy()
        env['TZ'] = 'Pacific/Honolulu'
        clock = json.loads(subprocess.check_output([str(package / 'station.sh'), 'time-status'], env=env).decode())
        require(clock['display_timezone'] == 'UTC', 'Clock diagnostic uses local time')
        require(clock['status'] in ('synchronized', 'unsynchronized', 'unknown'), 'Invalid synchronization status')
        report['host_clock'] = clock
        passed('services_and_clock_diagnostic_use_utc')

        from PIL import Image
        inbox = Path('/var/lib/satdump-station/inbox/images')
        cases = [
            ('offset', '2026-09-20T02:30:00+03:00', '2026-09-19T23:30:00Z', '2026-09-19T23:30:00Z', 'ok'),
            ('midnight', '19.09.2026 23:55:00 – 20.09.2026 00:10:00 UTC', '2026-09-19T23:55:00Z', '2026-09-20T00:10:00Z', 'ok'),
            ('missing-zone', '2026-09-20T02:30:00', None, None, 'invalid')]
        passports = {}
        for i, (name, raw, start, end, status) in enumerate(cases):
            title = 'release-check-' + name
            path = inbox / (title + '.png')
            require(not path.exists(), 'Fixture already exists; use a clean VM')
            metadata = {'schema': 'satdump.presentation/2', 'layout': 'minimal',
                        'pass': {'satellite': 'CI test fixture', 'product': title, 'acquisition_time': raw}}
            path.with_suffix('.json').write_text(json.dumps(metadata))
            pending = inbox / (title + '.png.part')
            Image.new('RGB', (32, 24), (100 + i, 120, 140)).save(str(pending), 'PNG')
            os.utime(str(pending), (946684800, 946684800))
            os.replace(str(pending), str(path))
            Path(str(path) + '.ready').touch()
            passports[title] = metadata

        def published():
            catalogue = get('/api/v1/board')
            selected = [e for e in catalogue['items'] if e['title'] in passports]
            return selected if len(selected) == len(cases) else None

        entries = wait_for(published, 'UTC fixtures were not published')
        for name, raw, start, end, status in cases:
            title = 'release-check-' + name
            entry = next(e for e in entries if e['title'] == title)
            require(entry['acquisition_start_utc'] == start and entry['acquisition_end_utc'] == end,
                    'UTC interval mismatch: ' + name)
            require(entry['acquisition_time_status'] == status and entry['acquisition_time_raw'] == raw,
                    'Acquisition status mismatch: ' + name)
            require(get('/' + entry['metadata']) == passports[title], 'Source passport changed')
            require(entry['file_mtime'] == 946684800, 'File mtime was used as observation time or changed')
        require([e['title'] for e in entries] == ['release-check-midnight', 'release-check-offset', 'release-check-missing-zone'],
                'Catalogue is not sorted by observation time, with unknown times last')
        passed('utc_offset_midnight_invalid_time_and_catalogue_order')
        before = {e['id'] for e in entries}
        subprocess.check_call(['systemctl', 'restart', 'satdump-worker', 'satdump-web', 'satdump-control'])
        wait_for(lambda: get('/health/ready').get('ready'), 'Worker did not resume')
        time.sleep(3)
        after = wait_for(published, 'Publications disappeared after restart')
        require({e['id'] for e in after} == before, 'Restart duplicated or replaced publications')
        health = get('/health.json')
        require(health['worker_alive'] and health['timezone'] == 'UTC' and health.get('boot_id')
                and isinstance(health.get('updated_monotonic'), (int, float)), 'Missing monotonic heartbeat')
        passed('restart_preserves_ids_and_monotonic_heartbeat')
        report['success'] = True
    finally:
        Path(args.output).write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
