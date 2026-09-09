#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Restricted, revisioned control plane. Python 3.5+, standard library only.

No shell, plugins, arbitrary argv, credentials or executable paths are writable.
The root-owned base configuration/policy remain the privilege boundary.
"""
from __future__ import print_function
import argparse
import copy
import fcntl
import hashlib
import hmac
import json
import math
import os
import re
import socketserver
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

LIMIT = 1024 * 1024
SCHEMA = 'satdump.station.control/1'
RANGES = {'poll_seconds': (1, 3600), 'settle_seconds': (0, 3600),
          'timeout_seconds': (1, 86400), 'max_attempts': (1, 20),
          'native_threads': (1, 256), 'min_free_mb': (0, 1048576),
          'max_input_mb': (1, 1048576), 'max_log_mb': (1, 1024),
          'max_image_pixels': (1, 200000000), 'max_items': (1, 10000)}
BOOLS = ('keep_work', 'archive_science')
PBOOLS = ('enabled', 'save_minimal', 'save_editorial', 'save_presentation',
          'save_legacy_alias', 'north_up', 'show_branding')
MODES = ('auto', 'keep', 'none', 'source', 'flip_vertical', 'vertical',
         'flip_horizontal', 'horizontal', 'rotate_180', '180')
COLORS = ('panel', 'panel_secondary', 'border', 'text', 'muted_text', 'accent',
          'warning', 'error', 'red_component', 'green_component', 'blue_component')
SOURCE_KEYS = ('id', 'kind', 'path', 'enabled', 'patterns', 'require_ready',
               'satellite', 'instrument', 'pipeline', 'input_level', 'options')
PIPE_OPTIONS = {'--samplerate': r'[0-9]{1,10}', '--baseband_format': r'(cs8|cs16|cf32|cu8|s8|s16|f32|u8)',
                '--dc_block': r'(true|false)', '--iq_swap': r'(true|false)',
                '--freq_shift': r'-?[0-9]{1,10}'}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def pairs(items):
    result = {}
    for key, value in items:
        require(key not in result, 'Повторный ключ JSON: ' + key)
        result[key] = value
    return result


def decode(raw):
    require(len(raw) <= LIMIT, 'JSON превышает 1 МиБ')
    def bad(value):
        raise ValueError('Недопустимое число: ' + value)
    value = json.loads(raw.decode('utf-8'), object_pairs_hook=pairs, parse_constant=bad)
    def finite(obj, depth=0):
        require(depth < 48, 'Слишком глубокий JSON')
        if isinstance(obj, float):
            require(math.isfinite(obj), 'Недопустимое число')
        elif isinstance(obj, dict):
            for v in obj.values():
                finite(v, depth + 1)
        elif isinstance(obj, list):
            for v in obj:
                finite(v, depth + 1)
    finite(value)
    return value


def read(path):
    with open(str(path), 'rb') as stream:
        return decode(stream.read(LIMIT + 1))


def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, allow_nan=False,
                      separators=(',', ':')).encode('utf-8')


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def atomic(path, value, mode=0o640):
    path = Path(path)
    fd, name = tempfile.mkstemp(prefix='.pending-', dir=str(path.parent))
    try:
        with os.fdopen(fd, 'wb') as stream:
            os.fchmod(stream.fileno(), mode)
            stream.write(canonical(value) + b'\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, str(path))
        directory = os.open(str(path.parent), os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def keys(value, allowed, context):
    require(isinstance(value, dict), context + ': требуется объект')
    require(not set(value) - set(allowed), context + ': неизвестные поля ' + ', '.join(sorted(set(value) - set(allowed))))



def editable_keys(value, allowed, baseline, context):
    """Preserve administrator-owned legacy fields, but never make them writable."""
    baseline = baseline if isinstance(baseline, dict) else {}
    keys(value, set(allowed) | set(baseline), context)
    for key in set(baseline) - set(allowed):
        require(key in value and value[key] == baseline[key], context + ': поле доступно только root: ' + key)



def text(value, limit=240):
    require(isinstance(value, str) and len(value) <= limit and not any(ord(c) < 32 for c in value), 'Некорректная строка')


def boolean(value):
    require(type(value) is bool, 'Требуется true/false, не строка или число')


def number(value, low, high, integer=False):
    require(type(value) is int if integer else type(value) in (int, float), 'Некорректный числовой тип')
    require(low <= value <= high, 'Число вне диапазона {0}..{1}'.format(low, high))


def beneath(path, root):
    return os.path.commonpath([os.path.realpath(path), os.path.realpath(root)]) == os.path.realpath(root)


def clean_path(path):
    text(path, 4096)
    require(bool(re.match(r'^/[A-Za-z0-9_./-]+$', path)) and '..' not in path.split('/'), 'Нужен абсолютный путь без пробелов/..')
    require(os.path.normpath(path) not in ('/', '/etc', '/usr', '/var', '/opt', '/home', '/root', '/tmp'), 'Нужен выделенный каталог')
    return os.path.normpath(path)


def jsonc(path):
    """Strip C/C++ comments without corrupting quoted URLs; no eval."""
    with open(str(path), encoding='utf-8') as stream:
        raw = stream.read(8 * LIMIT)
    pattern = r'("(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/)'
    raw = re.sub(pattern, lambda m: m.group(0) if m.group(0).startswith('"') else ' ', raw)
    return json.loads(raw)


def presentation(value):
    keys(value, PBOOLS + ('orientation_mode', 'orientation', 'outputs', 'minimal', 'editorial', 'presentational'), 'presentation')
    for key, v in value.items():
        if key in PBOOLS:
            boolean(v)
        elif key == 'orientation_mode':
            require(v in MODES, 'Неизвестный режим ориентации')
        elif key == 'orientation':
            if isinstance(v, str):
                require(v in MODES, 'Неизвестная ориентация')
            else:
                keys(v, ('mode', 'north_up'), key)
                if 'mode' in v:
                    require(v['mode'] in MODES, 'Неизвестная ориентация')
                if 'north_up' in v:
                    boolean(v['north_up'])
        elif key == 'outputs':
            allowed = ('minimal', 'editorial', 'presentation', 'legacy_alias')
            if isinstance(v, list):
                require(all(x in allowed for x in v), 'Неизвестный формат вывода')
            else:
                keys(v, allowed, key)
                for x in v.values():
                    boolean(x)
        else:
            if type(v) is bool:
                continue
            keys(v, ('enabled', 'branding', 'show_branding', 'theme'), key)
            for b in ('enabled', 'show_branding'):
                if b in v:
                    boolean(v[b])
            if 'branding' in v:
                text(v['branding'])
            theme = v.get('theme', {})
            keys(theme, COLORS + ('reference_width', 'minimum_scale', 'maximum_scale'), 'theme')
            for name, color in theme.items():
                if name in COLORS:
                    if isinstance(color, str):
                        require(bool(re.match(r'^#?[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$', color)), 'Некорректный цвет')
                    else:
                        require(isinstance(color, list) and len(color) in (3, 4), 'Цвет: RGB/RGBA')
                        for x in color:
                            number(x, 0, 255)
                else:
                    number(color, 320 if name == 'reference_width' else 0.25, 8192 if name == 'reference_width' else 8,
                           name == 'reference_width')
            require(theme.get('minimum_scale', 0.25) <= theme.get('maximum_scale', 8), 'minimum_scale > maximum_scale')


class Store:
    def __init__(self, config, engine_root=None):
        self.config = Path(config)
        self.base = read(self.config)
        self.processing = read(self.config.parent / self.base.get('processing_config', 'processing.json'))
        self.root = Path(self.base['data_dir']) / 'control'
        self.policy = read(self.config.parent / 'control-policy.json')
        # Python 3.5 Path.resolve is strict; current may not exist until activation.
        self.engine_root = Path(os.path.abspath(str(engine_root))) if engine_root else Path(os.path.abspath(self.base['engine'])).parent

    def capabilities(self):
        path = self.engine_root / 'share/satdump/satdump_cfg.json'
        instruments = jsonc(path).get('viewer', {}).get('instruments', {}) if path.is_file() else {}
        presets = {name: sorted(v.get('rgb_composites', {})) for name, v in instruments.items() if isinstance(v, dict)}
        return {'schema': 'satdump.station.capabilities/1', 'instruments': presets,
                'source_kinds': ['image', 'product', 'pipeline'], 'pipeline_ids': self.policy.get('pipeline_ids', []),
                'pipeline_options': sorted(PIPE_OPTIONS), 'station_ranges': RANGES,
                'presentation_fields': list(PBOOLS) + ['orientation', 'orientation_mode', 'outputs', 'minimal', 'editorial', 'presentational'],
                'theme_fields': list(COLORS) + ['reference_width', 'minimum_scale', 'maximum_scale'],
                'preset_fields': ['autogen', 'equation', 'presentation'], 'input_roots': self.policy['input_roots'],
                'privileged_read_only': {'data_dir': self.base['data_dir'], 'engine': self.base['engine'],
                                         'processing_config': self.base.get('processing_config', 'processing.json')},
                'restrictions': ['no_lua_or_shell', 'no_arbitrary_argv', 'no_live_storage_migration', 'trusted_lan_only']}

    def initial(self):
        s = {k: copy.deepcopy(v) for k, v in self.base.items() if k in tuple(RANGES) + BOOLS + ('title', 'sources')}
        return {'station': s, 'processing': copy.deepcopy(self.processing),
                'board': {'hidden_sources': [], 'hidden_instruments': [], 'hidden_products': [],
                          'layouts': ['editorial', 'minimal', 'external'], 'max_items': self.base.get('max_items', 1000)}}

    def current(self):
        path = self.root / 'active.json'
        if path.is_file():
            doc = read(path)
            require(doc.get('schema') == SCHEMA and doc.get('revision') == digest(doc['settings']), 'Повреждена ревизия конфигурации')
            self.validate(doc['settings'])
            return doc
        settings = self.initial()
        return {'schema': SCHEMA, 'revision': digest(settings), 'settings': settings, 'updated_at': 0}

    def validate(self, settings):
        keys(settings, ('station', 'processing', 'board'), 'settings')
        require(set(settings) == {'station', 'processing', 'board'}, 'Нужны station, processing и board')
        s = settings['station']
        keys(s, tuple(RANGES) + BOOLS + ('title', 'sources'), 'station')
        require('sources' in s, 'Не заданы источники')
        for k, v in s.items():
            if k in RANGES:
                number(v, RANGES[k][0], RANGES[k][1], True)
            elif k in BOOLS:
                boolean(v)
            elif k == 'title':
                text(v)
        require(isinstance(s['sources'], list) and len(s['sources']) <= 64, 'Не более 64 источников')
        seen, paths = set(), []
        for source in s['sources']:
            keys(source, SOURCE_KEYS, 'source')
            require(all(k in source for k in ('id', 'kind', 'path')), 'Нужны id, kind и path')
            require(isinstance(source['id'], str) and re.match(r'^[A-Za-z0-9_-]{1,64}$', source['id']) and source['id'] not in seen, 'Неуникальный/небезопасный id')
            seen.add(source['id'])
            require(source['kind'] in ('image', 'product', 'pipeline'), 'Неизвестный kind')
            path = clean_path(source['path'])
            require(any(beneath(path, r) for r in self.policy['input_roots']), 'Путь вне разрешённых root-политикой входов')
            for private in ('public', 'work', 'state', 'logs', 'archive', 'control'):
                p = str(Path(self.base['data_dir']) / private)
                require(not beneath(path, p) and not beneath(p, path), 'Пересечение входа и служебного каталога')
            require(not any(beneath(path, p) or beneath(p, path) for p in paths), 'Перекрывающиеся источники')
            paths.append(path)
            for k in ('enabled', 'require_ready'):
                if k in source:
                    boolean(source[k])
            for k in ('satellite', 'instrument', 'input_level'):
                if k in source:
                    text(source[k])
            patterns = source.get('patterns', [])
            require(isinstance(patterns, list) and len(patterns) <= 32, 'Некорректные маски')
            for pattern in patterns:
                text(pattern, 128)
                require('/' not in pattern and '\\' not in pattern, 'Маска не должна содержать путь')
            if source['kind'] == 'pipeline':
                require(source.get('pipeline') in self.policy.get('pipeline_ids', []), 'Конвейер не разрешён root-политикой')
                require(bool(re.match(r'^[A-Za-z0-9_-]{1,64}$', source.get('input_level', ''))), 'Некорректный input_level')
                options = source.get('options', [])
                trusted = next((s for s in self.base['sources'] if s['id'] == source['id'] and s['kind'] == 'pipeline'), None)
                if trusted is not None and options == trusted.get('options', []):
                    continue
                require(isinstance(options, list) and len(options) <= 10 and len(options) % 2 == 0, 'Нужны пары параметр/значение')
                flags = set()
                for i in range(0, len(options), 2):
                    flag, value = options[i:i + 2]
                    require(isinstance(flag, str) and flag in PIPE_OPTIONS and flag not in flags and isinstance(value, str), 'Запрещённый/повторный параметр конвейера')
                    require(re.match('^' + PIPE_OPTIONS[flag] + '$', value) is not None, 'Недопустимое значение параметра')
                    flags.add(flag)
            else:
                require(not set(source) & {'pipeline', 'input_level', 'options'}, 'Параметры конвейера у файлового входа')
        p = settings['processing']
        editable_keys(p, ('satdump_general', 'viewer'), self.processing, 'processing')
        g = p.get('satdump_general', {})
        general_keys = ('tle_update_interval', 'log_to_file', 'presentation_enabled', 'presentation_show_branding',
                        'presentation', 'qth_lat', 'qth_lon', 'qth_alt', 'default_qth_label')
        editable_keys(g, general_keys, self.processing.get('satdump_general', {}), 'satdump_general')
        for k, v in g.items():
            if k not in general_keys:
                continue
            if k == 'presentation':
                presentation(v)
                continue
            keys(v, ('value',), k)
            require('value' in v, 'Требуется value')
            v = v['value']
            if k == 'tle_update_interval':
                require(v == 'Never', 'Сетевое обновление отключено')
            elif k == 'log_to_file':
                require(v is False, 'Используйте журналы станции')
            elif k.startswith('presentation_'):
                boolean(v)
            elif k == 'default_qth_label':
                text(v)
            else:
                lo, hi = {'qth_lat': (-90, 90), 'qth_lon': (-180, 180), 'qth_alt': (-500, 10000)}[k]
                number(v, lo, hi)
        viewer = p.get('viewer', {})
        base_viewer = self.processing.get('viewer', {})
        editable_keys(viewer, ('instruments',), base_viewer, 'viewer')
        instruments = viewer.get('instruments', {})
        require(isinstance(instruments, dict), 'instruments: объект')
        available = self.capabilities()['instruments'] if instruments else {}
        for name, instrument in instruments.items():
            require(name in available, 'Прибор отсутствует в установленном движке: ' + name)
            base_instrument = base_viewer.get('instruments', {}).get(name, {})
            editable_keys(instrument, ('rgb_composites',), base_instrument, name)
            require(isinstance(instrument.get('rgb_composites', {}), dict), 'rgb_composites: объект')
            for preset, params in instrument.get('rgb_composites', {}).items():
                require(preset in available[name], 'Пресет отсутствует: ' + preset)
                editable_keys(params, ('autogen', 'equation', 'presentation'), base_instrument.get('rgb_composites', {}).get(preset, {}), preset)
                if 'autogen' in params:
                    boolean(params['autogen'])
                if 'equation' in params:
                    text(params['equation'], 2048)
                    require(bool(re.match(r'^[A-Za-z0-9_+*/().,:?<>!=&|^% \-]+$', params['equation'])), 'Недопустимое выражение каналов')
                if 'presentation' in params:
                    presentation(params['presentation'])
        b = settings['board']
        keys(b, ('hidden_sources', 'hidden_instruments', 'hidden_products', 'layouts', 'max_items'), 'board')
        for field in ('hidden_sources', 'hidden_instruments', 'hidden_products', 'layouts'):
            value = b.get(field, [])
            require(isinstance(value, list) and len(value) <= 1000, 'Некорректный список ' + field)
            for entry in value:
                text(entry)
        require(all(x in ('editorial', 'minimal', 'external') for x in b.get('layouts', [])), 'Неизвестный макет')
        number(b.get('max_items', 1000), 1, 10000, True)
        return settings

    @staticmethod
    def processing_key(settings):
        return digest({'processing': settings['processing'], 'sources': settings['station']['sources']})

    def save(self, settings, expected, confirm=False):
        self.validate(settings)
        with open(str(self.root / 'write.lock'), 'a') as lock:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
            old = self.current()
            if expected != old['revision']:
                return 412, {'error': 'revision_conflict', 'revision': old['revision']}
            changed = self.processing_key(settings) != self.processing_key(old['settings'])
            if changed and not confirm:
                return 409, {'error': 'reprocessing_confirmation_required', 'detail': 'Рецепт/источники изменились. Доступные входы могут быть обработаны повторно.'}
            revision = digest(settings)
            if revision == old['revision']:
                return 200, old
            doc = {'schema': SCHEMA, 'revision': revision, 'settings': settings,
                   'updated_at': time.time(), 'reprocessing_confirmed': bool(changed)}
            revisions = self.root / 'revisions'
            revisions.mkdir(exist_ok=True, mode=0o750)
            atomic(revisions / (revision + '.json'), doc)
            atomic(self.root / 'active.json', doc)
            return 202, doc


class Handler(BaseHTTPRequestHandler):
    server_version, sys_version = 'SatDumpControl', ''
    def setup(self):
        BaseHTTPRequestHandler.setup(self)
        self.connection.settimeout(10)

    def log_message(self, *args):
        pass  # Never log tokens, query strings or submitted settings.

    def response(self, code, value):
        raw = canonical(value)
        self.send_response(code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(raw)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Connection', 'close')
        if 'revision' in value:
            self.send_header('ETag', '"' + value['revision'] + '"')
        self.end_headers()
        self.wfile.write(raw)
        self.close_connection = True

    def dispatch(self):
        try:
            hosts = ['127.0.0.1:' + str(self.server.server_port), 'localhost:' + str(self.server.server_port)]
            if len(self.headers.get_all('Host', [])) != 1 or self.headers.get('Host') not in hosts:
                return self.response(403, {'error': 'host_rejected'})
            origins = ['http://' + h for h in hosts] + self.server.store.policy.get('admin_origins', [])
            if len(self.headers.get_all('Origin', [])) > 1 or (self.headers.get('Origin') and self.headers['Origin'] not in origins):
                return self.response(403, {'error': 'origin_rejected'})
            auth = self.headers.get('Authorization', '')
            if len(self.headers.get_all('Authorization', [])) != 1 or not hmac.compare_digest(auth.encode('utf-8'), ('Bearer ' + self.server.token).encode('ascii')):
                return self.response(401, {'error': 'authentication_required'})
            store = self.server.store
            if self.path == '/api/v1/control/health' and self.command == 'GET':
                return self.response(200, {'control_alive': True})
            if self.path == '/api/v1/control/capabilities' and self.command == 'GET':
                return self.response(200, store.capabilities())
            if self.path == '/api/v1/control/config' and self.command == 'GET':
                return self.response(200, store.current())
            if (self.path, self.command) not in (('/api/v1/control/validate', 'POST'), ('/api/v1/control/config', 'PUT')):
                return self.response(404, {'error': 'route_not_found'})
            if self.headers.get('Transfer-Encoding') or len(self.headers.get_all('Content-Length', [])) != 1:
                return self.response(400, {'error': 'content_length_required'})
            if self.headers.get('Content-Type', '').split(';')[0].strip() != 'application/json':
                return self.response(415, {'error': 'json_required'})
            length = int(self.headers['Content-Length'])
            if length < 0 or length > LIMIT:
                return self.response(413, {'error': 'body_too_large'})
            payload = decode(self.rfile.read(length))
            keys(payload, ('settings', 'confirm_reprocess'), 'request')
            require('settings' in payload, 'Нужны settings')
            boolean(payload.get('confirm_reprocess', False))
            store.validate(payload['settings'])
            if self.command == 'POST':
                return self.response(200, {'valid': True, 'reprocessing_required': store.processing_key(payload['settings']) != store.processing_key(store.current()['settings'])})
            match = self.headers.get('If-Match', '')
            if not re.match(r'^"[0-9a-f]{64}"$', match) or len(self.headers.get_all('If-Match', [])) != 1:
                return self.response(428, {'error': 'if_match_required'})
            code, result = store.save(payload['settings'], match[1:-1], payload.get('confirm_reprocess', False))
            return self.response(code, result)
        except (ValueError, KeyError, TypeError, OverflowError, RecursionError) as error:
            return self.response(422, {'error': 'validation_failed', 'detail': str(error)[:600]})
        except OSError:
            return self.response(503, {'error': 'storage_unavailable'})

    do_GET = do_POST = do_PUT = dispatch


class Server(socketserver.ThreadingMixIn, HTTPServer):
    daemon_threads, allow_reuse_address = True, True
    def __init__(self, address, store, token):
        require(address[0] == '127.0.0.1', 'API управления допускает только loopback')
        require(bool(re.match(r'^[0-9a-f]{64}$', token)), 'Нужен 256-битный токен')
        self.store, self.token = store, token
        self.slots = threading.BoundedSemaphore(8)
        HTTPServer.__init__(self, address, Handler)

    def process_request(self, request, address):
        if not self.slots.acquire(False):
            request.close()
            return
        try:
            socketserver.ThreadingMixIn.process_request(self, request, address)
        except Exception:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            socketserver.ThreadingMixIn.process_request_thread(self, request, address)
        finally:
            self.slots.release()


def main():
    parser = argparse.ArgumentParser(description='Локальный защищённый API Station')
    parser.add_argument('--config', default='/etc/satdump-station/station.json')
    parser.add_argument('--port', type=int, default=8091)
    parser.add_argument('--token-file', default='/etc/satdump-station/control.token')
    args = parser.parse_args()
    with open(args.token_file) as stream:
        token = stream.read().strip()
    server = Server(('127.0.0.1', args.port), Store(args.config), token)
    try:
        server.serve_forever()
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
