import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('installed_city_names', str(ROOT / 'scripts/maps/verify_installed_city_names.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class InstalledCityNamesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name) / 'source'
        self.destination = Path(self.temporary.name) / 'installed'
        (self.source / 'maps').mkdir(parents=True)
        (self.source / 'fonts').mkdir()
        self.data = {'type': 'FeatureCollection', 'features': [
            {'properties': {'name': 'Norilsk', 'name_ru': 'Норильск'}},
            {'properties': {'name': 'Unknown', 'name_ru': None}}]}
        raw = json.dumps(self.data, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode('utf-8')
        self.report = {'target_features': 2, 'russian_names': 1, 'failures': [],
                       'output_document_sha256': hashlib.sha256(raw).hexdigest()}
        (self.source / module.FILES[0]).write_text(json.dumps(self.data), encoding='utf-8')
        (self.source / module.FILES[1]).write_text(json.dumps(self.report), encoding='utf-8')
        for name in module.FILES[2:]:
            (self.source / name).write_bytes(b'test-only-font-placeholder')
        shutil.copytree(str(self.source), str(self.destination))

    def test_identical_installed_resources(self):
        result = module.verify(self.source, self.destination)
        self.assertEqual((result['features'], result['russian_names'], result['fallback_names']), (2, 1, 1))

    def test_stale_installed_catalogue_rejected(self):
        (self.destination / module.FILES[0]).write_text('{}', encoding='utf-8')
        with self.assertRaises(ValueError): module.verify(self.source, self.destination)

    def test_missing_font_rejected(self):
        (self.destination / module.FILES[2]).unlink()
        with self.assertRaises(OSError): module.verify(self.source, self.destination)

    def test_different_installed_font_rejected(self):
        (self.destination / module.FILES[2]).write_bytes(b'wrong-font')
        with self.assertRaises(ValueError): module.verify(self.source, self.destination)

    def test_both_catalogues_changed_but_report_not_updated(self):
        modified = copy.deepcopy(self.data)
        modified['features'][0]['properties']['name_ru'] = 'Wrong'
        for root in (self.source, self.destination):
            (root / module.FILES[0]).write_text(json.dumps(modified), encoding='utf-8')
        with self.assertRaises(ValueError): module.verify(self.source, self.destination)

    def test_report_count_rejected(self):
        self.report['russian_names'] = 2
        for root in (self.source, self.destination):
            (root / module.FILES[1]).write_text(json.dumps(self.report), encoding='utf-8')
        with self.assertRaises(ValueError): module.verify(self.source, self.destination)

    def test_cmake_installs_runtime_checker(self):
        cmake = (ROOT / 'src-testing/CMakeLists.txt').read_text()
        self.assertIn('install(TARGETS satdump-city-names-runtime-test', cmake)

    def test_packagers_require_city_gate_before_archive(self):
        for profile in ('astra17', 'portable'):
            text = (ROOT / ('scripts/astra/' + profile + '/make-bundle.sh')).read_text()
            self.assertLess(text.index('scripts/maps/validate_city_runtime.sh'), text.index('tar --sort=name'))
            self.assertIn('-DBUILD_TESTING=ON', (ROOT / ('scripts/astra/' + profile + '/inside-chroot.sh')).read_text())

    def test_native_build_requires_old_and_full_city_tests(self):
        text = (ROOT / 'scripts/astra/build-native.sh').read_text()
        self.assertIn('satdump-map-label-test', text)
        self.assertIn('satdump-city-names-runtime-test', text)
        self.assertIn('scripts/maps/validate_city_runtime.sh', text)

    def test_legacy_renderer_uses_shared_resolver(self):
        text = (ROOT / 'src-core/common/map/map_drawer.cpp').read_text()
        self.assertIn('resolve_city_name(mapStruct["properties"]', text)
        self.assertNotIn('std::string name = mapStruct["properties"]["nameascii"]', text)


if __name__ == '__main__': unittest.main()
