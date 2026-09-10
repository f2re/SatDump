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

    def test_duplicate_ids_use_coordinates(self):
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


    def test_historical_coordinates_with_name_and_country(self):
        target = feature(lon=90)
        target['properties'].update(longitude=88.2, latitude=69.3)
        output, report = sync.synchronize(document(target), document(feature()))
        self.assertEqual(report['match_methods'], {'historical_coordinates_country_name': 1})
        self.assertEqual(output['features'][0]['geometry'], target['geometry'])

    def test_historical_coordinates_do_not_match_unrelated_name(self):
        target = feature(name='Unrelated', lon=90, identifier=0)
        target['properties'].update(longitude=88.2, latitude=69.3)
        _, report = sync.synchronize(document(target), document(feature(identifier=0)))
        self.assertEqual(len(report['failures']), 1)

    def test_invalid_historical_coordinates_are_ignored(self):
        target = feature(lon=90)
        target['properties'].update(longitude=88.2, latitude=95)
        _, report = sync.synchronize(document(target), document(feature()))
        self.assertEqual(len(report['failures']), 1)

    def reviewed(self, target, source, missing=False):
        entry = {'target_ne_id': target['properties']['ne_id'],
                 'target_feature_sha256': sync.feature_digest(target),
                 'source_index': None if missing else 0, 'reason': 'Reviewed test fixture'}
        if not missing:
            entry['source_feature_sha256'] = sync.digest(source)
        return {'schema_version': 1, 'source_document_sha256': sync.digest(document(source)), 'entries': [entry]}

    def test_reviewed_pair_verbatim_and_idempotent(self):
        target, source = feature(lon=90), feature(ru='Имя из источника')
        manifest = self.reviewed(target, source)
        output, report = sync.synchronize(document(target), document(source), manifest)
        self.assertEqual(report['match_methods'], {'reviewed_version_pair': 1})
        self.assertEqual(output['features'][0]['properties']['name_ru'], 'Имя из источника')
        again, next_report = sync.synchronize(output, document(source), manifest)
        self.assertEqual(output, again)
        self.assertEqual(report, next_report)

    def test_reviewed_target_change_is_rejected(self):
        target, source = feature(lon=90), feature()
        manifest = self.reviewed(target, source)
        target['properties']['name'] = 'Changed'
        with self.assertRaises(sync.SyncError):
            sync.synchronize(document(target), document(source), manifest)

    def test_reviewed_source_snapshot_change_is_rejected(self):
        target, source = feature(lon=90), feature()
        manifest = self.reviewed(target, source)
        source['properties']['name_ru'] = 'Changed'
        with self.assertRaises(sync.SyncError):
            sync.synchronize(document(target), document(source), manifest)

    def test_reviewed_source_feature_hash_is_checked(self):
        target, source = feature(lon=90), feature()
        manifest = self.reviewed(target, source)
        manifest['entries'][0]['source_feature_sha256'] = 'incorrect'
        with self.assertRaises(sync.SyncError):
            sync.synchronize(document(target), document(source), manifest)

    def test_absent_reviewed_place_remains_null(self):
        target, source = feature(name='Absent', lon=90), feature()
        output, report = sync.synchronize(document(target), document(source), self.reviewed(target, source, True))
        self.assertIsNone(output['features'][0]['properties']['name_ru'])
        self.assertEqual(report['failures'], [])
        self.assertEqual(len(report['missing_in_source']), 1)

    def test_absent_reviewed_place_must_not_have_match(self):
        target, source = feature(), feature()
        with self.assertRaises(sync.SyncError):
            sync.synchronize(document(target), document(source), self.reviewed(target, source, True))

    def test_conflicting_reviewed_pairs_are_rejected(self):
        target, source = feature(lon=90), feature()
        manifest = self.reviewed(target, source)
        manifest['entries'].append(manifest['entries'][0])
        with self.assertRaises(sync.SyncError):
            sync.synchronize(document(target), document(source), manifest)

    def test_malformed_geometry_rejected(self):
        for geometry in ([], 7, {'type': 'Point', 'coordinates': 4}):
            target = feature()
            target['geometry'] = geometry
            with self.assertRaises(sync.SyncError):
                sync.synchronize(document(target), document(feature()))

    def test_runtime_has_no_transliteration_or_alias_dictionary(self):
        code = (ROOT/'src-core/common/map/city_labels.cpp').read_text(encoding='utf-8')
        self.assertIn('resolve_city_name(', code)
        self.assertNotIn('transliterate_to_russian', code)
        self.assertNotIn('russian_names()', code)

    def test_cli_source_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target, source = root/'target.json', root/'source.json'
            target.write_text(json.dumps(document(feature())), encoding='utf-8')
            source.write_text(json.dumps(document(feature())), encoding='utf-8')
            before = source.read_bytes()
            args = ['--source', str(source), '--target', str(target), '--report', str(source)]
            self.assertEqual(sync.main(args), 1)
            self.assertEqual(source.read_bytes(), before)

if __name__ == '__main__':
    unittest.main()
