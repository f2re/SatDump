# -*- coding: utf-8 -*-
"""Synthetic contract tests. These do NOT claim real RF decoding or native Astra acceptance."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import threading
import unittest
import subprocess
import time
from urllib.request import urlopen, Request
from urllib.error import HTTPError
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('station', str(ROOT / 'services/station/station.py'))
station = importlib.util.module_from_spec(spec)
spec.loader.exec_module(station)
pspec = importlib.util.spec_from_file_location('pack', str(ROOT / 'scripts/station/pack.py'))
pack = importlib.util.module_from_spec(pspec)
pspec.loader.exec_module(pack)


class TestStation(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix='station-test-'))
        self.inbox = self.tmp / 'inbox'
        self.inbox.mkdir()
        self.data = self.tmp / 'data'
        self.config = self.tmp / 'station.json'
        # Adapter tests substitute native() explicitly. They must not depend on a
        # previously installed engine (Path.resolve is strict on Python 3.5).
        self.cfg = {'schema': 'satdump.station/1', 'data_dir': str(self.data), 'settle_seconds': 0,
                    'engine': sys.executable, 'min_free_mb': 0, 'max_attempts': 2,
                    'sources': [{'id': 'test', 'path': str(self.inbox), 'kind': 'image'}]}
        station.atomic_json(self.config, self.cfg)
        station.atomic_json(self.tmp / 'processing.json', {'satdump_general': {'presentation_enabled': {'value': True}}})
        station.STOP.clear()
        self.worker = station.Worker(station.load_config(self.config))
        self.worker.recover()

    def tearDown(self):
        self.worker.close()
        shutil.rmtree(str(self.tmp))

    def save(self, name='pass.png', color=(45, 90, 150), size=(800, 400)):
        path = self.inbox / name
        Image.new('RGB', size, color).save(str(path))
        return path

    def ingest(self):
        self.worker.tick()
        self.worker.tick()

    def catalog(self):
        return station.read_json(self.data / 'public/catalog.json')['items']

    def test_image_publication_preserves_original(self):
        original = self.save()
        digest = pack.sha256(original)
        self.ingest()
        entries = self.catalog()
        self.assertEqual(1, len(entries))
        self.assertEqual(digest, pack.sha256(original))
        self.assertEqual(digest, pack.sha256(self.data / 'public' / entries[0]['original']))
        self.assertFalse(entries[0]['native_presentation'])
        self.assertEqual('', entries[0]['acquisition_time'])
        with Image.open(str(self.data / 'public' / entries[0]['preview'])) as preview:
            self.assertEqual((800, 400), preview.size)
        self.assertEqual(1, len(list((self.data / 'archive').iterdir())))
        self.assertEqual([], list((self.data / 'work').iterdir()))

    def test_idempotence_and_restart(self):
        self.save()
        self.ingest()
        self.worker.close()
        self.worker = station.Worker(station.load_config(self.config))
        self.worker.recover()
        self.ingest()
        self.assertEqual(1, len(self.catalog()))
        self.assertEqual(1, self.worker.db.execute('select count(*) from jobs').fetchone()[0])

    def test_changed_bytes_make_new_product(self):
        self.save()
        self.ingest()
        self.save(color=(1, 2, 3))
        self.ingest()
        self.assertEqual(2, len(self.catalog()))

    def test_sidecar_arriving_later_enqueues_new_snapshot(self):
        path = self.save()
        self.ingest()
        station.atomic_json(path.with_suffix('.json'), {'schema': 'satdump.presentation/2',
                            'pass': {'satellite': 'Метеор-М', 'acquisition_time': '2026-09-08 10:00 UTC'}})
        self.ingest()
        self.assertEqual('Метеор-М', self.catalog()[0]['satellite'])
        self.assertEqual(2, len(self.catalog()))

    def test_recipe_change_reprocesses(self):
        self.save()
        self.ingest()
        self.worker.patch['revision_test'] = 2
        self.ingest()
        self.assertEqual(2, len(self.catalog()))

    def test_partial_and_symlink_not_ingested(self):
        image = self.save()
        os.rename(str(image), str(self.inbox / 'first.png.part'))
        (self.inbox / 'linked.png').symlink_to(self.inbox / 'first.png.part')
        self.ingest()
        self.assertEqual([], self.catalog())

    def test_changed_input_resets_settle_window(self):
        self.save()
        self.worker.scan()
        self.save(color=(10, 10, 10))
        self.worker.scan()
        self.assertEqual(0, self.worker.db.execute('select count(*) from jobs').fetchone()[0])
        self.worker.scan()
        self.assertEqual(1, self.worker.db.execute('select count(*) from jobs').fetchone()[0])

    def test_bad_image_does_not_block_next(self):
        (self.inbox / 'bad.png').write_bytes(b'not an image')
        self.save('good.png')
        self.ingest()
        self.worker.tick()
        self.assertEqual(1, len(self.catalog()))
        self.assertEqual('pending', self.worker.db.execute("select state from jobs where path like '%bad.png'").fetchone()[0])

    def test_ready_marker_for_products_and_native_adapter(self):
        source = self.worker.sources['test']
        source.update({'kind': 'product', 'require_ready': True})
        directory = self.inbox / 'pass 01'
        directory.mkdir()
        (directory / 'product.cbor').write_bytes(b'synthetic contract fixture, not CBOR')
        for index in (1, 2, 3):
            Image.new('L', (80, 40), index * 60).save(str(directory / ('ch' + str(index) + '.png')))
        self.ingest()
        self.assertEqual([], self.catalog())
        calls = []
        def fake_native(argv, work, log):
            calls.append(argv)
            self.assertEqual('reprocess', argv[1])
            self.assertNotEqual(str(directory), argv[2])
            self.assertTrue(station.read_json(argv[3])['satdump_general']['presentation_enabled']['value'])
            output = Path(argv[2]) / 'msumr_rgb_test_annotated_presentation.png'
            channels = [Image.open(str(Path(argv[2]) / ('ch' + str(i) + '.png'))) for i in (1, 2, 3)]
            Image.merge('RGB', channels).save(str(output))
            for channel in channels:
                channel.close()
            station.atomic_json(output.with_suffix('.json'), {'schema': 'satdump.presentation/2',
                'pass': {'satellite': 'Синтетический тест', 'instrument': 'MSU-MR', 'product': 'Тест RGB'},
                'legend': {'kind': 'composite', 'notes': ['Не наблюдение']}})
        self.worker.native = fake_native
        (directory / '.ready').touch()
        self.ingest()
        self.assertEqual(1, len(calls))
        self.assertTrue(self.catalog()[0]['native_presentation'])
        self.assertFalse(any(directory.glob('*annotated*')))

    def test_native_empty_output_is_failure(self):
        source = self.worker.sources['test']
        source.update({'kind': 'pipeline', 'pipeline': 'invalid', 'input_level': 'baseband', 'patterns': ['*.cs16']})
        (self.inbox / 'pass.cs16').write_bytes(b'test')
        self.worker.native = lambda *_: None
        self.ingest()
        self.assertEqual([], self.catalog())
        error = self.worker.db.execute('select error from jobs').fetchone()[0]
        self.assertIn('No native presentation', error)

    def test_crash_after_publication_recovers_without_duplicate(self):
        self.save()
        self.worker.scan(); self.worker.scan()
        job = self.worker.db.execute('select * from jobs').fetchone()
        self.worker.process(job)
        self.worker.db.execute("update jobs set state='running'")
        self.worker.db.commit()
        self.worker.recover()
        self.worker.tick()
        self.assertEqual('done', self.worker.db.execute('select state from jobs').fetchone()[0])
        self.assertEqual(1, len(self.catalog()))

    def test_web_read_only_and_traversal(self):
        self.save(); self.ingest()
        server = station.GalleryServer(('127.0.0.1', 0), self.data / 'public')
        thread = threading.Thread(target=server.serve_forever)
        thread.daemon = True; thread.start()
        base = 'http://127.0.0.1:' + str(server.server_port)
        try:
            with urlopen(base + '/') as response:
                self.assertIn(b'SATDUMP', response.read())
                self.assertIn("script-src 'self'", response.headers['Content-Security-Policy'])
            with urlopen(base + '/health.json') as response:
                self.assertTrue(json.loads(response.read().decode())['worker_alive'])
            for path in ('/../state/queue.sqlite3', '/%2e%2e/state/queue.sqlite3', '/items/', '/station.json', '/%00'):
                with self.assertRaises(HTTPError) as error:
                    urlopen(base + path)
                self.assertEqual(404, error.exception.code)
            with self.assertRaises(HTTPError) as error:
                urlopen(Request(base + '/', data=b'write', method='POST'))
            self.assertEqual(405, error.exception.code)
        finally:
            server.shutdown(); server.server_close(); thread.join(3)

    def test_overlapping_input_rejected(self):
        self.cfg['sources'][0]['path'] = str(self.data / 'public')
        station.atomic_json(self.config, self.cfg)
        with self.assertRaises(ValueError):
            station.load_config(self.config)

    def test_malformed_sidecar_is_not_published(self):
        path = self.save()
        path.with_suffix('.json').write_text('{bad json')
        self.ingest()
        self.assertEqual([], self.catalog())

    def test_retry_exhaustion_is_terminal(self):
        (self.inbox / 'bad.png').write_bytes(b'corrupt')
        self.ingest()
        self.worker.db.execute('update jobs set next_at=0'); self.worker.db.commit()
        self.worker.tick()
        job = self.worker.db.execute('select state,attempts from jobs').fetchone()
        self.assertEqual(('failed', 2), tuple(job))
        self.worker.tick()
        self.assertEqual(2, self.worker.db.execute('select attempts from jobs').fetchone()[0])

    def test_input_changed_after_queue_is_superseded(self):
        self.save(); self.worker.scan(); self.worker.scan()
        self.save(color=(230, 10, 20))
        self.worker.tick()
        self.assertEqual('superseded', self.worker.db.execute('select state from jobs').fetchone()[0])
        self.worker.tick()
        self.assertEqual(1, len(self.catalog()))

    def test_ready_removed_after_queue_prevents_processing(self):
        path = self.save()
        self.worker.sources['test']['require_ready'] = True
        marker = Path(str(path) + '.ready'); marker.touch()
        self.worker.scan(); self.worker.scan(); marker.unlink()
        self.worker.tick()
        self.assertEqual([], self.catalog())
        self.assertIn('marker', self.worker.db.execute('select error from jobs').fetchone()[0])

    def test_native_timeout_kills_process_group(self):
        work = self.tmp / 'native'; work.mkdir()
        self.worker.cfg['timeout_seconds'] = 1
        with self.assertRaises(RuntimeError):
            self.worker.native(['/bin/sh', '-c', 'sleep 30'], work, work / 'native.log')

    def test_once_discovers_and_publishes(self):
        self.save()
        subprocess.check_call([sys.executable, str(ROOT / 'services/station/station.py'),
                               'once', '--config', str(self.config)])
        self.assertEqual(1, len(self.catalog()))

    def test_stale_worker_not_reported_alive(self):
        station.atomic_json(self.data / 'public/worker.json', {'updated_at': time.time() - 500, 'stale_after': 60})
        server = station.GalleryServer(('127.0.0.1', 0), self.data / 'public')
        thread = threading.Thread(target=server.serve_forever); thread.daemon = True; thread.start()
        try:
            with urlopen('http://127.0.0.1:' + str(server.server_port) + '/health.json') as response:
                status = json.loads(response.read().decode())
            self.assertTrue(status['web_alive'])
            self.assertFalse(status['worker_alive'])
        finally:
            server.shutdown(); server.server_close(); thread.join(3)

    def test_checksum_verification_detects_changes_and_extras(self):
        payload = self.tmp / 'payload'
        payload.mkdir()
        file = payload / 'file.txt'
        file.write_text('one')
        (payload / 'SHA256SUMS').write_text(pack.sha256(file) + '  file.txt\n')
        pack.verify(payload)
        (payload / 'extra.txt').write_text('extra')
        with self.assertRaises(ValueError):
            pack.verify(payload)

    def test_manifest_external_symlink_rejected(self):
        payload = self.tmp / 'payload'; payload.mkdir()
        (payload / 'link').symlink_to('/etc/passwd')
        with self.assertRaises(ValueError):
            list(pack.paths(payload))

    def test_pack_creates_output_before_resolving(self):
        target = self.tmp / 'new' / 'nested-output'
        self.assertFalse(target.exists())
        self.assertEqual(target.resolve() if target.exists() else target,
                         pack.output_directory(str(target)))
        self.assertTrue(target.is_dir())


if __name__ == '__main__':
    unittest.main()
