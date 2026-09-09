#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Reuse pinned native/runtime bytes only when their complete source inputs match.

This is a build-host optimization, never a bypass of offline or service acceptance.
Station Python/Bash files are always packaged from the current checkout.
"""
from __future__ import print_function
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SAFE_FILES = {'README.md', 'CHANGELOG.md', 'CONTRIBUTING.md', 'station.sh', 'install.sh'}
SAFE_SCRIPTS = {'install.sh', 'terminal.sh', 'configure.py', 'deploy.sh', 'ui-deploy.sh',
                'healthcheck.py', 'pack.py', 'acceptance.py', 'reuse-components.py', 'README.md'}


def allowed_change(path):
    if path in SAFE_FILES:
        return True
    if path.startswith(('docs/', 'services/station/', 'tests/station/', 'config/station/', '.github/workflows/')):
        return True
    return path.startswith('scripts/station/') and path[len('scripts/station/'):] in SAFE_SCRIPTS


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT)] + list(args)).decode('utf-8').strip()


def load_plan(path):
    with open(str(path)) as stream:
        plan = json.load(stream)
    if plan.get('schema') != 'satdump.station.component-cache/1':
        raise ValueError('Unsupported component-cache schema')
    if not re.match(r'^[0-9a-f]{40}$', plan.get('source_revision', '')):
        raise ValueError('Exact component source revision is required')
    if not re.match(r'^[0-9a-f]{64}$', plan.get('archive_sha256', '')):
        raise ValueError('Pinned archive SHA256 is required')
    if type(plan.get('run_id')) is not int or plan['run_id'] <= 0:
        raise ValueError('Invalid source run id')
    if not re.match(r'^[A-Za-z0-9_.-]+$', plan.get('artifact_name', '')):
        raise ValueError('Invalid artifact name')
    return plan


def reusable(plan):
    base = plan['source_revision']
    if subprocess.call(['git', '-C', str(ROOT), 'merge-base', '--is-ancestor', base, 'HEAD']):
        return False
    changed = git('diff', '--no-renames', '--name-only', base, 'HEAD', '--').splitlines()
    return all(allowed_change(path) for path in changed)


def checksum(path):
    digest = hashlib.sha256()
    with open(str(path), 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def repack(plan, downloaded, output):
    if not reusable(plan):
        raise ValueError('Native/runtime inputs changed: perform a full build')
    if git('status', '--porcelain', '--untracked-files=no'):
        raise ValueError('Package only a clean committed checkout')
    archives = list(Path(downloaded).glob('*.tar.gz'))
    if len(archives) != 1 or checksum(archives[0]) != plan['archive_sha256']:
        raise ValueError('Pinned component archive checksum mismatch')
    revision = git('rev-parse', 'HEAD')
    # The entire archive is trusted only AFTER the pinned hash above matched.
    with tempfile.TemporaryDirectory(prefix='station-components-') as temp:
        with tarfile.open(str(archives[0]), 'r:gz') as archive:
            for member in archive.getmembers():
                if member.name.startswith('/') or '..' in Path(member.name).parts or member.isdev() or member.isfifo():
                    raise ValueError('Unsafe package member')
            archive.extractall(temp)
        roots = [p for p in Path(temp).iterdir() if p.is_dir()]
        if len(roots) != 1:
            raise ValueError('Expected one package root')
        old = roots[0]
        subprocess.check_call([sys.executable, str(ROOT / 'scripts/station/pack.py'), '--verify', str(old)])
        with open(str(old / 'PACKAGE-MANIFEST.json')) as stream:
            manifest = json.load(stream)
        if manifest['git_commit'] != plan['source_revision']:
            raise ValueError('Component package source revision mismatch')
        if manifest.get('component_provenance'):
            raise ValueError('Pin a fully built component package, not a previous repackage')
        provenance = dict(plan)
        provenance.update(station_revision=revision, native_inputs_unchanged=True,
                          changed_paths=git('diff', '--no-renames', '--name-only', plan['source_revision'], 'HEAD', '--').splitlines())
        receipt = Path(temp) / 'component-provenance.json'
        receipt.write_text(json.dumps(provenance, indent=2) + '\n')
        env = os.environ.copy()
        env['SOURCE_DATE_EPOCH'] = git('show', '-s', '--format=%ct', 'HEAD')
        subprocess.check_call([sys.executable, str(ROOT / 'scripts/station/pack.py'),
                               '--engine', str(old / 'engine'), '--runtime', str(old / 'runtime'),
                               '--output', output, '--revision', revision,
                               '--component-provenance', str(receipt)], env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', default=str(ROOT / 'config/station/component-cache.json'))
    parser.add_argument('--select', action='store_true')
    parser.add_argument('--input')
    parser.add_argument('--output', default='dist/station')
    args = parser.parse_args()
    if args.select:
        try:
            plan = load_plan(args.plan)
            enabled = reusable(plan)
        except (OSError, ValueError, subprocess.CalledProcessError):
            plan, enabled = {}, False
        with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
            stream.write('reuse=' + ('true' if enabled else 'false') + '\n')
            if enabled:
                stream.write('run_id=' + str(plan['run_id']) + '\n')
                stream.write('artifact_name=' + plan['artifact_name'] + '\n')
        print('Native/runtime input match: ' + str(enabled) + '; all package acceptance gates remain mandatory')
    else:
        if not args.input:
            parser.error('--input is required')
        repack(load_plan(args.plan), args.input, args.output)


if __name__ == '__main__':
    main()
