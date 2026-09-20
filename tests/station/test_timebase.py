# -*- coding: utf-8 -*-
"""Clock and queue regression tests; the native executable is a test fixture."""
import json
import os
from pathlib import Path
import shlex
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'services/station'))
import timebase
import station
import board


class UTCFields(unittest.TestCase):
    def test_offset_to_utc(self):
        value = timebase.acquisition_fields('2026-09-15T02:30:00+03:00')
        self.assertEqual('2026-09-14T23:30:00Z', value['acquisition_start_utc'])
        self.assertEqual('14.09.2026 · 23:30:00 UTC', value['acquisition_time'])

    def test_native_single(self):
        value = timebase.acquisition_fields('15.09.2026 · 00:12:30 UTC')
        self.assertEqual('2026-09-15T00:12:30Z', value['acquisition_end_utc'])

    def test_native_same_day(self):
        value = timebase.acquisition_fields('15.09.2026 · 10:00:00–10:15:00 UTC')
        self.assertEqual(900, value['acquisition_end'] - value['acquisition_start'])

    def test_native_midnight(self):
        raw = '14.09.2026 23:55:00 – 15.09.2026 00:10:00 UTC'
        value = timebase.acquisition_fields(raw)
        self.assertEqual(raw, value['acquisition_time'])
        self.assertEqual(900, value['acquisition_end'] - value['acquisition_start'])

    def test_no_implicit_zone_or_guessed_date(self):
        for raw in ['2026-09-15T10:00:00', '15.09.2026 · 23:59:00–00:10:00 UTC',
                    '2026-02-30T12:00:00Z', '2026-09-15T10:00:00+25:00']:
            value = timebase.acquisition_fields(raw)
            self.assertEqual('invalid', value['acquisition_time_status'], raw)
            self.assertIsNone(value['acquisition_start'])
            self.assertEqual('', value['acquisition_time'])

    def test_missing_time_is_not_mtime(self):
        for raw in ['', 'Время наблюдения не указано']:
            value = timebase.normalize_entry({'acquisition_time': raw, 'file_mtime': 1789452000})
            self.assertEqual('missing', value['acquisition_time_status'])
            self.assertIsNone(value['acquisition_start'])

    def test_fractional_seconds(self):
        value = timebase.acquisition_fields('2026-09-15T10:00:00.125+01:30')
        self.assertEqual('2026-09-15T08:30:00.125000Z', value['acquisition_start_utc'])

    def test_timezone_independence(self):
        code = ('import sys,json; sys.path.insert(0,sys.argv[1]); import timebase; '
                'print(json.dumps(timebase.acquisition_fields("2026-09-15T01:00:00+03:00"),sort_keys=True))')
        outputs = []
        for zone in ['UTC', 'Europe/Helsinki', 'America/New_York', 'Pacific/Honolulu']:
            env = os.environ.copy(); env['TZ'] = zone
            outputs.append(subprocess.check_output([sys.executable, '-c', code, str(ROOT / 'services/station')], env=env))
        self.assertTrue(all(value == outputs[0] for value in outputs))

    def test_catalog_order_uses_observation_not_reprocessing_time(self):
        old = timebase.normalize_entry({'id': 'old', 'published_at': 9999999999,
                                       'acquisition_time': '2026-09-14T10:00:00Z'})
        new = timebase.normalize_entry({'id': 'new', 'published_at': 1,
                                       'acquisition_time': '2026-09-15T10:00:00Z'})
        self.assertEqual('new', sorted([old, new], key=timebase.catalog_order, reverse=True)[0]['id'])

    def test_monotonic_health_ignores_wall_clock_jump(self):
        status = {'boot_id': 'same', 'updated_monotonic': 100, 'updated_at': 1000, 'stale_after': 60}
        with mock.patch.object(timebase, 'boot_id', return_value='same'), mock.patch.object(timebase.time, 'monotonic', return_value=110):
            for wall in [0, 1e12]:
                with mock.patch.object(timebase.time, 'time', return_value=wall):
                    self.assertTrue(timebase.heartbeat_alive(status))
        with mock.patch.object(timebase, 'boot_id', return_value='other'):
            self.assertFalse(timebase.heartbeat_alive(status))

    def test_legacy_future_health_is_not_alive(self):
        with mock.patch.object(timebase.time, 'time', return_value=100):
            self.assertFalse(timebase.heartbeat_alive({'updated_at': 500, 'stale_after': 60}))

    def test_ntp_active_is_not_synchronized(self):
        output = b'NTP service: active\nSystem clock synchronized: no\nTime zone: Europe/Helsinki\n'
        with mock.patch.object(timebase.subprocess, 'check_output', return_value=output):
            status = timebase.clock_status()
        self.assertFalse(status['synchronized'])
        self.assertEqual('UTC', status['display_timezone'])

    def test_unavailable_sync_status_is_unknown(self):
        with mock.patch.object(timebase.subprocess, 'check_output', side_effect=OSError):
            self.assertIsNone(timebase.clock_status()['synchronized'])


class SchedulerClock(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix='satdump-time-'))
        self.inbox = self.tmp / 'inbox'; self.inbox.mkdir()
        self.cfg = {'schema': 'satdump.station/1', 'data_dir': str(self.tmp / 'data'),
                    'engine': sys.executable, 'settle_seconds': 0, 'min_free_mb': 0,
                    'max_attempts': 3, 'poll_seconds': 1,
                    'sources': [{'id': 'images', 'kind': 'image', 'path': str(self.inbox)}]}
        station.atomic_json(self.tmp / 'station.json', self.cfg)
        station.atomic_json(self.tmp / 'processing.json', {})
        station.STOP.clear()
        self.worker = station.Worker(station.load_config(self.tmp / 'station.json'))
        from PIL import Image
        Image.new('RGB', (4, 4)).save(str(self.inbox / 'fixture.png'))
        self.worker.scan(); self.worker.scan()

    def tearDown(self):
        self.worker.close()
        shutil.rmtree(str(self.tmp))

    def fail_once(self):
        self.worker.process = mock.Mock(side_effect=RuntimeError('test fixture failure'))
        with mock.patch.object(station.time, 'time', return_value=1000), mock.patch.object(station.time, 'monotonic', return_value=100):
            self.assertTrue(self.worker.tick())
        return self.worker.db.execute('SELECT * FROM jobs').fetchone()

    def test_retry_waits_for_monotonic_deadline(self):
        job = self.fail_once()
        self.assertEqual(160, job['retry_at'])
        self.worker.process = mock.Mock()
        for wall in [0, 1e12]:
            with mock.patch.object(station.time, 'time', return_value=wall), mock.patch.object(station.time, 'monotonic', return_value=110):
                self.assertFalse(self.worker.tick())
        self.worker.process.assert_not_called()
        with mock.patch.object(station.time, 'monotonic', return_value=160):
            self.assertTrue(self.worker.tick())
        self.assertEqual(1, self.worker.process.call_count)

    def test_restart_on_same_boot_preserves_delay(self):
        self.fail_once(); self.worker.close()
        self.worker = station.Worker(station.load_config(self.tmp / 'station.json'))
        self.worker.recover(); self.worker.process = mock.Mock()
        with mock.patch.object(station.time, 'monotonic', return_value=110), mock.patch.object(station.time, 'time', return_value=1e12):
            self.assertFalse(self.worker.tick())
        self.worker.process.assert_not_called()

    def test_reboot_does_not_wait_for_old_clock(self):
        self.fail_once(); self.worker.boot_scope = 'different-boot'; self.worker.process = mock.Mock()
        with mock.patch.object(station.time, 'monotonic', return_value=1):
            self.assertTrue(self.worker.tick())
        self.assertEqual(1, self.worker.process.call_count)

    def test_new_file_still_detected_after_clock_moves_back(self):
        self.worker.process = mock.Mock()
        with mock.patch.object(station.time, 'time', return_value=-1000):
            self.assertTrue(self.worker.tick())
        self.assertEqual('done', self.worker.db.execute('SELECT state FROM jobs').fetchone()[0])

    def test_sqlite_upgrade_preserves_jobs(self):
        self.worker.close()
        db = sqlite3.connect(str(self.tmp / 'data/state/queue.sqlite3'))
        db.execute('ALTER TABLE jobs RENAME TO previous_jobs')
        db.execute("CREATE TABLE jobs (id TEXT PRIMARY KEY, source TEXT, path TEXT, signature TEXT, state TEXT, attempts INTEGER DEFAULT 0, next_at REAL DEFAULT 0, error TEXT DEFAULT '', created REAL, finished REAL)")
        db.execute('INSERT INTO jobs SELECT id,source,path,signature,state,attempts,next_at,error,created,finished FROM previous_jobs')
        db.execute('DROP TABLE previous_jobs'); db.commit(); db.close()
        self.worker = station.Worker(station.load_config(self.tmp / 'station.json'))
        self.assertEqual(1, self.worker.db.execute('SELECT count(*) FROM jobs').fetchone()[0])
        self.assertIn('retry_boot', [row[1] for row in self.worker.db.execute('PRAGMA table_info(jobs)')])


class PipelineIntegration(unittest.TestCase):
    def test_ready_pipeline_publication_and_input_mtime(self):
        tmp = Path(tempfile.mkdtemp(prefix='satdump-pipeline-'))
        worker = None
        try:
            inbox = tmp / 'receiver'; inbox.mkdir()
            raw = inbox / 'fixture.cadu'; raw.write_bytes(b'unit-test-fixture')
            original_ns = 1600000000123456789
            os.utime(str(raw), ns=(original_ns, original_ns))
            engine = tmp / 'engine'
            fixture = tmp / 'fixture.py'
            fixture.write_text('''import json,os,sys
from pathlib import Path
from PIL import Image
out=Path(sys.argv[4]);out.mkdir(parents=True)
(out/'invocation.json').write_text(json.dumps({'argv':sys.argv[1:],'tz':os.environ.get('TZ'),'mtime_ns':Path(sys.argv[3]).stat().st_mtime_ns}))
for kind in ['presentation','minimal']:
 p=out/('test_annotated_'+kind+'.png')
 Image.new('RGB',(8,8)).save(str(p))
 p.with_suffix('.json').write_text(json.dumps({'schema':'satdump.presentation/2','layout':kind,'pass':{'satellite':'Test fixture','acquisition_time':'2026-09-14T23:59:00Z'}}))
''')
            # native() deliberately clears the caller's Python loader variables.
            # The bundled launcher restores its own libraries; no system Python is required.
            interpreter = ROOT / 'runtime/python'
            if not interpreter.is_file():
                interpreter = Path(sys.executable)
            engine.write_text('#!/bin/sh\nexec ' + shlex.quote(str(interpreter)) + ' ' +
                              shlex.quote(str(fixture)) + ' "$@"\n')
            os.chmod(str(engine), 0o755)
            cfg = {'schema': 'satdump.station/1', 'data_dir': str(tmp / 'data'), 'engine': str(engine),
                   'settle_seconds': 0, 'min_free_mb': 0,
                   'sources': [{'id': 'raw', 'kind': 'pipeline', 'path': str(inbox),
                                'patterns': ['*.cadu'], 'pipeline': 'fixture', 'input_level': 'frames', 'require_ready': True}]}
            station.atomic_json(tmp / 'station.json', cfg); station.atomic_json(tmp / 'processing.json', {})
            cls, unused = board.install_adapter(station)
            worker = cls(station.load_config(tmp / 'station.json'))
            self.assertFalse(worker.tick())
            Path(str(raw) + '.ready').touch()
            worker.tick(); worker.tick()
            job = worker.db.execute('SELECT * FROM jobs').fetchone()
            self.assertEqual('done', job['state'], job['error'])
            invocation = station.read_json(tmp / 'data/archive' / job['id'] / 'invocation.json')
            self.assertEqual('UTC', invocation['tz'])
            self.assertEqual(original_ns, invocation['mtime_ns'])
            self.assertIn('--offline', invocation['argv'])
            self.assertIn('--processing_config', invocation['argv'])
            catalog = station.read_json(tmp / 'data/public/board.json')
            self.assertEqual(2, len(catalog['items']))
            self.assertEqual('2026-09-14T23:59:00Z', catalog['items'][0]['acquisition_start_utc'])
            self.assertEqual(original_ns, raw.stat().st_mtime_ns)
            worker.tick()
            self.assertEqual(1, worker.db.execute('SELECT count(*) FROM jobs').fetchone()[0])
        finally:
            if worker: worker.close()
            shutil.rmtree(str(tmp))


if __name__ == '__main__':
    unittest.main()
