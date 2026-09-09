#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Control, publication, TTY and service contracts. Native calls use a fixture.

The optional adapter tests require the repository's original station.py. They
are mandatory in CI, where that module is present; standalone source packs may
run only the independent infrastructure tests.
"""
from __future__ import print_function
import copy
import importlib.util
import json
import os
import pty
import select
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'services/station'))
sys.path.insert(0, str(ROOT / 'scripts/station'))
import control
import board
import configure


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.config = self.root / 'etc'
        self.config.mkdir()
        self.data = self.root / 'data'
        self.engine = self.root / 'engine'
        (self.engine / 'share/satdump').mkdir(parents=True)
        (self.engine / 'satdump').write_text('#!/bin/sh\nexit 0\n')
        (self.engine / 'satdump').chmod(0o755)
        (self.engine / 'share/satdump/satdump_cfg.json').write_text('''{
          // A URL string must not be truncated by JSONC stripping.
          "url":"https://example.invalid/a//b", /* block */
          "viewer":{"instruments":{"msu_mr":{"rgb_composites":{"RGB 321":{"equation":"ch3,ch2,ch1"}}}}}
        }''')
        for name in ('inbox/images', 'inbox/products', 'control/revisions', 'public/items', 'state'):
            (self.data / name).mkdir(parents=True, exist_ok=True)
        self.cfg = {'schema': 'satdump.station/1', 'title': 'Тест', 'data_dir': str(self.data),
                    'engine': str(self.engine / 'satdump'), 'processing_config': 'processing.json',
                    'processing_revision': '1', 'poll_seconds': 1, 'settle_seconds': 0,
                    'timeout_seconds': 30, 'max_attempts': 2, 'native_threads': 1,
                    'min_free_mb': 0, 'max_input_mb': 10, 'max_log_mb': 2, 'max_items': 50,
                    'sources': [{'id': 'images', 'kind': 'image', 'path': str(self.data / 'inbox/images'), 'require_ready': False}]}
        control.atomic(self.config / 'station.json', self.cfg)
        control.atomic(self.config / 'processing.json', {'satdump_general': {'presentation': {'save_minimal': True, 'save_presentation': True}}})
        control.atomic(self.config / 'control-policy.json', {'input_roots': [str(self.data / 'inbox')],
                       'pipeline_ids': ['meteor_m2x_lrpt'], 'admin_origins': []})
        self.store = control.Store(self.config / 'station.json')

    def tearDown(self):
        self.temp.cleanup()


class ValidationTests(Fixture):
    def test_legacy_root_fields_readonly(self):
        initial = control.read(self.config / 'processing.json')
        initial['satdump_general']['root_only_extension'] = {'value': 'trusted'}
        control.atomic(self.config / 'processing.json', initial)
        store = control.Store(self.config / 'station.json')
        settings = store.initial()
        store.validate(settings)
        settings['processing']['satdump_general']['root_only_extension'] = {'value': 'changed'}
        with self.assertRaises(ValueError):
            store.validate(settings)

    def test_initial_valid(self):
        self.store.validate(self.store.initial())

    def test_real_capabilities(self):
        caps = self.store.capabilities()
        self.assertEqual(['RGB 321'], caps['instruments']['msu_mr'])
        self.assertIn('data_dir', caps['privileged_read_only'])

    def test_strict_json(self):
        for data in (b'{"x":1,"x":2}', b'{"x":NaN}', b'{"x":Infinity}', b'{"x":1e999}'):
            with self.assertRaises(ValueError):
                control.decode(data)

    def test_limits_and_types(self):
        for key, value in (('poll_seconds', True), ('settle_seconds', '5'), ('native_threads', 100000)):
            settings = self.store.initial()
            settings['station'][key] = value
            with self.assertRaises(ValueError):
                self.store.validate(settings)

    def test_privileged_fields_rejected(self):
        for field in ('data_dir', 'engine', 'processing_config'):
            settings = self.store.initial()
            settings['station'][field] = '/tmp/evil'
            with self.assertRaises(ValueError):
                self.store.validate(settings)

    def test_forbidden_paths_and_overlap(self):
        for path in ('/etc/passwd', str(self.data / 'public'), str(self.data / 'inbox/../state')):
            settings = self.store.initial()
            settings['station']['sources'][0]['path'] = path
            with self.assertRaises(ValueError):
                self.store.validate(settings)
        settings = self.store.initial()
        source = copy.deepcopy(settings['station']['sources'][0])
        source['id'] = 'duplicate_path'
        settings['station']['sources'].append(source)
        with self.assertRaises(ValueError):
            self.store.validate(settings)

    def test_symlink_escape_rejected(self):
        link = self.data / 'inbox/escape'
        link.symlink_to('/etc')
        settings = self.store.initial()
        settings['station']['sources'][0]['path'] = str(link)
        with self.assertRaises(ValueError):
            self.store.validate(settings)

    def test_pipeline_options_allowlist(self):
        settings = self.store.initial()
        s = settings['station']['sources'][0]
        s.update(kind='pipeline', pipeline='meteor_m2x_lrpt', input_level='baseband', options=['--samplerate', '1024000'])
        self.store.validate(settings)
        s['options'] = ['--processing_config', '/etc/passwd']
        with self.assertRaises(ValueError):
            self.store.validate(settings)

    def test_presentation_and_channel_parameters(self):
        settings = self.store.initial()
        settings['processing']['viewer'] = {'instruments': {'msu_mr': {'rgb_composites': {'RGB 321': {
            'autogen': True, 'equation': 'ch3,ch2,ch1',
            'presentation': {'north_up': True, 'minimal': {'enabled': True, 'theme': {'text': '#ffffff'}}}}}}}}
        self.store.validate(settings)
        settings['processing']['viewer']['instruments']['msu_mr']['rgb_composites']['RGB 321']['lua'] = 'os.execute()'
        with self.assertRaises(ValueError):
            self.store.validate(settings)

    def test_revision_conflict_and_no_partial_write(self):
        doc = self.store.current()
        settings = copy.deepcopy(doc['settings'])
        settings['station']['title'] = 'Новая подпись'
        code, new = self.store.save(settings, doc['revision'])
        self.assertEqual(202, code)
        self.assertEqual(new['revision'], self.store.current()['revision'])
        self.assertEqual(412, self.store.save(settings, doc['revision'])[0])
        self.assertTrue((self.store.root / 'revisions' / (new['revision'] + '.json')).is_file())

    def test_reprocessing_requires_confirmation(self):
        doc = self.store.current()
        settings = copy.deepcopy(doc['settings'])
        settings['processing']['satdump_general']['presentation']['north_up'] = False
        self.assertEqual(409, self.store.save(settings, doc['revision'])[0])
        self.assertFalse((self.store.root / 'active.json').exists())
        self.assertEqual(202, self.store.save(settings, doc['revision'], True)[0])

    def test_visibility_does_not_require_reprocessing(self):
        doc = self.store.current()
        settings = copy.deepcopy(doc['settings'])
        settings['board']['hidden_sources'] = ['images']
        self.assertEqual(202, self.store.save(settings, doc['revision'])[0])
        self.assertFalse(board.visible({'source': 'images', 'layout': 'editorial'}, settings['board']))

    def test_layout_preserved_for_external_presentation(self):
        self.assertEqual('minimal', board.layout({'schema': 'satdump.presentation/2', 'layout': 'minimal'}, False))
        self.assertEqual('external', board.layout({}, False))

    def test_atomic_permissions(self):
        path = self.root / 'private.json'
        control.atomic(path, {'x': 1})
        self.assertEqual(0o640, path.stat().st_mode & 0o777)

    def test_concurrent_cas_one_winner(self):
        doc = self.store.current()
        results = []
        def update(title):
            settings = copy.deepcopy(doc['settings'])
            settings['station']['title'] = title
            results.append(self.store.save(settings, doc['revision'])[0])
        threads = [threading.Thread(target=update, args=(str(i),)) for i in range(5)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        self.assertEqual([202, 412, 412, 412, 412], sorted(results))


class HttpTests(Fixture):
    def setUp(self):
        Fixture.setUp(self)
        self.token = 'a' * 64
        self.server = control.Server(('127.0.0.1', 0), self.store, self.token)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.daemon = True
        self.thread.start()
        self.url = 'http://127.0.0.1:' + str(self.server.server_port)

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(3)
        Fixture.tearDown(self)

    def request(self, path='/config', method='GET', payload=None, headers=None, auth=True):
        values = {'Authorization': 'Bearer ' + self.token} if auth else {}
        values.update(headers or {})
        if payload is not None:
            values['Content-Type'] = 'application/json'
        request = Request(self.url + '/api/v1/control' + path, data=control.canonical(payload) if payload is not None else None,
                          headers=values, method=method)
        try:
            response = urlopen(request, timeout=3)
        except HTTPError as error:
            response = error
        with response:
            return response.code, json.loads(response.read().decode('utf-8')), dict(response.headers)

    def test_no_auth_no_config(self):
        self.assertEqual(401, self.request(auth=False)[0])

    def test_authenticated_health(self):
        self.assertEqual({'control_alive': True}, self.request('/health')[1])

    def test_wrong_host_and_origin(self):
        self.assertEqual(403, self.request(headers={'Host': 'attacker.example'})[0])
        self.assertEqual(403, self.request(headers={'Origin': 'https://attacker.example'})[0])

    def test_no_query_tokens(self):
        self.assertEqual(401, self.request('/config?token=' + self.token, auth=False)[0])

    def test_etag_and_apply(self):
        code, doc, headers = self.request()
        self.assertEqual(200, code)
        settings = doc['settings']
        settings['station']['title'] = 'Изменено по API'
        payload = {'settings': settings}
        self.assertEqual(428, self.request(method='PUT', payload=payload)[0])
        self.assertEqual(202, self.request(method='PUT', payload=payload, headers={'If-Match': headers['ETag']})[0])
        self.assertEqual(412, self.request(method='PUT', payload=payload, headers={'If-Match': headers['ETag']})[0])

    def test_validate_does_not_save(self):
        settings = self.store.initial()
        self.assertEqual(200, self.request('/validate', 'POST', {'settings': settings})[0])
        self.assertFalse((self.store.root / 'active.json').exists())

    def test_bind_external_forbidden(self):
        with self.assertRaises(ValueError):
            control.Server(('0.0.0.0', 0), self.store, self.token)

    def test_errors_are_structured(self):
        code, body, unused = self.request('/validate', 'POST', {'settings': []})
        self.assertEqual(422, code)
        self.assertEqual('validation_failed', body['error'])
        self.assertNotIn(self.token, json.dumps(body))


class DeploymentTests(Fixture):
    def test_three_services_and_privilege_separation(self):
        units = configure.unit_files('/opt/satdump-station', '/etc/satdump-station', '/var/lib/satdump-station', '127.0.0.1', 8090, 8091, 8092, 'builtin')
        self.assertEqual(3, len(units))
        self.assertIn('User=satdump-web', units['satdump-web.service'])
        self.assertNotIn('ReadWriteDirectories=', units['satdump-web.service'])
        self.assertIn('PrivateNetwork=true', units['satdump-worker.service'])
        self.assertIn('ReadWriteDirectories=/var/lib/satdump-station/control', units['satdump-control.service'])
        self.assertNotIn('User=root', ''.join(units.values()))

    def test_nginx_isolated_and_control_not_exposed(self):
        text = configure.nginx_config('/etc/satdump-station', '127.0.0.1', 18090, 18092)
        self.assertIn('location /api/v1/control/ { return 404; }', text)
        self.assertNotIn('\n    include ', text)
        self.assertNotIn('\ninclude ', text)
        binary = shutil.which('nginx') or '/usr/sbin/nginx'
        if not os.path.isfile(binary):
            self.skipTest('nginx not installed')
        run = self.root / 'run'
        run.mkdir()
        text = text.replace('/run/satdump-web', str(run))
        path = self.root / 'nginx.conf'
        path.write_text(text)
        subprocess.check_call([binary, '-t', '-c', str(path)])

    def test_invalid_service_arguments(self):
        with self.assertRaises(ValueError):
            configure.unit_files('/opt/sd', '/etc/sd', '/data/sd', '127.0.0.1', 8090, 8090, 8092, 'builtin')

    def test_doctor_does_not_modify(self):
        before = (self.config / 'station.json').read_bytes()
        self.assertTrue(configure.doctor(str(self.config / 'station.json'))['ok'])
        self.assertEqual(before, (self.config / 'station.json').read_bytes())

    def test_shell_syntax(self):
        for path in list((ROOT / 'scripts/station').glob('*.sh')) + [ROOT / 'station.sh', ROOT / 'install.sh']:
            subprocess.check_call(['bash', '-n', str(path)])

    def test_unattended_plan_no_ansi(self):
        out = subprocess.check_output(['bash', str(ROOT / 'install.sh'), '--allow-compatible', '--dry-run', '--non-interactive'], stderr=subprocess.STDOUT)
        self.assertNotIn(b'\x1b', out)
        self.assertIn('API'.encode(), out)

    def tty(self, answers, extra=None):
        master, slave = pty.openpty()
        env = os.environ.copy()
        env['TERM'] = 'xterm'
        env.pop('NO_COLOR', None)
        proc = subprocess.Popen(['bash', str(ROOT / 'install.sh'), '--allow-compatible', '--dry-run', '--interactive'] + (extra or []),
                                stdin=slave, stdout=slave, stderr=slave, env=env)
        os.close(slave)
        os.write(master, answers.encode('utf-8'))
        output = b''
        until = time.time() + 8
        try:
            while time.time() < until:
                ready, unused, unused2 = select.select([master], [], [], 0.1)
                if ready:
                    try:
                        block = os.read(master, 65536)
                    except OSError:
                        break
                    if not block:
                        break
                    output += block
                elif proc.poll() is not None:
                    break
            proc.wait(timeout=2)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            os.close(master)
        return proc.returncode, output

    def test_tty_accept_defaults(self):
        rc, output = self.tty('\n' * 6)
        self.assertEqual(0, rc)
        self.assertIn(b'\x1b', output)
        self.assertIn('План установки'.encode(), output)

    def test_tty_back_and_cancel(self):
        rc, output = self.tty('\nb\nq\n', ['--no-color', '--no-animation'])
        self.assertEqual(0, rc)
        self.assertNotIn(b'\x1b', output)
        self.assertGreaterEqual(output.count('1/6'.encode()), 2)

    def test_tty_rejects_invalid_then_accepts(self):
        rc, output = self.tty('\n999.2.3.4\n127.0.0.1\n\n\n\n\n')
        self.assertEqual(0, rc)
        self.assertIn('Некорректный IPv4'.encode(), output)


@unittest.skipUnless((ROOT / 'services/station/station.py').is_file(), 'Requires upstream station.py; mandatory in repository CI')
class AdapterTests(Fixture):
    def setUp(self):
        Fixture.setUp(self)
        spec = importlib.util.spec_from_file_location('station_adapter_test', str(ROOT / 'services/station/station.py'))
        self.station = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.station)
        self.Worker, self.Server = board.install_adapter(self.station)
        self.worker = self.Worker(self.station.load_config(self.config / 'station.json'))

    def tearDown(self):
        self.worker.close()
        Fixture.tearDown(self)

    def ingest(self):
        self.worker.scan()
        self.worker.scan()
        self.worker.tick()

    def test_external_image_and_metadata_flow(self):
        from PIL import Image
        path = self.data / 'inbox/images/test.png'
        Image.new('RGB', (32, 20)).save(str(path))
        control.atomic(path.with_suffix('.json'), {'schema': 'satdump.presentation/2', 'layout': 'minimal',
                        'pass': {'satellite': 'METEOR-M', 'acquisition_time': '2026-09-09T00:00:00Z'},
                        'orientation': {'north_up_verified': False}})
        self.ingest()
        catalog = control.read(self.data / 'public/board.json')
        self.assertEqual(1, catalog['total'])
        entry = catalog['items'][0]
        self.assertEqual('minimal', entry['layout'])
        self.assertEqual('2026-09-09T00:00:00Z', entry['acquisition_time'])
        passport = control.read(self.data / 'public' / entry['metadata'])
        self.assertFalse(passport['orientation']['north_up_verified'])
        doc = self.store.current()
        settings = doc['settings']
        settings['board']['hidden_sources'] = ['images']
        self.store.save(settings, doc['revision'])
        self.worker.tick()
        self.assertEqual(0, control.read(self.data / 'public/board.json')['total'])
        self.assertTrue((self.data / 'public' / entry['original']).exists())
        self.assertEqual(1, self.worker.db.execute('select count(*) from jobs').fetchone()[0])

    def native_fixture(self, minimal_only):
        from PIL import Image
        self.worker.close()
        self.cfg['sources'] = [{'id': 'products', 'kind': 'product', 'path': str(self.data / 'inbox/products'), 'require_ready': True}]
        control.atomic(self.config / 'station.json', self.cfg)
        self.worker = self.Worker(self.station.load_config(self.config / 'station.json'))
        product = self.data / 'inbox/products/pass'
        product.mkdir()
        (product / 'product.cbor').write_bytes(b'fixture; native decoder intentionally mocked')
        (product / '.ready').touch()
        def native(argv, work, log):
            output = Path(argv[2])
            for suffix, kind in [('minimal', 'minimal')] + ([] if minimal_only else [('presentation', 'editorial')]):
                path = output / ('product_annotated_' + suffix + '.png')
                Image.new('RGB', (32, 20)).save(str(path))
                control.atomic(path.with_suffix('.json'), {'schema': 'satdump.presentation/2', 'layout': kind, 'pass': {'product': 'RGB 321'}})
        self.worker.native = native
        self.ingest()
        return control.read(self.data / 'public/board.json')['items']

    def test_native_editorial_and_minimal_discovery(self):
        self.assertEqual(['editorial', 'minimal'], sorted(e['layout'] for e in self.native_fixture(False)))

    def test_native_minimal_only_is_valid(self):
        self.assertEqual(['minimal'], [e['layout'] for e in self.native_fixture(True)])

    def test_public_headless_and_private_paths(self):
        server = self.Server(('127.0.0.1', 0), self.data / 'public')
        thread = threading.Thread(target=server.serve_forever)
        thread.daemon = True
        thread.start()
        try:
            base = 'http://127.0.0.1:' + str(server.server_port)
            with urlopen(base + '/') as response:
                self.assertFalse(json.loads(response.read().decode())['ui_installed'])
            with urlopen(base + '/api/v1/board') as response:
                self.assertEqual('satdump.board/1', json.loads(response.read().decode())['schema'])
            for path in ('/api/v1/control/config', '/state/queue.sqlite3', '/app.js', '/items/../../control/active.json'):
                with self.assertRaises(HTTPError) as caught:
                    urlopen(base + path)
                self.assertEqual(404, caught.exception.code)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(3)


if __name__ == '__main__':
    unittest.main()
