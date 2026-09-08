#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build/verify the self-contained Astra 1.6 station payload, without changing libc."""
from __future__ import print_function
import argparse
import gzip
import hashlib
import json
import os
import re
import shutil
import subprocess
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def sha256(path):
    digest = hashlib.sha256()
    with open(str(path), 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def output_directory(value):
    # Python 3.5 Path.resolve() is strict: create the destination before resolving.
    path = Path(os.path.abspath(value))
    path.mkdir(parents=True, exist_ok=True)
    return path.resolve()


def paths(root):
    for path in sorted(root.rglob('*'), key=str):
        if path.is_symlink():
            if not path.exists() or os.path.commonpath([str(path.resolve()), str(root.resolve())]) != str(root.resolve()):
                raise ValueError('External or broken symlink: ' + str(path))
        if path.is_file():
            yield path


def verify(root):
    root = Path(root).resolve()
    expected = {}
    with open(str(root / 'SHA256SUMS'), encoding='utf-8') as stream:
        for line in stream:
            digest, relative = line.rstrip('\n').split('  ', 1)
            if not re.match(r'^[0-9a-f]{64}$', digest) or Path(relative).is_absolute() or '..' in Path(relative).parts:
                raise ValueError('Invalid checksum record')
            if relative in expected:
                raise ValueError('Duplicate checksum record')
            expected[relative] = digest
    actual = {str(p.relative_to(root)): sha256(p) for p in paths(root) if p != root / 'SHA256SUMS'}
    if actual != expected:
        differences = sorted(k for k in set(actual) | set(expected) if actual.get(k) != expected.get(k))
        raise ValueError('Package checksum mismatch: ' + ', '.join(differences[:10]))
    print('Verified {0} files'.format(len(expected)))


def glibc_floor(root):
    maximum, count = (0, 0), 0
    env = os.environ.copy()
    env['LC_ALL'] = 'C'
    for path in paths(root):
        with open(str(path), 'rb') as stream:
            if stream.read(4) != b'\x7fELF':
                continue
        count += 1
        header = subprocess.check_output(['readelf', '-h', str(path)], env=env).decode('utf-8', 'replace')
        if 'Advanced Micro Devices X86-64' not in header:
            raise ValueError('Not an x86_64 ELF: ' + str(path))
        out = subprocess.check_output(['readelf', '--version-info', str(path)], stderr=subprocess.STDOUT, env=env).decode('utf-8', 'replace')
        for major, minor in re.findall(r'GLIBC_([0-9]+)\.([0-9]+)', out):
            maximum = max(maximum, (int(major), int(minor)))
    if count == 0 or maximum > (2, 24):
        raise ValueError('Missing ELF payload or GLIBC requirement exceeds 2.24: ' + str(maximum))
    return '.'.join(map(str, maximum)), count


def pack(args):
    engine = Path(args.engine).resolve()
    runtime = Path(args.runtime).resolve()
    for file_path in (engine / 'satdump', engine / 'bin/satdump', runtime / 'python'):
        if not os.access(str(file_path), os.X_OK):
            raise ValueError('Missing executable: ' + str(file_path))
    if not (engine / 'PORTABLE-MANIFEST.txt').is_file():
        raise ValueError('Use the glibc224 portable engine, not the Astra 1.7 package')
    with open(str(engine / 'PORTABLE-MANIFEST.txt')) as stream:
        if 'glibc_allowed_max=2.24' not in stream.read():
            raise ValueError('Engine baseline must be glibc 2.24')
    proc = subprocess.Popen([str(engine / 'satdump'), 'reprocess'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        output = proc.communicate(timeout=30)[0]
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        raise ValueError('Engine command probe timed out')
    if proc.returncode != 2 or b'Usage: satdump reprocess' not in output:
        raise ValueError('Engine has no station reprocess command: rebuild this branch')
    subprocess.check_call([str(runtime / 'python'), '-c', 'import sqlite3,ssl;from PIL import Image;print("runtime OK")'])
    revision = args.revision
    if not re.match(r'^[0-9a-f]{12,40}$', revision):
        raise ValueError('revision must be the built Git commit SHA')
    release_id = '1.2.2-astra16-station-' + revision[:12]
    output_dir = output_directory(args.output)
    bundle = output_dir / ('satdump-' + release_id + '-x86_64')
    if bundle.exists():
        raise ValueError('Output already exists; use a clean output directory: ' + str(bundle))
    bundle.mkdir()
    shutil.copytree(str(engine), str(bundle / 'engine'), symlinks=True)
    shutil.copytree(str(runtime), str(bundle / 'runtime'), symlinks=True)
    for relative in ('services/station', 'scripts/station', 'config/station', 'tests/station'):
        shutil.copytree(str(ROOT / relative), str(bundle / relative),
                        ignore=shutil.ignore_patterns('__pycache__', '*.pyc', '*.pyo'))
    shutil.copyfile(str(ROOT / 'station.sh'), str(bundle / 'station.sh'))
    shutil.copyfile(str(ROOT / 'docs/ru/STATION_ASTRA16.md'), str(bundle / 'START_HERE.ru.md'))
    shutil.copyfile(str(ROOT / 'docs/ru/STATION_OPERATIONS.md'), str(bundle / 'OPERATIONS.ru.md'))
    shutil.copyfile(str(ROOT / 'LICENSE'), str(bundle / 'LICENSE'))
    with open(str(bundle / 'install.sh'), 'w') as stream:
        stream.write('#!/usr/bin/env bash\nset -Eeuo pipefail\nROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"\nexec bash "$ROOT/scripts/station/install.sh" "$@"\n')
    for relative in ('station.sh', 'install.sh'):
        os.chmod(str(bundle / relative), 0o755)
    required, elf_count = glibc_floor(bundle)
    manifest = {'schema': 'satdump.station.package/1', 'release_id': release_id,
                'git_commit': revision, 'target': 'Astra Linux 1.6 x86_64',
                'glibc_required': required, 'glibc_allowed_max': '2.24', 'elf_count': elf_count,
                'system_glibc_replaced': False, 'native_astra16_acceptance': 'not implied by packaging',
                'services': ['satdump-worker', 'satdump-web']}
    with open(str(bundle / 'PACKAGE-MANIFEST.json'), 'w') as stream:
        json.dump(manifest, stream, indent=2, sort_keys=True)
    records = list(paths(bundle))
    with open(str(bundle / 'SHA256SUMS'), 'w', encoding='utf-8') as stream:
        for path in records:
            stream.write(sha256(path) + '  ' + str(path.relative_to(bundle)) + '\n')
    verify(bundle)
    epoch = int(os.environ.get('SOURCE_DATE_EPOCH', '0'))
    archive = Path(str(bundle) + '.tar.gz')
    with open(str(archive), 'wb') as raw:
        with gzip.GzipFile(filename='', fileobj=raw, mode='wb', mtime=epoch) as zipped:
            with tarfile.open(fileobj=zipped, mode='w') as tar:
                for path in [bundle] + sorted(bundle.rglob('*'), key=str):
                    info = tar.gettarinfo(str(path), arcname=str(path.relative_to(output_dir)))
                    info.uid = info.gid = 0
                    info.uname = info.gname = ''
                    info.mtime = epoch
                    if info.isfile():
                        with open(str(path), 'rb') as stream:
                            tar.addfile(info, stream)
                    else:
                        tar.addfile(info)
    with open(str(archive) + '.sha256', 'w') as stream:
        stream.write(sha256(archive) + '  ' + archive.name + '\n')
    print(str(archive))


def main():
    parser = argparse.ArgumentParser(description='Упаковщик SatDump Station для Astra 1.6')
    parser.add_argument('--verify')
    parser.add_argument('--engine')
    parser.add_argument('--runtime')
    parser.add_argument('--output', default='dist/station')
    parser.add_argument('--revision', default='')
    args = parser.parse_args()
    if args.verify:
        verify(args.verify)
    else:
        if not args.engine or not args.runtime:
            parser.error('--engine and --runtime are required')
        pack(args)


if __name__ == '__main__':
    main()
