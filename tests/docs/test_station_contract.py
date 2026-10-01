# -*- coding: utf-8 -*-
"""Keep first-party instructions and API definitions aligned with shipped code."""
import ast
import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'services/station'))
import control
import product_quality


class StationDocumentationContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.api = json.loads((ROOT / 'config/station/board-openapi.json').read_text(encoding='utf-8'))

    def test_documented_source_fields_match_validator(self):
        source = self.api['components']['schemas']['Source']
        self.assertEqual(set(control.SOURCE_KEYS), set(source['properties']))
        self.assertEqual(32, source['properties']['required_instruments']['maxItems'])
        self.assertTrue(source['properties']['required_instruments']['uniqueItems'])
        product_quality.validate_required({'kind': 'product', 'required_instruments': ['mtvza']})
        with self.assertRaises(ValueError):
            product_quality.validate_required({'kind': 'image', 'required_instruments': ['mtvza']})

    def test_editable_ranges_match_validator(self):
        schema = self.api['components']['schemas']['Settings']['properties']['station']
        self.assertEqual(set(control.RANGES) | set(control.BOOLS) | {'title', 'sources'}, set(schema['properties']))
        for key, limits in control.RANGES.items():
            self.assertEqual(tuple(limits), (schema['properties'][key]['minimum'], schema['properties'][key]['maximum']))
        self.assertNotIn('processing_revision', schema['properties'])

    def test_default_pipeline_examples_exist(self):
        pipelines = set()
        for path in (ROOT / 'pipelines').glob('*.json'):
            pipelines.update(control.jsonc(path))
        config = json.loads((ROOT / 'config/station/station.json').read_text())
        for source in config['sources']:
            if source['kind'] == 'pipeline':
                self.assertIn(source['pipeline'], pipelines, source['pipeline'])

    def test_public_routes_are_not_documented_as_token_protected(self):
        for route in ('/api/v1/board', '/health.json', '/health/ready'):
            self.assertEqual([], self.api['paths'][route]['get']['security'])
        self.assertTrue(self.api['security'])
        self.assertNotIn('security', self.api['paths']['/api/v1/control/config']['get'])

    def test_source_help_has_unique_ids_and_local_assets(self):
        html = (ROOT / 'services/station/web/index.html').read_text(encoding='utf-8')
        ids = re.findall(r'\bid="([^"]+)"', html)
        self.assertEqual(len(ids), len(set(ids)))
        for link in re.findall(r'(?:src|href)="([^"]+)"', html):
            if link.startswith('#'):
                self.assertIn(link[1:], ids)
            else:
                self.assertNotIn('://', link)
                self.assertTrue((ROOT / 'services/station/web' / link).is_file(), link)
        self.assertIn('/status.js', (ROOT / 'services/station/station.py').read_text())

    def test_main_documentation_distinguishes_release_from_source(self):
        readme = (ROOT / 'README.md').read_text(encoding='utf-8')
        self.assertIn('v1.2.2-astra16-station.22', readme)
        self.assertIn('но не в пакете 22', readme)
        self.assertIn('MTVZA_PROCESSING.md', readme)
        self.assertIn('required_instruments', (ROOT / 'docs/ru/station/CONFIGURATION.md').read_text())


if __name__ == '__main__':
    unittest.main()
