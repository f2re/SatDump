#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Atomic publication across filesystems and systemd per-directory bind mounts."""
import errno
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'services/station'))
import board


def load_station():
    spec = importlib.util.spec_from_file_location('crossmount_station', str(ROOT / 'services/station/station.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.STOP.clear()
    return module


class CrossMountTests(unittest.TestCase):
    def setUp(self):
        self.station = load_station()
        self.tmp = tempfile.TemporaryDirectory(prefix='satdump-transfer-')
        self.root = Path(self.tmp.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        (self.source / 'item.json').write_text('{"entries": []}')
        (self.source / 'data').write_bytes(b'original bytes\x00')
        self.public = self.root / 'public'
        self.public.mkdir()
        self.dest = self.public / 'aabbcc'
        self.rename = os.rename

    def tearDown(self):
        self.station.STOP.clear()
        self.tmp.cleanup()

    def cross_device(self, source, destination):
        if source == str(self.source):
            raise OSError(errno.EXDEV, 'simulated independent systemd bind mount')
        # Staging has a private wrapper; discovery must not see item.json early.
        self.assertFalse(self.dest.exists())
        self.assertEqual([], list(self.public.glob('*/item.json')))
        return self.rename(source, destination)

    def test_fallback_is_atomic_and_preserves_bytes(self):
        with mock.patch.object(self.station.os, 'rename', side_effect=self.cross_device):
            self.station.move_tree(self.source, self.dest)
        self.assertEqual(b'original bytes\x00', (self.dest / 'data').read_bytes())
        self.assertFalse(self.source.exists())
        self.assertEqual([self.dest], list(self.public.iterdir()))

    def test_same_mount_uses_rename_without_copy(self):
        with mock.patch.object(self.station.shutil, 'copytree', side_effect=AssertionError('unneeded copy')):
            self.station.move_tree(self.source, self.dest)
        self.assertEqual(b'original bytes\x00', (self.dest / 'data').read_bytes())

    def test_permission_error_is_not_retried_as_copy(self):
        with mock.patch.object(self.station.os, 'rename', side_effect=OSError(errno.EACCES, 'permission')):
            with mock.patch.object(self.station.shutil, 'copytree') as copy:
                with self.assertRaises(OSError):
                    self.station.move_tree(self.source, self.dest)
                copy.assert_not_called()
        self.assertTrue(self.source.exists())
        self.assertFalse(self.dest.exists())

    def test_copy_failure_preserves_source_and_hides_partial_tree(self):
        def fail_copy(source, target, **unused):
            Path(target).mkdir()
            (Path(target) / 'item.json').write_text('{"partial": true}')
            self.assertEqual([], list(self.public.glob('*/item.json')))
            raise OSError(errno.ENOSPC, 'disk full')
        with mock.patch.object(self.station.os, 'rename', side_effect=self.cross_device):
            with mock.patch.object(self.station.shutil, 'copytree', side_effect=fail_copy):
                with self.assertRaises(OSError):
                    self.station.move_tree(self.source, self.dest)
        self.assertEqual(b'original bytes\x00', (self.source / 'data').read_bytes())
        self.assertEqual([], list(self.public.iterdir()))

    def test_commit_failure_preserves_source_and_removes_staging(self):
        def fail_commit(source, destination):
            raise OSError(errno.EXDEV if source == str(self.source) else errno.EACCES, 'rejected')
        with mock.patch.object(self.station.os, 'rename', side_effect=fail_commit):
            with self.assertRaises(OSError):
                self.station.move_tree(self.source, self.dest)
        self.assertEqual(b'original bytes\x00', (self.source / 'data').read_bytes())
        self.assertEqual([], list(self.public.iterdir()))

    def test_existing_destination_is_never_replaced(self):
        self.dest.mkdir()
        (self.dest / 'saved').write_text('keep')
        with self.assertRaises(ValueError):
            self.station.move_tree(self.source, self.dest)
        self.assertEqual('keep', (self.dest / 'saved').read_text())
        self.assertTrue(self.source.exists())

    def test_links_and_special_files_are_rejected(self):
        (self.source / 'link').symlink_to('/etc/passwd')
        with self.assertRaises(ValueError):
            self.station.move_tree(self.source, self.dest)
        (self.source / 'link').unlink()
        os.mkfifo(str(self.source / 'fifo'))
        with self.assertRaises(ValueError):
            self.station.move_tree(self.source, self.dest)

    def test_interruption_does_not_commit(self):
        self.station.STOP.set()
        with self.assertRaises(RuntimeError):
            self.station.move_tree(self.source, self.dest)
        self.assertTrue(self.source.exists())
        self.assertEqual([], list(self.public.iterdir()))

    def test_actual_cross_filesystem_transfer(self):
        alternate = Path('/dev/shm')
        if not alternate.is_dir() or alternate.stat().st_dev == self.source.stat().st_dev:
            self.skipTest('no independent writable filesystem')
        with tempfile.TemporaryDirectory(prefix='satdump-crossfs-', dir=str(alternate)) as target:
            destination = Path(target) / 'result'
            with self.assertRaises(OSError) as error:
                self.rename(str(self.source), str(destination))
            self.assertEqual(errno.EXDEV, error.exception.errno)
            self.station.move_tree(self.source, destination)
            self.assertEqual(b'original bytes\x00', (destination / 'data').read_bytes())
            self.assertFalse(self.source.exists())

    def run_worker_flow(self, with_board):
        from PIL import Image
        station = load_station()
        worker_class = board.install_adapter(station)[0] if with_board else station.Worker
        data, inbox = self.root / 'data', self.root / 'inbox'
        inbox.mkdir()
        image = inbox / 'pass.png'
        Image.new('RGB', (24, 32)).save(str(image))
        original = image.read_bytes()
        metadata = {'schema': 'satdump.presentation/2', 'layout': 'minimal',
                    'pass': {'satellite': 'test', 'acquisition_time': '2000-01-01T00:00:00Z'}}
        station.atomic_json(image.with_suffix('.json'), metadata)
        config = self.root / 'station.json'
        station.atomic_json(config, {'schema': 'satdump.station/1', 'data_dir': str(data),
            'engine': sys.executable, 'min_free_mb': 0, 'settle_seconds': 0,
            'sources': [{'id': 'test', 'kind': 'image', 'path': str(inbox)}]})
        station.atomic_json(self.root / 'processing.json', {})
        worker = worker_class(station.load_config(config))
        failures = []
        def mounted_rename(source, destination):
            if source.startswith(str(data / 'work') + '/') and not destination.startswith(str(data / 'work') + '/'):
                failures.append((source, destination))
                raise OSError(errno.EXDEV, 'systemd bind mount')
            return self.rename(source, destination)
        try:
            worker.recover()
            with mock.patch.object(station.os, 'rename', side_effect=mounted_rename):
                worker.tick()
                worker.tick()
            self.assertEqual(2, len(failures))
            self.assertEqual('done', worker.db.execute('SELECT state FROM jobs').fetchone()[0])
            catalog = station.read_json(data / 'public' / ('board.json' if with_board else 'catalog.json'))
            self.assertEqual(1, len(catalog['items']))
            entry = catalog['items'][0]
            self.assertEqual(metadata, station.read_json(data / 'public' / entry['metadata']))
            self.assertEqual(original, (data / 'public' / entry['original']).read_bytes())
            self.assertEqual(original, image.read_bytes())
            self.assertEqual([], list((data / 'work').iterdir()))
        finally:
            worker.close()

    def test_legacy_worker_cross_mount_publication(self):
        self.run_worker_flow(False)

    def test_board_worker_cross_mount_publication(self):
        self.run_worker_flow(True)


if __name__ == '__main__':
    unittest.main()
