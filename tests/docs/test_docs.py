# -*- coding: utf-8 -*-
"""Test documentation checks and offline export, not the meteorological engine."""
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/docs'))
import check
import export as exporter


class DocumentationTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix='satdump-docs-'))

    def tearDown(self):
        shutil.rmtree(str(self.root))

    def write(self, name, text):
        target = self.root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding='utf-8')
        return target

    def validate(self, text):
        self.write('README.md', text)
        return check.validate(self.root, ['README.md'])[1]

    def test_good_relative_link_and_anchor(self):
        self.write('docs/page.md', '# Страница\n\n<a id="entry"></a>\n')
        self.assertEqual([], self.validate('# Главная\n\n[Далее](docs/page.md#entry)\n'))

    def test_bad_link(self):
        self.assertTrue(self.validate('# Главная\n\n[Далее](missing.md)\n'))

    def test_bad_anchor(self):
        self.assertTrue(self.validate('# Главная\n\n[Далее](#missing)\n'))

    def test_invalid_json(self):
        self.assertTrue(self.validate('# Главная\n\n```json\n{"x":}\n```\n'))

    def test_invalid_bash(self):
        self.assertTrue(self.validate('# Главная\n\n```bash\nif then\n```\n'))

    def test_bash_is_not_executed(self):
        marker = self.root / 'must-not-exist'
        text = '# Главная\n\n```bash\ntouch "' + str(marker) + '"\n```\n'
        self.assertEqual([], self.validate(text))
        self.assertFalse(marker.exists())

    def test_unclosed_fence(self):
        self.assertTrue(self.validate('# Главная\n\n```text\nunfinished\n'))

    def test_links_inside_examples_ignored(self):
        self.assertEqual([], self.validate('# Главная\n\n```text\n[x](missing.md)\n```\n'))

    def test_repository_escape(self):
        self.assertTrue(self.validate('# Главная\n\n[x](../escape.md)\n'))

    def test_offline_rewrite_preserves_examples(self):
        text = '[Установка](INSTALL.md) [Код](../../../src/main.cpp)\n```text\n[x](unknown.md)\n```\n'
        result = exporter.rewrite(text, 'docs/ru/station/README.md', 'START_HERE.ru.md', self.root,
                                  {'docs/ru/station/INSTALL.md'}, 'a' * 40)
        self.assertIn('(docs/ru/station/INSTALL.md)', result)
        self.assertIn('/blob/' + 'a' * 40 + '/src/main.cpp)', result)
        self.assertIn('[x](unknown.md)', result)

    def test_exported_navigation_is_valid(self):
        pages = ['README.md', 'docs/ru/station/README.md', 'docs/ru/STATION_ASTRA16.md', 'docs/ru/STATION_OPERATIONS.md']
        for page in pages:
            self.write(page, '# Руководство\n')
        self.write('docs/ru/station/README.md', '# Руководство\n\n[Начать](../STATION_ASTRA16.md)\n')
        self.write('LICENSE', 'License\n')
        self.write('docs/navigation.json', json.dumps({'checked': pages}) + '\n')
        output = self.root / 'output'
        exporter.export(self.root, output, 'a' * 40)
        manifest = json.loads((output / 'docs/navigation.json').read_text())
        self.assertEqual([], check.validate(output, manifest['checked'])[1])
        self.assertIn('документации', (output / 'README.md').read_text())

    def test_export_refuses_source_overwrite(self):
        with self.assertRaises(ValueError):
            exporter.export(self.root, self.root, 'a' * 40)


if __name__ == '__main__':
    unittest.main()
