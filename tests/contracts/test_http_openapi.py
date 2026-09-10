#!/usr/bin/env python3
"""Validate real HTTP responses against the published OpenAPI schemas.

Development test only (jsonschema). Production remains Python 3.5 stdlib + Pillow.
The OpenAPI document itself is independently checked by openapi-spec-validator in CI.
"""
import copy
import json
import sys
import unittest
from pathlib import Path
import jsonschema
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tests/station'))
import test_satellite_web as support
request = support.request


def json_schema(value):
    """Translate OpenAPI 3.0 nullable to JSON Schema without weakening required/enum/ranges."""
    if isinstance(value,list):
        return [json_schema(item) for item in value]
    if not isinstance(value,dict):
        return value
    result={key:json_schema(item) for key,item in value.items() if key!='nullable'}
    if value.get('nullable'):
        return {'anyOf':[result,{'type':'null'}]}
    return result


class Responses(unittest.TestCase):
    def setUp(self):
        self.fixture=support.SatelliteHTTP(); self.fixture.setUp()
        self.fixture.ingest('wide',size=(4096,1200))
        self.fixture.ingest('undated',known=False,size=(80,80))
        self.spec=json.loads((ROOT/'config/station/board-openapi.json').read_text())

    def tearDown(self):
        self.fixture.tearDown()

    def assert_response(self,path,base=None,method='get',value=None,headers=None):
        f=self.fixture
        code,headers,raw=request(base or f.base,path,method.upper(),value,headers)
        self.assertIn(str(code),self.spec['paths'][path][method]['responses'])
        schema=self.spec['paths'][path][method]['responses'][str(code)].get('content',{}).get('application/json',{}).get('schema')
        self.assertIsNotNone(schema)
        root=json_schema(dict(schema,components=self.spec['components']))
        jsonschema.Draft4Validator(root).validate(json.loads(raw))
        return code,json.loads(raw),headers

    def test_real_public_responses_with_large_and_undated_images(self):
        for path in ['/api/v1/board','/api/v1/board/status','/api/v1/satellite/manifest','/api/v1/satellite/config','/api/v1/openapi.json']:
            with self.subTest(path=path):
                self.assertEqual(200,self.assert_response(path)[0])

    def test_authenticated_config_capabilities_applied_and_validation(self):
        f=self.fixture
        for path in ['/api/v1/control/config','/api/v1/control/capabilities','/api/v1/control/status','/api/v1/control/health']:
            with self.subTest(path=path):
                self.assertEqual(200,self.assert_response(path,f.admin,headers=f.auth)[0])
        current=f.store.current(); settings=copy.deepcopy(current['settings']);settings['board']['display']={'imageMode':'preview'}
        code,value,headers=self.assert_response('/api/v1/control/validate',f.admin,'post',{'settings':settings},f.auth)
        self.assertEqual(200,code);self.assertFalse(value['reprocessing_required'])
        headers=dict(f.auth,**{'If-Match':'"'+current['revision']+'"'})
        code,value,_=self.assert_response('/api/v1/control/config',f.admin,'put',{'settings':settings},headers)
        self.assertEqual(202,code)
        f.worker.tick()
        self.assert_response('/api/v1/satellite/manifest')
        self.assert_response('/api/v1/control/status',f.admin,headers=f.auth)
        self.assertEqual(412,self.assert_response('/api/v1/control/config',f.admin,'put',{'settings':settings},headers)[0])

    def test_authentication_and_preconditions_are_documented(self):
        f=self.fixture
        self.assertEqual(401,self.assert_response('/api/v1/control/config',f.admin)[0])
        self.assertEqual(428,self.assert_response('/api/v1/control/config',f.admin,'put',{'settings':f.store.current()['settings']},f.auth)[0])

if __name__=='__main__':
    unittest.main()
