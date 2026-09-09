#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BOARD adapter; no new frontend. Uses the existing SatDump queue and renderer."""
from __future__ import print_function
import argparse
import copy
import json
import logging
import os
import re
import shutil
import sys
import time
from pathlib import Path
from urllib.parse import unquote, urlsplit
import control

LOG = logging.getLogger('satdump-board')


def layout(metadata, native):
    if not native and metadata.get('schema') not in ('satdump.presentation/1', 'satdump.presentation/2'):
        return 'external'
    value = metadata.get('layout', '')
    return 'minimal' if value == 'minimal' else 'editorial'


def visible(entry, settings):
    return (entry.get('source') not in settings.get('hidden_sources', []) and
            entry.get('instrument') not in settings.get('hidden_instruments', []) and
            entry.get('title') not in settings.get('hidden_products', []) and
            entry.get('layout', 'editorial') in settings.get('layouts', ['editorial', 'minimal', 'external']))


def install_adapter(station, ui_root=None):
    """Bind an extension, leaving upstream CLI/UI/algorithms untouched."""
    BaseWorker = station.Worker
    BaseServer = station.GalleryServer

    class BoardWorker(BaseWorker):
        def __init__(self, cfg):
            self.base = copy.deepcopy(cfg)
            self.managed = None
            self.revision = ''
            self.revision_error = False
            self.board_settings = {'layouts': ['editorial', 'minimal', 'external'], 'max_items': cfg['max_items']}
            BaseWorker.__init__(self, cfg)
            policy = Path(cfg['_config_dir']) / 'control-policy.json'
            if policy.is_file():
                self.managed = control.Store(Path(cfg['_config_dir']) / 'station.json')
                self.reload()

        def reload(self):
            if self.managed is None:
                return
            try:
                doc = self.managed.current()
                if doc['revision'] == self.revision:
                    self.revision_error = False
                    return
                settings = doc['settings']
                new_cfg = copy.deepcopy(self.base)
                new_cfg.update(copy.deepcopy(settings['station']))
                new_patch = copy.deepcopy(settings['processing'])
                new_patch.setdefault('satdump_general', {})['tle_update_interval'] = {'value': 'Never'}
                new_patch['satdump_general']['log_to_file'] = {'value': False}
                applied = self.data / 'state/applied-control.json'
                previous_key = control.read(applied).get('processing_key') if applied.is_file() else control.Store.processing_key(self.managed.initial())
                next_key = control.Store.processing_key(settings)
                changed = previous_key != next_key
                if changed:
                    # Never execute an old queued job with a new recipe. Old completed
                    # products are retained; scan produces fresh content-addressed jobs.
                    self.db.execute("UPDATE jobs SET state='superseded',error='configuration_changed' WHERE state IN ('pending','running')")
                    self.db.commit()
                self.cfg, self.patch = new_cfg, new_patch
                self.sources = {s['id']: s for s in new_cfg['sources'] if s.get('enabled', True)}
                self.source_errors = {k: v for k, v in self.source_errors.items() if k in self.sources}
                self.board_settings = copy.deepcopy(settings['board'])
                self.revision = doc['revision']
                self.revision_error = False
                if changed:
                    self.seen = {}
                station.atomic_json(applied, {'revision': self.revision, 'processing_key': next_key})
                self.catalog()
                self.heartbeat()
            except (OSError, ValueError, KeyError, TypeError):
                self.revision_error = True
                if not self.revision:
                    raise RuntimeError('Invalid control configuration at startup')
                LOG.exception('Rejected control revision; keeping last valid configuration')

        def tick(self):
            # tick/process are synchronous: reload only between complete jobs.
            self.reload()
            return BaseWorker.tick(self)

        def prepare_image(self, source_path, dest, job_id, index, source, file_time, native):
            entry = BaseWorker.prepare_image(self, source_path, dest, job_id, index, source, file_time, native, )
            passport = station.read_json(dest / ('{0:03d}.json'.format(index)))
            entry['layout'] = layout(passport, native)
            entry['settings_revision'] = self.revision or None
            # Preserve the complete original sidecar at entry['metadata'].
            # No guessed observation time, channels, calibration or orientation.
            return entry

        def process(self, job):
            """Native base algorithm with explicit editorial AND minimal discovery.

            Kept here instead of patching upstream source or wrapping a command shell.
            Input hashing, working copy, native timeout and atomic publication are
            shared with the existing worker.
            """
            job_id = job['id']
            final = self.data / 'public/items' / job_id
            if (final / 'item.json').is_file() and station.read_json(final / 'item.json')['job_id'] == job_id:
                return
            source = self.sources[job['source']]
            path = Path(job['path'])
            ready = path / '.ready' if source['kind'] == 'product' else Path(str(path) + '.ready')
            if source.get('require_ready', source['kind'] == 'product') and not ready.is_file():
                raise RuntimeError('Input readiness marker was removed')
            sig, files, total = station.inventory(path, source['kind'], self.cfg['max_input_mb'] * 1024 ** 2)
            if self.fingerprint(source, sig) != job['signature']:
                raise station.InputChanged('Input changed; wait for stable snapshot')
            if shutil.disk_usage(str(self.data)).free < total * 2 + self.cfg['min_free_mb'] * 1024 ** 2:
                raise RuntimeError('Insufficient free space')
            work = self.data / 'work' / job_id
            if work.exists():
                shutil.rmtree(str(work))
            incoming = work / 'input'
            incoming.mkdir(parents=True)
            file_time = path.stat().st_mtime
            base = path if source['kind'] == 'product' else path.parent
            last = time.monotonic()
            for original in files:
                target = incoming / original.relative_to(base)
                target.parent.mkdir(parents=True, exist_ok=True)
                with open(str(original), 'rb') as src, open(str(target), 'wb') as dst:
                    for block in iter(lambda: src.read(4 * 1024 * 1024), b''):
                        if station.STOP.is_set():
                            raise RuntimeError('Interrupted while copying')
                        dst.write(block)
                        if time.monotonic() - last > 5:
                            self.heartbeat('copying')
                            last = time.monotonic()
            current = station.inventory(path, source['kind'], self.cfg['max_input_mb'] * 1024 ** 2)[0]
            if self.fingerprint(source, current) != job['signature']:
                raise station.InputChanged('Input changed while copying')
            copied = [incoming / original.relative_to(base) for original in files]
            recipe = {'source': source, 'processing': self.patch, 'revision': self.cfg.get('processing_revision', '1')}
            if station.content_id(copied, incoming, recipe, lambda: self.heartbeat('verifying')) != job_id:
                raise station.InputChanged('Copied bytes differ from queued input')
            station.atomic_json(work / 'home/.config/satdump/settings.json', self.patch)
            patch_path = work / 'processing.json'
            station.atomic_json(patch_path, self.patch)
            result = incoming
            log_path = self.data / 'logs' / (job_id + '.log')
            if log_path.exists():
                os.replace(str(log_path), str(log_path) + '.previous')
            if source['kind'] == 'product':
                for product in sorted(incoming.rglob('product.cbor')):
                    for old in product.parent.glob('*_annotated*'):
                        if old.is_file():
                            old.unlink()
                    self.native([self.engine, 'reprocess', str(product.parent), str(patch_path)], work, log_path)
            elif source['kind'] == 'pipeline':
                result = work / 'decoded'
                argv = [self.engine, source['pipeline'], source['input_level'], str(incoming / path.name), str(result)]
                self.native(argv + source.get('options', []) + ['--offline', '--processing_config', str(patch_path)], work, log_path)
            if source['kind'] == 'image':
                images = [incoming / path.name]
            else:
                images = sorted(p for p in result.rglob('*.png') if p.is_file() and
                                p.name.endswith(('_annotated_presentation.png', '_annotated_minimal.png')))
            if not images:
                raise RuntimeError('No native presentation PNG/JSON; inspect recipe and native log')
            if len(images) > 1000:
                raise ValueError('More than 1000 presentation products in one pass')
            publication = work / 'publication'
            publication.mkdir()
            entries = [self.prepare_image(p, publication, job_id, i, source, file_time, source['kind'] != 'image')
                       for i, p in enumerate(images)]
            station.atomic_json(publication / 'item.json', {'job_id': job_id, 'entries': entries})
            archive = self.data / 'archive' / job_id
            if self.cfg.get('archive_science', True) and not archive.exists():
                os.rename(str(result), str(archive))
            os.rename(str(publication), str(final))
            if not self.cfg.get('keep_work', False):
                shutil.rmtree(str(work))

        def catalog(self):
            BaseWorker.catalog(self)  # Preserve existing gallery contract.
            entries = []
            for manifest in sorted((self.data / 'public/items').glob('*/item.json')):
                try:
                    entries.extend(station.read_json(manifest)['entries'])
                except (OSError, ValueError, KeyError):
                    LOG.warning('Skipped invalid publication manifest')
            items = sorted([e for e in entries if visible(e, self.board_settings)],
                           key=lambda e: (e['published_at'], e['id']), reverse=True)
            station.atomic_json(self.data / 'public/board.json', {
                'schema': 'satdump.board/1', 'updated_at': time.time(), 'settings_revision': self.revision or None,
                'title': self.cfg.get('title', 'Спутниковые наблюдения'), 'total': len(items),
                'hidden': len(entries) - len(items), 'items': items[:self.board_settings.get('max_items', 1000)]})

        def heartbeat(self, phase='idle'):
            BaseWorker.heartbeat(self, phase)
            station.atomic_json(self.data / 'public/board-status.json', {
                'schema': 'satdump.board.status/1', 'updated_at': time.time(), 'phase': phase,
                'applied_revision': self.revision or None, 'configuration_error': self.revision_error})

    class BoardHandler(station.GalleryHandler):
        def respond(self, body):
            endpoints = {'/api/v1/board': 'board.json', '/api/v1/board/status': 'board-status.json'}
            if self.path in endpoints:
                path = self.server.public / endpoints[self.path]
                try:
                    self.send_bytes(control.canonical(station.read_json(path, limit=32 * 1024 * 1024)), 'application/json', body)
                except (OSError, ValueError):
                    self.send_error(503, 'Waiting for worker publication')
                return
            path = unquote(urlsplit(self.path).path)
            reserved = path.startswith(('/api/', '/items/', '/health')) or path in ('/catalog.json', '/worker.json')
            if ui_root and not reserved:
                relative = 'index.html' if path == '/' else path.lstrip('/')
                types = {'.html': 'text/html; charset=utf-8', '.js': 'application/javascript; charset=utf-8',
                         '.css': 'text/css; charset=utf-8', '.json': 'application/json', '.svg': 'image/svg+xml',
                         '.png': 'image/png', '.jpg': 'image/jpeg', '.ico': 'image/x-icon',
                         '.woff': 'font/woff', '.woff2': 'font/woff2'}
                target = Path(ui_root) / relative
                safe = '\x00' not in path and '\\' not in path and not any(p.startswith('.') for p in relative.split('/'))
                if safe and target.suffix in types and station.contained(target, ui_root) and target.is_file():
                    with open(str(target), 'rb') as stream:
                        size = os.fstat(stream.fileno()).st_size
                        if size > 16 * 1024 * 1024:
                            self.send_error(413)
                            return
                        self.headers_ok(size, types[target.suffix])
                        if body:
                            shutil.copyfileobj(stream, self.wfile, 128 * 1024)
                    return
            if path in ('/', '/index.html'):
                self.send_bytes(control.canonical({'service': 'SatDump BOARD', 'ui_installed': False,
                                'catalog': '/api/v1/board', 'status': '/api/v1/board/status'}), 'application/json', body)
                return
            if path in ('/app.js', '/style.css'):
                self.send_error(404)
                return
            if self.path == '/health/ready':
                try:
                    status = station.read_json(self.server.public / 'worker.json')
                    alive = time.time() - status['updated_at'] < status['stale_after']
                except (OSError, ValueError, KeyError):
                    alive = False
                if not alive:
                    self.send_error(503, 'Worker not ready')
                else:
                    self.send_bytes(b'{"ready":true}', 'application/json', body)
                return
            station.GalleryHandler.respond(self, body)

    class BoardServer(BaseServer):
        def __init__(self, address, public):
            BaseServer.__init__(self, address, public)
            self.RequestHandlerClass = BoardHandler

    return BoardWorker, BoardServer


def main():
    import station
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--ui-root', default='/opt/satdump-station/board-ui/current')
    args, remaining = parser.parse_known_args()
    sys.argv = [sys.argv[0]] + remaining
    worker, server = install_adapter(station, args.ui_root)
    station.Worker, station.GalleryServer = worker, server
    return station.main()


if __name__ == '__main__':
    sys.exit(main())
