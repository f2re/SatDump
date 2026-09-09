#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Regressions for installed-runtime selection and safe native component reuse."""
from __future__ import print_function
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('reuse_components', str(ROOT / 'scripts/station/reuse-components.py'))
reuse = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reuse)


class ReleasePipelineTests(unittest.TestCase):
    def test_service_probe_uses_installed_runtime(self):
        script = (ROOT / 'scripts/station/install.sh').read_text()
        assignment = script.index('PYTHON="$TARGET/runtime/python"')
        copied = script.index('mv "$STAGE" "$TARGET"')
        probe = script.index('runuser -u satdump-station -- "$PYTHON"')
        self.assertTrue(copied < assignment < probe)
        self.assertNotIn('PYTHON="$PACKAGE/runtime/python"', script[assignment:])

    def test_allow_only_station_repack_changes(self):
        for path in ('scripts/station/install.sh', 'services/station/board.py',
                     'tests/station/test_release_pipeline.py', 'docs/ru/RELEASES.md'):
            self.assertTrue(reuse.allowed_change(path), path)
        for path in ('scripts/station/bundle-python.sh', 'scripts/station/build.sh',
                     'scripts/astra/portable/inside-chroot.sh', 'CMakeLists.txt',
                     'src-core/products/image_products.cpp', 'resources/palettes/new.png',
                     'tests/presentation_render_test.cpp', 'satdump_cfg.json', '.gitmodules'):
            self.assertFalse(reuse.allowed_change(path), path)

    def test_plan_requires_pinned_source_and_archive(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'cache.json'
            path.write_text(json.dumps({'schema': 'satdump.station.component-cache/1'}))
            with self.assertRaises(ValueError):
                reuse.load_plan(path)

    def test_tampered_archive_rejected_before_extraction(self):
        with tempfile.TemporaryDirectory() as temp:
            (Path(temp) / 'x.tar.gz').write_bytes(b'not the pinned bytes')
            with mock.patch.object(reuse, 'reusable', return_value=True), mock.patch.object(reuse, 'git', return_value=''):
                with self.assertRaises(ValueError):
                    reuse.repack({'archive_sha256': '0' * 64}, temp, temp)

    @unittest.skipUnless(shutil.which('git'), 'git is a build-host tool')
    def test_native_rename_into_docs_is_not_reusable(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            def git(*args):
                return subprocess.check_output(['git', '-C', temp] + list(args), stderr=subprocess.STDOUT).decode().strip()
            git('init')
            git('config', 'user.email', 'ci@example.invalid')
            git('config', 'user.name', 'CI')
            (root / 'src-core').mkdir()
            (root / 'src-core/native.cpp').write_text('original native source\n')
            git('add', '.')
            git('commit', '-m', 'base')
            base = git('rev-parse', 'HEAD')
            (root / 'docs').mkdir()
            git('mv', 'src-core/native.cpp', 'docs/native.cpp')
            git('commit', '-m', 'rename')
            with mock.patch.object(reuse, 'ROOT', root):
                self.assertFalse(reuse.reusable({'source_revision': base}))

    @unittest.skipUnless(shutil.which('git'), 'git is a build-host tool')
    def test_station_only_change_reuses_native(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            def git(*args):
                return subprocess.check_output(['git', '-C', temp] + list(args), stderr=subprocess.STDOUT).decode().strip()
            git('init'); git('config', 'user.email', 'ci@example.invalid'); git('config', 'user.name', 'CI')
            (root / 'station.sh').write_text('old\n')
            git('add', '.'); git('commit', '-m', 'base')
            base = git('rev-parse', 'HEAD')
            (root / 'station.sh').write_text('new\n')
            git('commit', '-am', 'station')
            with mock.patch.object(reuse, 'ROOT', root):
                self.assertTrue(reuse.reusable({'source_revision': base}))


if __name__ == '__main__':
    unittest.main()
