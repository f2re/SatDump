#!/usr/bin/env python3
"""The fast installer fix path must never silently reuse changed native code."""
import io
import json
import os
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/station'))
import maintenance
import pack


class MaintenanceTests(unittest.TestCase):
    def test_only_reviewed_installer_changes_allowed(self):
        maintenance.permitted_changes(['scripts/station/install.sh', 'tests/station/test_install_permissions.py'])
        for path in ('src-core/core.cpp', 'scripts/station/bundle-python.sh', 'scripts/station/build.sh',
                     'services/station/board.py', 'CMakeLists.txt', 'scripts/station/pack.py'):
            with self.assertRaises(ValueError):
                maintenance.permitted_changes([path])

    def test_component_identity_includes_contents_modes_and_links(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            item = root / 'binary'
            item.write_bytes(b'one')
            initial = maintenance.component_digest(root)
            item.write_bytes(b'two')
            self.assertNotEqual(initial, maintenance.component_digest(root))
            item.write_bytes(b'one')
            self.assertEqual(initial, maintenance.component_digest(root))
            item.chmod(0o755)
            executable = maintenance.component_digest(root)
            self.assertNotEqual(initial, executable)
            (root / 'launcher').symlink_to('binary')
            self.assertNotEqual(executable, maintenance.component_digest(root))

    def test_archive_rejects_escape_and_linked_parent(self):
        for member_name, link in (('../escape', None), ('/escape', None), ('satdump-x/link', '/etc'),
                                  ('satdump-x/link', '../elsewhere')):
            with tempfile.TemporaryDirectory() as name:
                root = Path(name)
                archive = root / 'bad.tar.gz'
                with tarfile.open(str(archive), 'w:gz') as tar:
                    member = tarfile.TarInfo(member_name)
                    if link:
                        member.type = tarfile.SYMTYPE
                        member.linkname = link
                    tar.addfile(member)
                with self.assertRaises(ValueError):
                    maintenance.extract(archive, root / 'extracted')
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            archive = root / 'bad.tar.gz'
            with tarfile.open(str(archive), 'w:gz') as tar:
                member = tarfile.TarInfo('satdump-x/link')
                member.type = tarfile.SYMTYPE
                member.linkname = 'directory'
                tar.addfile(member)
                tar.addfile(tarfile.TarInfo('satdump-x/link/payload'))
            with self.assertRaises(ValueError):
                maintenance.extract(archive, root / 'out')

    def test_new_archive_checksums_and_provenance_roundtrip(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            bundle = root / 'satdump-fixture'
            bundle.mkdir()
            (bundle / 'data').write_bytes(b'real fixture bytes')
            (bundle / 'PACKAGE-MANIFEST.json').write_text(json.dumps({'git_commit': '1' * 40, 'maintenance': {'source_git_commit': '2' * 40}}))
            archive = maintenance.write_archive(bundle, root, 1000)
            self.assertIn(pack.sha256(archive), Path(str(archive) + '.sha256').read_text())
            unpacked = maintenance.extract(archive, root / 'unpacked')
            pack.verify(unpacked)
            self.assertEqual(b'real fixture bytes', (unpacked / 'data').read_bytes())
            report = json.loads((unpacked / 'PACKAGE-MANIFEST.json').read_text())
            self.assertEqual('2' * 40, report['maintenance']['source_git_commit'])


if __name__ == '__main__':
    unittest.main()
