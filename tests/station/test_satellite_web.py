#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Satellite integration contracts. Real PNG/Pillow and HTTP; no claim of RF/Astra testing."""
from __future__ import print_function
import ast
import copy
import hashlib
import http.client
import json
import socket
import subprocess
import threading
import time
import unittest
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError
from PIL import Image
from test_board_infra import Fixture, ROOT
import board
import control
import configure
import satellite
import station


def json_value(raw):
    """Python 3.5 requires explicit decoding of HTTP JSON bytes."""
    return json.loads(raw.decode('utf-8') if isinstance(raw, bytes) else raw)


def request(base, path, method='GET', value=None, headers=None):
    raw = None if value is None else control.canonical(value)
    req = Request(base + path, data=raw, method=method, headers=headers or {})
    try:
        response = urlopen(req, timeout=10)
    except HTTPError as error:
        response = error
    with response:
        body = response.read()
        return response.code, dict(response.headers), body


class SatelliteRules(unittest.TestCase):
    def test_native_utc_labels(self):
        cases = [('09.09.2026 · 12:00:00–12:10:00 UTC', '2026-09-09T12:00:00Z', '2026-09-09T12:10:00Z'),
                 ('09.09.2026 · 12:00:00 UTC', '2026-09-09T12:00:00Z', '2026-09-09T12:00:00Z'),
                 ('09.09.2026 23:59:00 – 10.09.2026 00:09:00 UTC', '2026-09-09T23:59:00Z', '2026-09-10T00:09:00Z')]
        for label, start, end in cases:
            self.assertEqual((satellite.utc_seconds(start), satellite.utc_seconds(end)), satellite.observation_interval({'pass': {'acquisition_time': label}}))

    def test_explicit_offset_and_fraction(self):
        self.assertEqual(satellite.utc_seconds('2026-09-09T12:00:00.5Z'), satellite.utc_seconds('2026-09-09T15:00:00.500000+03:00'))

    def test_unknown_and_malformed_time_never_guessed(self):
        for value in ('2026-09-09T12:00:00', 'yesterday', '2026-02-30T12:00:00Z', '2026-09-09T12:00:00+25:00', 1789018188):
            self.assertIsNone(satellite.utc_seconds(value))
        for value in ({}, {'filename': 'METEOR_20260909_1200.png', 'file_mtime': 1789018188}, {'start': 'bad', 'pass': {'acquisition_time': '2026-09-09T12:00:00Z'}}):
            self.assertEqual((None, None), satellite.observation_interval(value))
        self.assertEqual((None, None), satellite.observation_interval({'start': '2026-09-09T13:00:00Z', 'end': '2026-09-09T12:00:00Z'}))

    def test_display_defaults_and_strict_validation(self):
        self.assertEqual(24, satellite.display_settings()['windowHours'])
        for value in ({'windowHours': True}, {'windowHours': 49}, {'pollSeconds': 2}, {'ambient': 'false'}, {'preloadSeconds': 40}, {'minimumDisplaySeconds': 60}, {'reloadHoursUTC': [5, 3]}, {'productPriority': ['x\n']}, {'remote_url': 'https://invalid'}):
            with self.assertRaises(ValueError): satellite.display_settings(value)
        result = satellite.display_settings({'windowHours': 6, 'ambient': False})
        self.assertEqual(6, result['windowHours'])
        result['productPriority'].clear()
        self.assertTrue(satellite.DISPLAY_DEFAULTS['productPriority'])

    def test_assets_remain_local_and_bounded(self):
        for value in ('https://host/items/a.jpg', '//host/x', 'items/../etc/passwd', 'items/' + 'a'*64 + '/000.json/../x'):
            self.assertIsNone(satellite.public_asset(value))
        self.assertEqual('/items/' + 'a'*64 + '/000-preview.jpg', satellite.public_asset('items/' + 'a'*64 + '/000-preview.jpg'))

    def test_rolling_window_expires_without_worker(self):
        now = satellite.utc_seconds('2026-09-10T12:00:00Z')
        publication = {'updated_at': now, 'frames': [{'start': '2026-09-10T11:00:00Z', 'end': '2026-09-10T11:10:00Z'}], 'display': {'windowHours': 1}}
        self.assertEqual(1, len(satellite.manifest(publication, now)['frames']))
        self.assertEqual(0, len(satellite.manifest(publication, now+3600)['frames']))
        self.assertEqual(1, satellite.manifest(publication, now-7200)['excluded']['future'])

    def test_python35_syntax(self):
        # Runtime may be newer on the test host; this checks syntax, not target ABI.
        for relative in ('services/station/satellite.py','services/station/control.py','services/station/board.py','scripts/station/configure.py'):
            if __import__('sys').version_info >= (3,8):
                ast.parse((ROOT/relative).read_text(), feature_version=(3,5))
            else:
                ast.parse((ROOT/relative).read_text())

    def test_openapi_refs_and_display_ranges(self):
        spec = json_value((ROOT/'config/station/board-openapi.json').read_text())
        self.assertEqual('3.0.3', spec['openapi'])
        def walk(value):
            if isinstance(value, dict):
                if '$ref' in value:
                    target=spec
                    for part in value['$ref'][2:].split('/'): target=target[part]
                for v in value.values(): walk(v)
            elif isinstance(value, list):
                for v in value: walk(v)
        walk(spec)
        display=spec['components']['schemas']['Display']['properties']
        self.assertEqual(satellite.DISPLAY_DEFAULTS, {k:v['default'] for k,v in display.items()})
        for k, bounds in satellite.DISPLAY_RANGES.items(): self.assertEqual(bounds,(display[k]['minimum'],display[k]['maximum']))
        ids=[op['operationId'] for route in spec['paths'].values() for op in route.values()]
        self.assertEqual(len(ids),len(set(ids)))
        self.assertIn('/api/v1/control/status',spec['paths'])

    def test_apache_preserves_astra_security_mode(self):
        config = configure.apache_config('127.0.0.1',8090,8092,8091,8093)
        self.assertIn('LoadModule mpm_prefork_module',config)
        self.assertNotIn('LoadModule mpm_event_module',config)
        self.assertNotIn('AstraMode off',config)
        self.assertIn('MaxRequestWorkers 8',config)

    def test_deployment_modes_isolation(self):
        for mode in ('apache2','nginx','builtin'):
            units=configure.unit_files('/opt/satdump-station','/etc/satdump-station','/var/lib/satdump-station','0.0.0.0',8090,8091,8092,mode,8093)
            self.assertIn('User=satdump-web',units['satdump-web.service'])
            self.assertNotIn('User=root', ''.join(units.values()))
            self.assertIn('NoNewPrivileges=true',units['satdump-web.service'])
            if mode!='builtin': self.assertIn('--host 127.0.0.1 --port 8092',units['satdump-board.service'])
        conf=configure.apache_config('0.0.0.0',8090,8092,8091,8093)
        self.assertIn('Listen 127.0.0.1:8093',conf)
        self.assertIn('Require all denied',conf)
        self.assertNotIn('Include ',conf)
        self.assertNotIn('a2enmod',conf)


class SatelliteHTTP(Fixture):
    def setUp(self):
        Fixture.setUp(self)
        self.Worker,self.Server=board.install_adapter(station,str(ROOT/'services/station/web'))
        self.worker=self.Worker(station.load_config(self.config/'station.json'))
        self.web=self.Server(('127.0.0.1',0),self.data/'public')
        self.api=control.Server(('127.0.0.1',0),self.store,'a'*64)
        self.threads=[]
        for server in (self.web,self.api):
            thread=threading.Thread(target=server.serve_forever);thread.daemon=True;thread.start();self.threads.append(thread)
        self.base='http://127.0.0.1:'+str(self.web.server_port)
        self.admin='http://127.0.0.1:'+str(self.api.server_port)
        self.auth={'Authorization':'Bearer '+'a'*64,'Content-Type':'application/json'}

    def tearDown(self):
        for server in (self.web,self.api): server.shutdown();server.server_close()
        for thread in self.threads: thread.join(5)
        self.worker.close();Fixture.tearDown(self)

    def ingest(self, name='frame', known=True, hours=0.1, size=(2400,1600), color=(20,65,110), passport=None):
        path=self.data/'inbox/images'/(name+'.png')
        Image.new('RGB',size,color).save(str(path))
        self.passport={'schema':'satdump.presentation/2','layout':'editorial','pass':{'satellite':'METEOR-M 2-4','instrument':'MSU-MR','product':'Natural color','acquisition_time':satellite.iso(time.time()-hours*3600) if known else ''},'legend':{'kind':'composite','title':'Цветовой синтез','notes':['RGB: не отдельная физическая величина'],'components':[{'component':'R','channel':'ch3','formula':'ch3'}]},'orientation':{'pass_direction':'descending','north_up_verified':False}}
        if passport is not None: self.passport.update(copy.deepcopy(passport))
        control.atomic(path.with_suffix('.json'),self.passport)
        self.worker.scan();self.worker.scan();self.worker.tick()
        return path

    def document(self, path='/api/v1/satellite/manifest'):
        code,headers,raw=request(self.base,path);self.assertEqual(200,code);return json_value(raw),headers

    def test_end_to_end_publication_and_passport_preserved(self):
        original=self.ingest();doc,_=self.document();self.assertEqual(1,len(doc['frames']));frame=doc['frames'][0]
        self.assertEqual((2400,1600),(frame['width'],frame['height']));self.assertEqual('original',frame['image']['mode']);self.assertEqual(frame['original'],frame['display'])
        for field in ('display','ambient','metadata','original'):
            code,headers,raw=request(self.base,frame[field]);self.assertEqual(200,code)
            if field=='metadata': self.assertEqual(self.passport,json_value(raw))
            if field=='original': self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(),hashlib.sha256(raw).hexdigest())
        self.assertEqual(self.passport['legend'],frame['legend'])
        self.assertEqual('descending',frame['pass'])

    def test_dimensions_selection_metadata_and_ambient_are_backend_owned(self):
        import io
        original = self.ingest(size=(320,240), color=(240,50,20), passport={
            'product': {'code':'test','description':'Backend explanation','purpose':'Тестовая интерпретация'},
            'layers': [{'name':'Прибрежная линия','description':'Наложение из паспорта'}],
            'output': {'width':999,'height':999}})
        doc,_ = self.document(); frame = doc['frames'][0]
        self.assertEqual((320,240),(frame['image']['width'],frame['image']['height']))
        self.assertEqual('Backend explanation',frame['product']['description'])
        self.assertEqual(self.passport['layers'],frame['layers'])
        self.assertEqual(1,len(frame['metadataWarnings']))
        raw=request(self.base,frame['ambient'])[2]
        with Image.open(io.BytesIO(raw)) as image:
            self.assertEqual(image.size,(frame['ambientImage']['width'],frame['ambientImage']['height']))
            self.assertGreater(image.convert('RGB').getpixel((5,5))[0],100)
        self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(),hashlib.sha256(request(self.base,frame['display'])[2]).hexdigest())

    def test_hot_preview_original_switch_without_reprocessing(self):
        self.ingest(size=(3600,2400)); before=self.worker.db.execute('SELECT count(*) FROM jobs').fetchone()[0]
        document=self.store.current(); settings=copy.deepcopy(document['settings'])
        settings['board']['display']={'imageMode':'preview'}
        self.assertEqual(202,self.store.save(settings,document['revision'])[0]); self.worker.tick()
        frame=self.document()[0]['frames'][0]
        self.assertEqual(frame['previewImage']['url'],frame['display'])
        self.assertEqual((1620,1080),(frame['width'],frame['height']))
        self.assertEqual((3600,2400),(frame['originalImage']['width'],frame['originalImage']['height']))
        self.assertEqual(before,self.worker.db.execute('SELECT count(*) FROM jobs').fetchone()[0])

    def test_portrait_four_k_and_small_files_all_survive_publication(self):
        for name,size in [('portrait',(1200,4000)),('wide',(4096,900)),('small',(64,64))]:
            self.ingest(name,size=size)
        doc,_=self.document()
        self.assertEqual({(1200,4000),(4096,900),(64,64)},set((f['width'],f['height']) for f in doc['frames']))

    def test_installed_healthcheck_includes_real_satellite_screen(self):
        import healthcheck
        self.assertTrue(healthcheck.check('127.0.0.1',self.web.server_port,self.api.server_port,'a'*64,self.api.server_port))

    def test_unknown_time_visible_without_false_timeline_timestamp(self):
        self.ingest(known=False);doc,_=self.document()
        self.assertEqual(1,len(doc['frames']));self.assertFalse(doc['frames'][0]['timeKnown']);self.assertIsNone(doc['frames'][0]['start']);self.assertEqual(1,doc['undatedCount'])
        catalog,_=self.document('/api/v1/board');self.assertEqual(1,catalog['total'])

    def test_future_and_old_frames_excluded(self):
        self.ingest('old',hours=49);self.ingest('future',hours=-2)
        doc,_=self.document();self.assertEqual([],doc['frames']);self.assertEqual(1,doc['excluded']['expired']);self.assertEqual(1,doc['excluded']['future'])

    def test_etag_head_and_query_contract(self):
        self.ingest();doc,headers=self.document();etag=headers['ETag']
        self.assertEqual(304,request(self.base,'/api/v1/satellite/manifest',headers={'If-None-Match':etag})[0])
        self.assertEqual(doc,self.document('/api/v1/satellite/manifest?probe=1')[0])
        self.assertEqual(b'',request(self.base,'/api/v1/satellite/manifest',method='HEAD')[2])
        self.assertEqual(200,request(self.base,'/api/v1/board?probe=1')[0])

    def test_all_frontend_assets_and_no_fonts_or_external_runtime(self):
        self.ingest()
        for file in (ROOT/'services/station/web/sat').rglob('*'):
            if not file.is_file() or file.name in ('package.json',) or file.suffix=='.mjs':continue
            code,_,_=request(self.base,'/sat/'+str(file.relative_to(ROOT/'services/station/web/sat')))
            self.assertEqual(200,code,str(file))
        self.assertIn('ОРБИТА'.encode(),request(self.base,'/')[2])
        self.assertFalse(any((ROOT/'services/station/web/sat').rglob('*.woff*')))
        self.assertEqual(200,request(self.base,'/api/v1/openapi.json')[0])
        for path in ('/sat/../control.token','/sat/%2e%2e/control.token','/sat/%00.js','/settings/../../etc/passwd'):
            self.assertNotEqual(200,request(self.base,path)[0])

    def test_display_settings_apply_without_new_processing(self):
        self.ingest();doc=self.store.current();settings=copy.deepcopy(doc['settings']);settings['board']['display']['windowHours']=6
        self.assertEqual(200,json_value(request(self.admin,'/api/v1/control/validate','POST',{'settings':settings},self.auth)[2])['valid']*200)
        self.assertFalse(json_value(request(self.admin,'/api/v1/control/validate','POST',{'settings':settings},self.auth)[2])['reprocessing_required'])
        code,_,raw=request(self.admin,'/api/v1/control/config','PUT',{'settings':settings},dict(self.auth,**{'If-Match':'"'+doc['revision']+'"'}))
        self.assertEqual(202,code);new=json_value(raw)
        status=json_value(request(self.admin,'/api/v1/control/status',headers=self.auth)[2]);self.assertFalse(status['applied'])
        self.worker.tick();status=json_value(request(self.admin,'/api/v1/control/status',headers=self.auth)[2]);self.assertTrue(status['applied'])
        manifest,_=self.document();self.assertEqual(6,manifest['windowHours']);self.assertEqual(new['revision'],manifest['settings_revision'])
        self.assertEqual(1,self.worker.db.execute('SELECT count(*) FROM jobs').fetchone()[0])

    def test_status_stale_worker_never_claims_applied(self):
        self.worker.heartbeat();status=json_value(request(self.admin,'/api/v1/control/status',headers=self.auth)[2]);self.assertTrue(status['applied'])
        heartbeat=control.read(self.data/'public/worker.json');heartbeat['updated_at']-=3600;control.atomic(self.data/'public/worker.json',heartbeat)
        status=json_value(request(self.admin,'/api/v1/control/status',headers=self.auth)[2]);self.assertFalse(status['applied']);self.assertFalse(status['worker_alive'])

    def test_ui_public_file_does_not_authorize_api(self):
        self.assertEqual(200,request(self.admin,'/settings/')[0])
        self.assertEqual(401,request(self.admin,'/api/v1/control/config')[0])
        self.assertEqual(403,request(self.admin,'/api/v1/control/config',headers=dict(self.auth,Origin='https://untrusted.invalid'))[0])
        self.assertEqual(404,request(self.base,'/api/v1/control/config',headers=self.auth)[0])

    def test_revisions_and_reprocessing_confirmation(self):
        doc=self.store.current();settings=copy.deepcopy(doc['settings']);settings['processing']['satdump_general']['presentation']['save_minimal']=False
        headers=dict(self.auth,**{'If-Match':'"'+doc['revision']+'"'})
        self.assertEqual(409,request(self.admin,'/api/v1/control/config','PUT',{'settings':settings},headers)[0])
        self.assertEqual(202,request(self.admin,'/api/v1/control/config','PUT',{'settings':settings,'confirm_reprocess':True},headers)[0])
        self.assertEqual(412,request(self.admin,'/api/v1/control/config','PUT',{'settings':settings,'confirm_reprocess':True},headers)[0])
        self.assertEqual(428,request(self.admin,'/api/v1/control/config','PUT',{'settings':settings},self.auth)[0])

    def test_manual_legend_strict_boundary(self):
        settings=self.store.initial();settings['processing']['satdump_general']['presentation']['legend']={'kind':'composite','title':'RGB','notes':['Условные цвета'],'components':[{'component':'R','channel':'ch3'}]}
        self.store.validate(settings)
        settings['processing']['satdump_general']['presentation']['legend']['shell']='sh'
        with self.assertRaises(ValueError):self.store.validate(settings)

    def test_legacy_existing_item_upgraded_without_decode(self):
        self.ingest();manifest_path=next((self.data/'public/items').glob('*/item.json'));old=control.read(manifest_path)
        for item in old['entries']:
            for key in ('display_width','display_height','ambient','acquisition_start_utc','acquisition_end_utc','legend','pass_direction','asset_revision'):item.pop(key,None)
        control.atomic(manifest_path,old)
        self.worker.catalog();doc,_=self.document();self.assertEqual(1,len(doc['frames']))
        self.assertEqual(1,self.worker.db.execute('SELECT count(*) FROM jobs').fetchone()[0])
