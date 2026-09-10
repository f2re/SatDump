import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('sync_ru', str(ROOT / 'scripts/maps/sync_city_names_ru.py'))
sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync)


def feature(name='Norilsk', ru='Норильск', lon=88.2, lat=69.3, nation='RUS', identifier=1157000000):
    return {'type': 'Feature', 'properties': {'name': name, 'name_ru': ru, 'adm0_a3': nation, 'ne_id': identifier},
            'geometry': {'type': 'Point', 'coordinates': [lon, lat]}}


def document(*items):
    return {'type': 'FeatureCollection', 'features': list(items)}


class SynchronizationTests(unittest.TestCase):
    def test_verbatim_and_preservation(self):
        target = document(feature(ru='Неправильно'))
        source = document(feature(ru='Норильск — Ё'))
        original = copy.deepcopy(target)
        output, report = sync.synchronize(target, source)
        self.assertEqual(output['features'][0]['properties']['name_ru'], 'Норильск — Ё')
        self.assertEqual(target, original)
        self.assertEqual(sync.without_ru(output), sync.without_ru(target))
        self.assertEqual(report['failures'], [])

    def test_duplicate_rounded_ids_use_coordinates(self):
        a, b = feature(lon=88), feature(name='Another', lon=90, ru='Другой')
        output, report = sync.synchronize(document(a, b), document(b, a))
        self.assertEqual(report['matched_features'], 2)
        self.assertEqual(report['duplicate_target_ne_id_values'], 1)
        self.assertEqual(output['features'][0]['properties']['name_ru'], 'Норильск')

    def test_case_insensitive_source_fields(self):
        source = feature()
        source['properties'] = {k.upper(): v for k, v in source['properties'].items()}
        result, report = sync.synchronize(document(feature()), document(source))
        self.assertEqual(result['features'][0]['properties']['name_ru'], 'Норильск')
        self.assertEqual(report['matched_features'], 1)

    def test_homonyms_do_not_cross_country(self):
        target = feature(name='London', nation='GBR', lon=0, lat=50)
        source = feature(name='London', nation='CAN', lon=0, lat=50)
        _, report = sync.synchronize(document(target), document(source))
        self.assertEqual(len(report['failures']), 1)

    def test_ambiguous_coordinates_fail(self):
        item = feature()
        _, report = sync.synchronize(document(item), document(item, item))
        self.assertEqual(report['failures'][0]['reason'], 'ambiguous_or_reused')

    def test_source_cannot_be_reused(self):
        item = feature()
        _, report = sync.synchronize(document(item, item), document(item))
        self.assertEqual(len(report['failures']), 1)

    def test_coordinate_rounding(self):
        _, report = sync.synchronize(document(feature(lon=88.20000001)), document(feature(lon=88.20000002)))
        self.assertEqual(report['match_methods'], {'coordinates_6dp': 1})

    def test_unique_id_requires_distance(self):
        _, report = sync.synchronize(document(feature(lon=0)), document(feature(lon=80)))
        self.assertEqual(len(report['failures']), 1)

    def test_unique_id_close_shift(self):
        _, report = sync.synchronize(document(feature(lon=88.2001)), document(feature()))
        self.assertEqual(report['match_methods'], {'unique_id_and_distance': 1})

    def test_name_country_close_shift(self):
        a, b = feature(lon=88.2001, identifier=0), feature(identifier=0)
        _, report = sync.synchronize(document(a), document(b))
        self.assertEqual(report['match_methods'], {'country_name_and_distance': 1})

    def test_no_fuzzy_name_matching(self):
        a, b = feature(name='Unrelated', lon=88.2001, identifier=0), feature(identifier=0)
        _, report = sync.synchronize(document(a), document(b))
        self.assertEqual(len(report['failures']), 1)

    def test_null_and_empty_are_not_invented(self):
        source = document(feature(ru=None), feature(name='B', lon=90, ru=''))
        output, report = sync.synchronize(source, source)
        self.assertIsNone(output['features'][0]['properties']['name_ru'])
        self.assertEqual(output['features'][1]['properties']['name_ru'], '')
        self.assertEqual(len(report['missing_russian_names']), 2)

    def test_missing_source_field_rejected(self):
        item = feature()
        del item['properties']['name_ru']
        with self.assertRaises(sync.SyncError):
            sync.synchronize(document(feature()), document(item))

    def test_numeric_translation_rejected(self):
        with self.assertRaises(sync.SyncError):
            sync.synchronize(document(feature()), document(feature(ru=123)))

    def test_conflicting_case_fields_rejected(self):
        item = feature()
        item['properties']['NAME_RU'] = 'Другой'
        with self.assertRaises(sync.SyncError):
            sync.synchronize(document(feature()), document(item))

    def test_invalid_geometry_rejected(self):
        with self.assertRaises(sync.SyncError):
            sync.synchronize(document(feature(lat=95)), document(feature()))

    def test_idempotent_report_and_data(self):
        source = document(feature())
        output, report = sync.synchronize(document(feature(ru='Old')), source)
        again, second_report = sync.synchronize(output, source)
        self.assertEqual(output, again)
        self.assertEqual(report, second_report)

    def test_cli_atomic_failure_and_check(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target, source, report = root/'target.json', root/'source.json', root/'report.json'
            target.write_text(json.dumps(document(feature(ru='Old'))), encoding='utf-8')
            source.write_text(json.dumps(document(feature(lon=0))), encoding='utf-8')
            before = target.read_bytes()
            args = ['--source', str(source), '--target', str(target), '--report', str(report)]
            self.assertEqual(sync.main(args), 1)
            self.assertEqual(target.read_bytes(), before)
            source.write_text(json.dumps(document(feature())), encoding='utf-8')
            self.assertEqual(sync.main(args), 0)
            before, report_before = target.read_bytes(), report.read_bytes()
            self.assertEqual(sync.main(args+['--check']), 0)
            self.assertEqual(target.read_bytes(), before)
            self.assertEqual(report.read_bytes(), report_before)


if __name__ == '__main__':
    unittest.main()
