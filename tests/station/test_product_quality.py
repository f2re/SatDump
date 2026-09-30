# -*- coding: utf-8 -*-
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'services/station'))
import station
import board
import product_quality as quality
from PIL import Image


class QualityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.source = {'id': 'test', 'kind': 'product', 'path': str(self.root)}

    def tearDown(self):
        self.temp.cleanup()

    def report(self, name, value):
        p = self.root / name
        station.atomic_json(p, value)
        return p

    def processing(self, instrument, status='ok', generated=1):
        return self.report(instrument + '/processing-status.json', {
            'schema': 'satdump.processing-status/1', 'instrument': instrument,
            'status': status, 'generated': generated})

    def image(self, instrument):
        p = self.root / instrument / 'fixture_annotated_presentation.png'
        p.parent.mkdir(exist_ok=True)
        Image.new('RGB', (8, 8)).save(str(p))
        return p

    def test_optional_missing_instrument_is_visible(self):
        self.report('decode-status.json', {'schema': 'satdump.decode-status/1', 'instruments': [
            {'instrument': 'mtvza', 'status': 'no_data', 'lines': 0}]})
        self.processing('msu_mr')
        result = quality.assess(self.root, self.source, [self.image('msu_mr')], station.read_json)
        self.assertEqual('partial', result['status'])
        self.assertIn('mtvza:no_data', result['warnings'])
        self.assertEqual([], result['missing_required'])

    def test_required_missing_is_not_masked_by_other_images(self):
        self.source['required_instruments'] = ['mtvza']
        self.processing('msu_mr')
        result = quality.assess(self.root, self.source, [self.image('msu_mr')], station.read_json)
        self.assertEqual(['mtvza'], result['missing_required'])

    def test_report_without_actual_presentation_does_not_satisfy_required(self):
        self.source['required_instruments'] = ['mtvza']
        self.processing('mtvza')
        self.assertEqual(['mtvza'], quality.assess(self.root, self.source, [], station.read_json)['missing_required'])

    def test_good_required_instrument(self):
        self.source['required_instruments'] = ['mtvza']
        self.processing('mtvza')
        result = quality.assess(self.root, self.source, [self.image('mtvza')], station.read_json)
        self.assertEqual('ok', result['status'])

    def test_partial_native_error_is_not_silent(self):
        self.processing('mtvza', 'partial')
        result = quality.assess(self.root, self.source, [self.image('mtvza')], station.read_json, ['native exit 1'])
        self.assertEqual('partial', result['status'])
        self.assertIn('native_processing_error', result['warnings'])

    def test_old_engine_reports_unverified_not_complete(self):
        result = quality.assess(self.root, self.source, [self.image('mtvza')], station.read_json)
        self.assertEqual('partial', result['status'])
        self.assertIn('instrument_completeness_unverified', result['warnings'])

    def test_invalid_receipt_is_rejected(self):
        self.report('processing-status.json', {'schema': 'unknown'})
        with self.assertRaises(ValueError):
            quality.assess(self.root, self.source, [], station.read_json)

    def test_required_configuration(self):
        for value in ['mtvza', ['mtvza', 'mtvza'], ['../private'], [True], [None]]:
            self.source['required_instruments'] = value
            with self.assertRaises(ValueError):
                quality.validate_required(self.source)
        self.source.update(kind='image', required_instruments=['mtvza'])
        with self.assertRaises(ValueError):
            quality.validate_required(self.source)


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.inbox = self.root / 'input'
        self.inbox.mkdir()
        station.STOP.clear()
        cfg = {'schema': 'satdump.station/1', 'data_dir': str(self.root / 'data'),
               'settle_seconds': 0, 'min_free_mb': 0, 'engine': sys.executable,
               'sources': [{'id': 'images', 'kind': 'image', 'path': str(self.inbox)}]}
        station.atomic_json(self.root / 'station.json', cfg)
        station.atomic_json(self.root / 'processing.json', {})
        self.cls, unused = board.install_adapter(station)
        self.worker = self.cls(station.load_config(self.root / 'station.json'))
        Image.new('RGB', (8, 8)).save(str(self.inbox / 'fixture.png'))

    def tearDown(self):
        self.worker.close()
        station.STOP.clear()
        self.temp.cleanup()

    def ingest(self):
        self.worker.tick(); self.worker.tick()
        return self.worker.db.execute('SELECT * FROM jobs').fetchone()

    def test_missing_asset_recovered_without_duplicate_job(self):
        job = self.ingest()
        final = self.root / 'data/public/items' / job['id']
        original = next(final.glob('*.png'))
        original.unlink()
        self.worker.close()
        self.worker = self.cls(station.load_config(self.root / 'station.json'))
        self.worker.recover()
        self.assertEqual('pending', self.worker.db.execute('SELECT state FROM jobs').fetchone()[0])
        self.worker.tick()
        self.assertTrue(self.worker.publication_complete(job['id']))
        self.assertEqual(1, self.worker.db.execute('SELECT count(*) FROM jobs').fetchone()[0])
        self.assertTrue(list((self.root / 'data/work').glob('quarantine-*')))

    def test_manifest_alone_is_not_a_complete_publication(self):
        job = self.ingest()
        manifest_path = self.root / 'data/public/items' / job['id'] / 'item.json'
        manifest = station.read_json(manifest_path)
        manifest['entries'][0]['original'] = 'items/' + job['id'] + '/../secret.png'
        station.atomic_json(manifest_path, manifest)
        self.assertFalse(self.worker.publication_complete(job['id']))

    def test_stop_does_not_consume_attempt(self):
        self.worker.scan(); self.worker.scan()
        def interrupt(job):
            station.STOP.set()
            raise RuntimeError('service stop')
        self.worker.process = interrupt
        self.worker.tick()
        job = self.worker.db.execute('SELECT * FROM jobs').fetchone()
        self.assertEqual('pending', job['state'])
        self.assertEqual(0, job['attempts'])

    def test_completed_publication_recovers_missing_database_commit(self):
        job = self.ingest()
        self.worker.db.execute("UPDATE jobs SET state='running'")
        self.worker.db.commit()
        self.worker.recover()
        self.worker.tick()
        self.assertEqual('done', self.worker.db.execute('SELECT state FROM jobs').fetchone()[0])
        self.assertEqual(1, len(list((self.root / 'data/public/items').iterdir())))

    def test_worker_forces_automatic_pipeline_processing(self):
        self.assertTrue(self.worker.patch['satdump_general']['auto_process_products']['value'])


if __name__ == '__main__':
    unittest.main()
