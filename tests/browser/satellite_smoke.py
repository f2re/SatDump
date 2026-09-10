#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Real Chromium -> unprivileged Apache/nginx -> BOARD/Control -> PNG publication.

Run: python3 tests/browser/satellite_smoke.py --server apache2
Development-only: requires Pillow, Playwright and a Chromium executable. No RF
or native Astra acceptance is implied. All services/files are temporary.
"""
import argparse
import json
import os
import pwd
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tests/station'))
from test_satellite_web import SatelliteHTTP, request
import control
import configure


def port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0)); return sock.getsockname()[1]


def wait_js(page, expression, arg=None, timeout=30000):
    """Poll through CDP evaluation, not in-page eval/string timers forbidden by CSP."""
    predicate = expression if '=>' in expression else '() => (' + expression + ')'
    deadline = time.monotonic() + timeout / 1000.0
    while time.monotonic() < deadline:
        if page.evaluate(predicate, arg):
            return
        page.wait_for_timeout(100)
    raise AssertionError('Browser condition timed out: ' + expression)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--server',choices=('apache2','nginx'),default='apache2');parser.add_argument('--chromium',help='Optional explicit executable; default is Playwright managed Chromium');parser.add_argument('--http-only',action='store_true');parser.add_argument('--artifacts',default='/tmp/satdump-browser-results');args=parser.parse_args()
    if not Path('/usr/sbin/'+args.server).is_file():raise SystemExit('OS web server is required for this test')
    out=Path(args.artifacts);out.mkdir(parents=True,exist_ok=True)
    fixture=SatelliteHTTP();fixture.setUp();process=None;log=None;checks=[]
    try:
        fixture.ingest('first',hours=2,size=(900,2400),color=(20,140,170))
        fixture.ingest('second',hours=.25,size=(4096,1200),color=(245,90,20),passport={
            'product':{'code':'native-ir','description':'Backend brightness temperature, not retrieved cloud-top temperature.', 'purpose':'Контроль интерпретации: тестовый паспорт, не метеонаблюдение.'},
            'legend':{'kind':'continuous','title':'Яркостная температура','unit':'K','color_stops':[{'position':0,'color':[20,30,200]},{'position':1,'color':[250,150,20]}], 'ticks':[{'position':0,'label':'180 K'},{'position':1,'label':'320 K'}], 'notes':['Тестовый паспорт. Температура яркостная, не восстановленная.']}})
        fixture.ingest('undated',known=False,size=(80,80),color=(130,60,190))
        current=fixture.store.current(); settings=current['settings']; settings['board']['display']={'displaySeconds':25,'complexDisplaySeconds':25,'latestSeconds':25,'preloadSeconds':5,'transitionMilliseconds':200,'pollSeconds':30}
        assert fixture.store.save(settings,current['revision'])[0]==202
        fixture.worker.tick()
        public_port,admin_port=port(),port()
        policy=control.read(fixture.config/'control-policy.json');policy['admin_origins']=['http://127.0.0.1:'+str(admin_port)];control.atomic(fixture.config/'control-policy.json',policy);fixture.store.policy=policy
        fixture.root.chmod(0o755)
        runtime=fixture.root/'proxy-runtime';runtime.mkdir(mode=0o755)
        confpath=fixture.root/(args.server+'.conf')
        if args.server=='apache2':
            config=configure.apache_config('127.0.0.1',public_port,fixture.web.server_port,fixture.api.server_port,admin_port)
            # Capture Apache diagnostics even in a minimal OS container without syslog.
            config=config.replace('ErrorLog syslog:daemon:satdump-web','ErrorLog '+str(runtime/'error.log'))
            command=['/usr/sbin/apache2','-f',str(confpath),'-DFOREGROUND']
            probe=['/usr/sbin/apache2','-t','-f',str(confpath)]
        else:
            config=configure.nginx_config(str(fixture.config),'127.0.0.1',public_port,fixture.web.server_port,fixture.api.server_port,admin_port)
            command=['/usr/sbin/nginx','-c',str(confpath),'-g','daemon off;']
            probe=['/usr/sbin/nginx','-t','-c',str(confpath)]
        confpath.write_text(config.replace('/run/satdump-web',str(runtime)));confpath.chmod(0o644)
        preexec=None
        if os.geteuid()==0:
            account=pwd.getpwnam('nobody');os.chown(runtime,account.pw_uid,account.pw_gid)
            def demote():
                os.setgroups([]);os.setgid(account.pw_gid);os.setuid(account.pw_uid)
            preexec=demote
        log=open(str(out/(args.server+'.log')),'w')
        subprocess.run(probe,check=True,stdout=log,stderr=log,preexec_fn=preexec)
        process=subprocess.Popen(command,stdout=log,stderr=log,preexec_fn=preexec)
        public='http://127.0.0.1:'+str(public_port);admin='http://127.0.0.1:'+str(admin_port)
        for attempt in range(100):
            if process.poll() is not None:raise RuntimeError('Proxy exited; see '+str(out/(args.server+'.log')))
            try:
                if request(public,'/health/ready')[0]==200:break
            except OSError:time.sleep(.1)
        else:raise RuntimeError('Proxy not ready')
        checks.append('unprivileged_proxy_start')
        for path in ('/settings/','/settings/admin.js','/api/v1/control/config','/api/v1/control/status'):
            assert request(public,path,headers=fixture.auth)[0] in (403,404),path
        assert request(admin,'/api/v1/control/config')[0]==401
        assert request(admin,'/api/v1/control/config',headers=dict(fixture.auth,Origin='https://invalid.example'))[0]==403
        checks.append('public_control_denied_and_origin_checked')
        # This mode is also used inside the target OS image without a GUI stack.
        import healthcheck
        assert healthcheck.check('127.0.0.1',public_port,fixture.api.server_port,'a'*64,admin_port)
        manifest=json.loads(request(public,'/api/v1/satellite/manifest')[2])
        assert len(manifest['frames'])==3
        for frame in manifest['frames']:
            for path in (frame['display'],frame['ambient'],frame['metadata']):
                assert request(public,path)[0]==200,path
        checks.extend(['healthcheck_real_screen_openapi_manifest','all_published_image_and_metadata_urls'])
        if args.http_only:
            result={'server':args.server,'checks':checks,'passed':len(checks),'browser_tested':False,'rf_decoding_tested':False}
            (out/(args.server+'-http-result.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
            print(json.dumps(result,ensure_ascii=False));return
        from playwright.sync_api import sync_playwright
        errors=[];external=[];missing=[]
        with sync_playwright() as pw:
            browser=pw.chromium.launch(executable_path=args.chromium,headless=True,args=['--no-sandbox'])
            context=browser.new_context(viewport={'width':1920,'height':1080})
            def watch(page):
                page.on('pageerror',lambda e:errors.append(str(e)))
                page.on('request',lambda req:external.append(req.url) if not any(req.url.startswith(origin+'/') for origin in (public,admin)) else None)
                page.on('response',lambda response:missing.append((response.status,response.url)) if response.status>=400 else None)
            page=context.new_page();watch(page);page.goto(public+'/',wait_until='networkidle')
            wait_js(page, "document.querySelector('#image-stage .is-active')?.naturalWidth > 0")
            checks.append('real_png_loaded')
            assert page.locator('#frame-b').evaluate('(img)=>img.naturalHeight')==2400
            assert page.locator('#satellite-art').evaluate('(img)=>!img.hidden && img.naturalWidth>0')
            assert page.locator('.timeline-event').count()==2  # undated photo is not a fabricated UTC event
            assert page.locator('#legend-entries').inner_text().find('ch3')>=0
            page.screenshot(path=str(out/(args.server+'-portrait.png')))
            initial_ambient=page.locator('.ambient.is-active').get_attribute('src')
            # Real production interval and image decode; never weaken CSP for a fake clock.
            wait_js(page, "[...document.querySelectorAll('.sat-image')].some(i=>!i.classList.contains('is-active') && i.naturalWidth===4096)")
            wait_js(page, "document.querySelector('.sat-image.is-active').naturalWidth===4096")
            page.wait_for_timeout(350)
            assert page.locator('.ambient.is-active').get_attribute('src')!=initial_ambient
            assert page.locator('.ambient.is-active').evaluate('(img)=>Number(getComputedStyle(img).opacity)')>.7
            assert page.locator('#description').inner_text()=='Backend brightness temperature, not retrieved cloud-top temperature.'
            assert page.locator('.legend-gradient').count()==1
            assert '180 K' in page.locator('#legend-stops').inner_text()
            assert 'тестовый паспорт' in page.locator('#purpose').inner_text()
            assert '4096 × 1200' in page.locator('#image-dimensions').inner_text()
            checks.extend(['automatic_transition_and_synchronized_visible_ambient','native_color_stops_units_and_purpose','four_k_original_not_filtered'])
            page.screenshot(path=str(out/(args.server+'-wide.png')))
            page.set_viewport_size({'width':2560,'height':1440});page.wait_for_timeout(100)
            assert page.locator('#chronoscope').evaluate('(el)=>el.getBoundingClientRect().width')>2400
            page.set_viewport_size({'width':1920,'height':1080})
            page.locator('#pause-button').click();assert page.locator('#pause-button').get_attribute('aria-pressed')=='true'
            first=page.locator('#image-stage .is-active').get_attribute('src')
            page.locator('#next').click();wait_js(page, '(old)=>document.querySelector("#image-stage .is-active")?.src !== old',arg=first)
            page.wait_for_timeout(400)
            second=page.locator('#image-stage .is-active').get_attribute('src');assert first!=second
            assert page.locator('#image-stage .is-active').evaluate('(i)=>i.naturalWidth')==80
            assert 'Нет времени' in page.locator('#acquisition-time').inner_text()
            checks.append('small_undated_image_visible_without_fake_timeline')
            page.locator('#previous').click();wait_js(page, '(old)=>document.querySelector("#image-stage .is-active")?.src !== old',arg=second)
            page.wait_for_timeout(400)
            assert page.locator('#passport-link').get_attribute('href').startswith(public+'/items/')
            checks.append('pause_navigation_and_passport')
            page.screenshot(path=str(out/(args.server+'-screen.png')))
            settings=context.new_page();watch(settings);settings.goto(admin+'/settings/',wait_until='networkidle')
            settings.locator('#token').fill('a'*64);settings.locator('#login button').click();settings.locator('#settings').wait_for(state='visible')
            wait_js(settings, "document.querySelector('#message').textContent.includes('Настройки загружены')")
            assert settings.evaluate('localStorage.length')==0
            assert settings.evaluate('sessionStorage.length')==0
            checks.append('authenticated_ui_no_persistent_token')
            window_input=settings.locator('#display-fields label').filter(has_text='Глубина истории').locator('input')
            window_input.fill('1');window_input.press('Tab')
            settings.locator('#validate').click();wait_js(settings, "document.querySelector('#message').textContent.includes('Повторная обработка не требуется')")
            settings.locator('#save').click();wait_js(settings, "document.querySelector('#message').textContent.includes('Ревизия сохранена')")
            fixture.worker.tick()
            wait_js(settings, "document.querySelector('#applied').textContent === 'Применено обработчиком'",timeout=10000)
            assert fixture.store.current()['settings']['board']['display']['windowHours']==1
            checks.append('form_validate_save_and_applied_revision')
            settings.screenshot(path=str(out/(args.server+'-settings.png')))
            # Avoid relying on visibility events in headless tabs; explicitly refresh.
            page.bring_to_front();page.locator('#refresh').click();page.wait_for_timeout(400)
            assert request(public,'/api/v1/satellite/config')[0]==200
            assert len(json.loads(request(public,'/api/v1/satellite/manifest')[2])['frames'])==2
            assert fixture.worker.db.execute('SELECT count(*) FROM jobs').fetchone()[0]==3
            checks.append('hot_settings_without_new_jobs')
            settings.bring_to_front()
            settings.locator('#display-fields label').filter(has_text='Файл для показа').locator('select').select_option('preview')
            settings.locator('#save').click();wait_js(settings, "document.querySelector('#message').textContent.includes('Ревизия сохранена')")
            fixture.worker.tick()
            page.bring_to_front();page.locator('#refresh').click()
            wait_js(page, "document.querySelector('.sat-image.is-active').src.endsWith('-preview.jpg')")
            assert '1920 × 563' in page.locator('#image-dimensions').inner_text()
            assert fixture.worker.db.execute('SELECT count(*) FROM jobs').fetchone()[0]==3
            checks.append('server_preview_switch_while_paused_no_new_jobs')
            settings.bring_to_front();settings.locator('#advanced').click()
            editor=settings.locator('#json-settings');draft=json.loads(editor.input_value());draft['processing']['satdump_general']['presentation']['save_minimal']=False
            editor.fill(json.dumps(draft));settings.locator('#use-json').click();wait_js(settings, "document.querySelector('#message').textContent.includes('JSON проверен')")
            settings.locator('#save').click();settings.locator('#reprocess-panel').wait_for(state='visible');assert fixture.store.current()['settings']['processing']['satdump_general']['presentation']['save_minimal'] is True
            settings.locator('#confirm-reprocess').check();settings.locator('#save').click();wait_js(settings, "document.querySelector('#message').textContent.includes('Ревизия сохранена')")
            checks.append('explicit_reprocessing_confirmation')
            # Both desktop and narrow admin layout must remain usable without overflow.
            settings.set_viewport_size({'width':390,'height':844});settings.screenshot(path=str(out/(args.server+'-settings-mobile.png')),full_page=True)
            assert settings.evaluate('document.documentElement.scrollWidth <= innerWidth+2')
            checks.append('mobile_settings_layout')
            # Do not let beforeunload dialogs stall context cleanup.
            context.close();browser.close()
        assert not errors,errors
        assert not external,external
        assert not missing,missing
        checks.extend(['no_javascript_errors','no_external_requests','no_missing_assets'])
        result={'server':args.server,'checks':checks,'passed':len(checks),'native_astra_tested':False,'rf_decoding_tested':False,'browser':'Chromium'}
        (out/(args.server+'-result.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print(json.dumps(result,ensure_ascii=False))
    finally:
        if process is not None:
            process.terminate()
            try:process.wait(timeout=10)
            except subprocess.TimeoutExpired:process.kill();process.wait()
        if log:
            log.close()
            print((out/(args.server+'.log')).read_text(),file=sys.stderr)
        if 'runtime' in locals() and (runtime/'error.log').is_file():
            print((runtime/'error.log').read_text(),file=sys.stderr)
        fixture.tearDown()


if __name__=='__main__':main()
