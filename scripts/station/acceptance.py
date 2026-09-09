#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Destructive installation acceptance ONLY on an explicitly disposable CI VM.

Run with the packaged Python, root and SATDUMP_DISPOSABLE_CI=1. Uses real
systemd/services and HTTP; input fixture is a synthetic PNG, not radio data.
"""
from __future__ import print_function
import argparse
import copy
import hashlib
import json
import os
import socket
import subprocess
import time
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


def wait_for(test, message, timeout=80):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            value = test()
            if value:
                return value
        except (OSError, ValueError, KeyError, URLError):
            pass
        time.sleep(1)
    raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    if os.geteuid() != 0 or os.environ.get('SATDUMP_DISPOSABLE_CI') != '1':
        parser.error('Only root on a disposable CI VM with SATDUMP_DISPOSABLE_CI=1')
    if not Path('/run/systemd/system').is_dir():
        parser.error('A real running systemd is required; no service mocks')
    package = Path(args.package).resolve()
    if not (package / 'PACKAGE-MANIFEST.json').is_file():
        parser.error('A complete binary package is required')
    if Path('/etc/satdump-station').exists() or Path('/opt/satdump-station/current').exists():
        parser.error('Refusing to alter an existing installation')
    report = {'schema': 'satdump.station.acceptance/1', 'checks': [],
              'host': 'disposable Ubuntu VM, real systemd',
              'native_astra16_acceptance': False,
              'input': 'synthetic PNG and presentation/2 sidecar; not a radio recording'}
    with (package / 'PACKAGE-MANIFEST.json').open() as stream:
        report['package'] = json.load(stream)
    data = Path('/var/lib/satdump-station')
    prefix = Path('/opt/satdump-station')
    token = ''

    def passed(name):
        report['checks'].append(name)
        print('PASS: ' + name, flush=True)

    def command(argv, success=True):
        rc = subprocess.call([str(x) for x in argv])
        if (rc == 0) != success:
            raise RuntimeError('Unexpected command result: ' + str(argv[0]) + ': ' + str(rc))

    def install(*options, **kw):
        command(['/bin/bash', package / 'install.sh', '--allow-compatible', '--yes', '--no-animation'] + list(options), kw.get('success', True))

    def request(path, payload=None, etag=None, method=None, auth=True):
        headers = {'Authorization': 'Bearer ' + token} if auth else {}
        body = None
        if payload is not None:
            body = json.dumps(payload).encode('utf-8')
            headers['Content-Type'] = 'application/json'
        if etag:
            headers['If-Match'] = '"' + etag + '"'
        port = 8091 if path.startswith('/api/v1/control/') else 8090
        req = Request('http://127.0.0.1:' + str(port) + path, body, headers, method=method)
        with urlopen(req, timeout=5) as response:
            raw = response.read()
            return json.loads(raw.decode('utf-8')) if 'json' in response.headers.get('Content-Type', '') else raw

    def services():
        for unit in ('satdump-worker', 'satdump-web', 'satdump-control'):
            command(['systemctl', 'is-active', '--quiet', unit])
            command(['systemctl', 'is-enabled', '--quiet', unit])
        wait_for(lambda: request('/health.json', auth=False).get('worker_alive'), 'Unhealthy worker/WEB')
        wait_for(lambda: request('/api/v1/control/health').get('control_alive'), 'Unhealthy API')

    try:
        install()
        with open('/etc/satdump-station/control.token') as stream:
            token = stream.read().strip()
        services()
        passed('install_and_enable_worker_web_control')
        caps = request('/api/v1/control/capabilities')
        if not caps['instruments']:
            raise RuntimeError('Missing native instrument/preset registry in binary package')
        try:
            request('/api/v1/control/config', auth=False)
        except HTTPError as error:
            if error.code != 401:
                raise
        else:
            raise RuntimeError('Control API allowed an unauthenticated request')
        passed('real_engine_capabilities_and_http_authentication')
        for private in ('/etc/satdump-station/control.token', '/etc/satdump-station/station.json', str(data / 'state/queue.sqlite3')):
            command(['runuser', '-u', 'satdump-web', '--', 'test', '-r', private], False)
        passed('web_user_cannot_read_token_configuration_or_queue')
        doc = request('/api/v1/control/config')
        settings = copy.deepcopy(doc['settings'])
        settings['station'].update(poll_seconds=1, settle_seconds=0)
        saved = request('/api/v1/control/config', {'settings': settings}, doc['revision'], 'PUT')
        wait_for(lambda: request('/api/v1/board/status', auth=False)['applied_revision'] == saved['revision'], 'Revision not applied')
        passed('authenticated_config_apply_between_jobs')
        from PIL import Image
        image = data / 'inbox/images/acceptance.png'
        Image.new('RGB', (32, 24), (100, 120, 140)).save(str(image))
        metadata = {'schema': 'satdump.presentation/2', 'layout': 'minimal',
                    'pass': {'satellite': 'CI fixture', 'instrument': 'synthetic',
                             'product': 'Deployment acceptance', 'acquisition_time': '2000-01-01T00:00:00Z'},
                    'orientation': {'north_up_requested': True, 'north_up_verified': False}}
        # Input mtime is deliberately different from the observation time.
        with open(str(image.with_suffix('.json')), 'w') as stream:
            json.dump(metadata, stream)
        def find_entry():
            return next((i for i in request('/api/v1/board', auth=False)['items'] if i['title'] == 'Deployment acceptance'), None)
        entry = wait_for(find_entry, 'PNG not published through real worker/WEB')
        if request('/' + entry['metadata'], auth=False) != metadata or entry['layout'] != 'minimal':
            raise RuntimeError('Presentation passport was changed')
        raw = request('/' + entry['original'], auth=False)
        if hashlib.sha256(raw).digest() != hashlib.sha256(image.read_bytes()).digest():
            raise RuntimeError('Published original bytes differ')
        passed('worker_png_board_metadata_end_to_end')
        revision = saved['revision']
        install()
        services()
        if request('/api/v1/control/config')['revision'] != revision:
            raise RuntimeError('Reinstall changed managed settings')
        passed('repeat_install_preserves_configuration_and_data')
        if Path('/usr/sbin/nginx').is_file():
            install('--web-server', 'nginx')
            services()
            command(['systemctl', 'is-active', '--quiet', 'satdump-board'])
            if not find_entry():
                raise RuntimeError('nginx cannot access BOARD')
            command([prefix / 'current/station.sh', 'rollback', '--allow-compatible'])
            services()
            if not find_entry():
                raise RuntimeError('Rollback lost publications')
            passed('nginx_real_start_and_rollback_to_builtin')
        else:
            raise RuntimeError('Acceptance VM must provide nginx')
        blocker = socket.socket()
        blocker.bind(('127.0.0.1', 18094))
        blocker.listen(1)
        try:
            install('--port', '18094', '--source-path', '/srv/station-ci-receiver', success=False)
        finally:
            blocker.close()
        services()
        if request('/api/v1/control/config')['revision'] != revision:
            raise RuntimeError('Failed activation did not restore managed API settings')
        passed('occupied_port_restores_code_units_and_managed_settings')
        command([prefix / 'current/station.sh', 'restart'])
        wait_for(lambda: request('/health.json', auth=False).get('worker_alive'), 'Services did not resume')
        if not find_entry():
            raise RuntimeError('Restart lost publications')
        passed('restart_preserves_queue_and_publication')
        report['success'] = True
    finally:
        report.setdefault('success', False)
        with open(args.output, 'w') as stream:
            json.dump(report, stream, ensure_ascii=False, indent=2)
        subprocess.call(['journalctl', '-u', 'satdump-worker', '-u', 'satdump-web', '-u', 'satdump-control', '-u', 'satdump-board', '--no-pager', '-n', '160'])
    if not report['success']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
