#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Root-side setup/rendering and read-only diagnostics. No package manager calls."""
from __future__ import print_function
import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'services/station'))
import control


def provision(package, config, data, source_path='', source_kind='image'):
    """Only explicitly requested values change; existing root config is preserved."""
    config, data, package = Path(config), Path(data), Path(package)
    config.mkdir(parents=True, exist_ok=True)
    station_path = config / 'station.json'
    if station_path.exists():
        station = control.read(station_path)
        control.require(station['data_dir'] == str(data), 'Для существующего data_dir нужна отдельная миграция')
    else:
        station = control.read(package / 'config/station/station.json')
        old = station['data_dir']
        station['data_dir'] = str(data)
        for source in station['sources']:
            source['path'] = str(data) + source['path'][len(old):]
        control.atomic(station_path, station)
    if source_path:
        control.clean_path(source_path)
        control.require(source_kind in ('image', 'product'), 'Источник мастера: image или product')
        source = {'id': 'receiver', 'kind': source_kind, 'enabled': True,
                  'path': source_path, 'require_ready': True}
        if source_kind == 'image':
            source['patterns'] = ['*.png', '*.jpg', '*.jpeg']
        station['sources'] = [s for s in station['sources'] if s['id'] != 'receiver'] + [source]
        control.atomic(station_path, station)
    processing = config / 'processing.json'
    if not processing.exists():
        control.atomic(processing, control.read(package / 'config/station/processing.json'))
    policy_path = config / 'control-policy.json'
    if policy_path.exists():
        policy = control.read(policy_path)
    else:
        policy = {'schema': 'satdump.station.policy/1', 'input_roots': [],
                  'pipeline_ids': [], 'admin_origins': []}
    roots = [s['path'] for s in station['sources']]
    policy['input_roots'] = sorted(set(policy['input_roots'] + roots))
    policy['pipeline_ids'] = sorted(set(policy['pipeline_ids'] + [s['pipeline'] for s in station['sources'] if s['kind'] == 'pipeline']))
    control.atomic(policy_path, policy)
    for name in ('state', 'work', 'logs', 'archive', 'public/items', 'control/revisions', 'inbox/images', 'inbox/products', 'inbox/meteor_iq'):
        (data / name).mkdir(parents=True, exist_ok=True)
    store = control.Store(station_path, package / 'engine')
    # A previous active revision wins. Explicit new source parameters are merged,
    # not silently hidden behind the old managed document.
    if source_path and (store.root / 'active.json').exists():
        doc = store.current()
        settings = doc['settings']
        settings['station']['sources'] = [s for s in settings['station']['sources'] if s['id'] != 'receiver'] + [source]
        code, result = store.save(settings, doc['revision'], True)
        control.require(code in (200, 202), 'Не удалось применить источник')
    store.validate(store.current()['settings'])
    token_path = config / 'control.token'
    if not token_path.exists():
        with open(str(token_path), 'x') as stream:
            os.chmod(str(token_path), 0o600)
            stream.write(os.urandom(32).hex() + '\n')
    return store.current()['revision']


def unit_files(prefix, config, data, host, port, control_port, backend, web_server):
    # These strings feed systemd/nginx. Keep them narrower than filesystem paths.
    for path in (prefix, config, data):
        control.clean_path(path)
    socket.inet_pton(socket.AF_INET, host)
    for number in (port, control_port, backend):
        control.number(number, 1024, 65535, True)
    control.require(len({port, control_port, backend}) == 3, 'Порты должны различаться')
    launcher = prefix + '/current/station.sh'
    common = '\nRestart=on-failure\nRestartSec=5\nNoNewPrivileges=true\nPrivateTmp=true\nPrivateDevices=true\nProtectSystem=full\nReadOnlyDirectories=/\n'
    def service(description, user, command, extras='', after='local-fs.target network.target'):
        return ('[Unit]\nDescription=' + description + '\nAfter=' + after + '\nRequiresMountsFor=' + data + '\n'
                '[Service]\nType=simple\nUser=' + user + '\nGroup=' + user + '\nExecStart=' + command + common + extras +
                '\n[Install]\nWantedBy=multi-user.target\n')
    worker = service('SatDump processing and BOARD publisher', 'satdump-station',
                     launcher + ' worker --config ' + config + '/station.json',
                     'SupplementaryGroups=satdump-config\nPrivateNetwork=true\nUMask=0022\n'
                     'TimeoutStopSec=30\nKillMode=control-group\nNice=10\nWorkingDirectory=' + data + '/work\n'
                     'ReadWriteDirectories=' + ' '.join(data + '/' + x for x in ('state', 'work', 'logs', 'archive', 'public')) + '\n')
    control_unit = service('SatDump authenticated control API (loopback only)', 'satdump-control',
                           launcher + ' control --config ' + config + '/station.json --port ' + str(control_port) +
                           ' --token-file ' + config + '/control.token',
                           'SupplementaryGroups=satdump-config\nUMask=0027\nProtectHome=true\n'
                           'ReadWriteDirectories=' + data + '/control\n'
                           'InaccessibleDirectories=' + ' '.join(data + '/' + x for x in ('state', 'work', 'logs', 'archive', 'inbox')) + '\n')
    listen_host, listen_port = (host, port) if web_server == 'builtin' else ('127.0.0.1', backend)
    board_unit = service('SatDump read-only BOARD HTTP', 'satdump-web',
                         launcher + ' serve --public ' + data + '/public --host ' + listen_host + ' --port ' + str(listen_port) + ' --ui-root ' + prefix + '/board-ui/current',
                         'ProtectHome=true\n')
    result = {'satdump-worker.service': worker, 'satdump-control.service': control_unit}
    if web_server == 'builtin':
        result['satdump-web.service'] = board_unit
    else:
        result['satdump-board.service'] = board_unit
        result['satdump-web.service'] = service('SatDump dedicated nginx frontend', 'satdump-web',
                         '/usr/sbin/nginx -c ' + config + '/nginx.conf -g "daemon off;"',
                         'RuntimeDirectory=satdump-web\nReadWriteDirectories=/run/satdump-web\nProtectHome=true\n',
                         'local-fs.target network.target satdump-board.service')
    return result


def nginx_config(config, host, port, backend):
    return '''# Dedicated unprivileged instance; does not include or alter /etc/nginx.
worker_processes 1;
pid /run/satdump-web/nginx.pid;
error_log stderr warn;
events { worker_connections 128; }
http {
    access_log off;
    server_tokens off;
    client_max_body_size 1m;
    client_body_temp_path /run/satdump-web/body;
    proxy_temp_path /run/satdump-web/proxy;
    fastcgi_temp_path /run/satdump-web/fastcgi;
    uwsgi_temp_path /run/satdump-web/uwsgi;
    scgi_temp_path /run/satdump-web/scgi;
    server {
        listen %s:%s;
        server_name _;
        location /api/v1/control/ { return 404; }
        location / {
            proxy_pass http://127.0.0.1:%s;
            proxy_set_header Host 127.0.0.1:%s;
            proxy_buffering off;
            proxy_connect_timeout 5s;
            proxy_read_timeout 30s;
        }
    }
}
''' % (host, port, backend, backend)


def doctor(config):
    checks = []
    def record(name, ok, detail):
        checks.append({'check': name, 'ok': bool(ok), 'detail': detail})
    try:
        store = control.Store(config)
        settings = store.current()['settings']
        store.validate(settings)
        record('configuration', True, 'Структура и root-политика согласованы')
        for source in settings['station']['sources']:
            if source.get('enabled', True):
                path = source['path']
                record('source:' + source['id'], os.path.isdir(path) and os.access(path, os.R_OK | os.X_OK),
                       'Доступ проверен от текущего пользователя; монтирование: ' + str(os.path.ismount(path)))
        data = store.base['data_dir']
        free = shutil.disk_usage(data).free
        record('storage', free >= settings['station'].get('min_free_mb', 1024) * 1024 ** 2, 'Свободно байт: ' + str(free))
        engine = store.base['engine']
        record('engine', os.access(engine, os.X_OK), 'Проверка исполняемого launcher; декодирование не запускалось')
    except (OSError, ValueError, KeyError, TypeError) as error:
        record('configuration', False, str(error))
    return {'ok': all(c['ok'] for c in checks), 'checks': checks}


def main():
    parser = argparse.ArgumentParser(description='Подготовка служб и диагностика Station')
    parser.add_argument('command', choices=('provision', 'units', 'doctor'))
    parser.add_argument('--package', default=str(ROOT))
    parser.add_argument('--config', default='/etc/satdump-station')
    parser.add_argument('--data', default='/var/lib/satdump-station')
    parser.add_argument('--prefix', default='/opt/satdump-station')
    parser.add_argument('--source-path', default='')
    parser.add_argument('--source-kind', default='image')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8090)
    parser.add_argument('--control-port', type=int, default=8091)
    parser.add_argument('--backend-port', type=int, default=8092)
    parser.add_argument('--web-server', choices=('builtin', 'nginx'), default='builtin')
    parser.add_argument('--output')
    args = parser.parse_args()
    if args.command == 'provision':
        print(provision(args.package, args.config, args.data, args.source_path, args.source_kind))
    elif args.command == 'doctor':
        result = doctor(str(Path(args.config) / 'station.json'))
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0 if result['ok'] else 1
    else:
        result = unit_files(args.prefix, args.config, args.data, args.host, args.port, args.control_port, args.backend_port, args.web_server)
        out = Path(args.output)
        out.mkdir(parents=True, exist_ok=True)
        for name, value in result.items():
            (out / name).write_text(value)
        if args.web_server == 'nginx':
            (out / 'nginx.conf').write_text(nginx_config(args.config, args.host, args.port, args.backend_port))
    return 0


if __name__ == '__main__':
    sys.exit(main())
