#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Repackage installer-only fixes with explicit, verified native provenance.

This is not a substitute for a native build: any change outside the reviewed
maintenance allowlist is rejected. The new archive passes normal acceptance.
"""
from __future__ import print_function
import argparse
import gzip
import hashlib
import json
import os
import posixpath
import re
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path
import pack

ROOT = Path(__file__).resolve().parents[2]
ALLOWED = frozenset((
    'scripts/station/install.sh', 'scripts/station/maintenance.py',
    'tests/station/test_install_permissions.py', 'tests/station/test_maintenance.py',
    '.github/workflows/station-maintenance.yml',
    'docs/ru/station/RELEASE_MAINTENANCE.md',
))


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT)] + list(args)).decode('utf-8').strip()


def permitted_changes(paths):
    rejected = sorted(set(paths) - ALLOWED)
    if rejected:
        raise ValueError('Full native build required; changes outside maintenance scope: ' + ', '.join(rejected))


def extract(archive, destination):
    """Check every member before extracting a trusted CI artifact to a private dir."""
    with tarfile.open(str(archive), 'r:gz') as stream:
        members = stream.getmembers()
        names, links, roots = set(), set(), set()
        if not members or len(members) > 50000:
            raise ValueError('Invalid archive member count')
        for member in members:
            name = member.name.rstrip('/')
            if not name or name.startswith('/') or '\\' in name or '..' in name.split('/') or posixpath.normpath(name) != name:
                raise ValueError('Unsafe archive member')
            if name in names or not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                raise ValueError('Duplicate or special archive member')
            names.add(name)
            roots.add(name.split('/')[0])
            if member.issym() or member.islnk():
                target = posixpath.normpath(member.linkname if member.islnk() else
                                           posixpath.join(posixpath.dirname(name), member.linkname))
                if target.startswith('/') or target.split('/')[0] != name.split('/')[0]:
                    raise ValueError('Escaping archive link')
                links.add(name)
        if len(roots) != 1 or not next(iter(roots)).startswith('satdump-'):
            raise ValueError('Expected one SatDump package directory')
        for name in names:
            parts = name.split('/')
            if any('/'.join(parts[:i]) in links for i in range(1, len(parts))):
                raise ValueError('Archive member nested under a link')
        stream.extractall(str(destination))
    return Path(destination) / next(iter(roots))


def component_digest(root):
    records = []
    for path in sorted(Path(root).rglob('*'), key=str):
        relative = str(path.relative_to(root))
        mode = path.lstat().st_mode & 0o777
        if path.is_symlink():
            records.append([relative, mode, 'link', os.readlink(str(path))])
        elif path.is_file():
            records.append([relative, mode, 'file', pack.sha256(path)])
        elif path.is_dir():
            records.append([relative, mode, 'directory'])
        else:
            raise ValueError('Special file in native component')
    return hashlib.sha256(json.dumps(records, ensure_ascii=True, sort_keys=True).encode('ascii')).hexdigest()


def write_archive(bundle, output, epoch):
    checksum = bundle / 'SHA256SUMS'
    if checksum.exists():
        checksum.unlink()
    records = list(pack.paths(bundle))
    with checksum.open('w', encoding='utf-8') as stream:
        for path in records:
            stream.write(pack.sha256(path) + '  ' + str(path.relative_to(bundle)) + '\n')
    pack.verify(bundle)
    archive = Path(output) / (bundle.name + '.tar.gz')
    with archive.open('wb') as raw:
        with gzip.GzipFile(filename='', fileobj=raw, mode='wb', mtime=epoch) as zipped:
            with tarfile.open(fileobj=zipped, mode='w') as tar:
                for path in [bundle] + sorted(bundle.rglob('*'), key=str):
                    info = tar.gettarinfo(str(path), arcname=str(path.relative_to(output)))
                    info.uid = info.gid = 0
                    info.uname = info.gname = ''
                    info.mtime = epoch
                    if info.isfile():
                        with path.open('rb') as source:
                            tar.addfile(info, source)
                    else:
                        tar.addfile(info)
    Path(str(archive) + '.sha256').write_text(pack.sha256(archive) + '  ' + archive.name + '\n')
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True)
    parser.add_argument('--source-sha', required=True)
    parser.add_argument('--source-run', required=True, type=int)
    parser.add_argument('--output', default='dist/station-maintenance')
    args = parser.parse_args()
    if not re.match(r'^[0-9a-f]{40}$', args.source_sha) or args.source_run <= 0:
        parser.error('An exact source SHA and CI run are required')
    revision = git('rev-parse', 'HEAD')
    subprocess.check_call(['git', '-C', str(ROOT), 'merge-base', '--is-ancestor', args.source_sha, revision])
    if git('status', '--porcelain', '--untracked-files=no'):
        raise ValueError('Commit all tracked changes before packaging')
    changes = git('diff', '--no-renames', '--name-only', args.source_sha, revision).splitlines()
    permitted_changes(changes)
    output = pack.output_directory(args.output)
    release_id = '1.2.2-astra16-station-' + revision[:12]
    bundle = output / ('satdump-' + release_id + '-x86_64')
    if bundle.exists() or Path(str(bundle) + '.tar.gz').exists():
        raise ValueError('Refusing to overwrite an existing package')
    with tempfile.TemporaryDirectory(prefix='satdump-maintenance-') as tmp:
        original = extract(Path(args.archive), tmp)
        pack.verify(original)
        with (original / 'PACKAGE-MANIFEST.json').open() as stream:
            manifest = json.load(stream)
        if manifest['git_commit'] != args.source_sha or manifest['glibc_allowed_max'] != '2.24':
            raise ValueError('Wrong source package or native baseline')
        if manifest.get('maintenance'):
            raise ValueError('Reuse the original full-build package, not a chain of maintenance packages')
        components = {name: component_digest(original / name) for name in ('engine', 'runtime')}
        shutil.copytree(str(original), str(bundle), symlinks=True)
        for relative in changes:
            if relative.startswith('.github/'):
                continue
            source, target = ROOT / relative, bundle / relative
            if not source.is_file() or source.is_symlink():
                raise ValueError('Maintenance changes must be regular committed files: ' + relative)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(str(source), str(target))
        for name, digest in components.items():
            if component_digest(bundle / name) != digest:
                raise ValueError('Native component was changed: ' + name)
        provenance = {'schema': 'satdump.station.maintenance/1', 'source_git_commit': args.source_sha,
                      'source_workflow_run': args.source_run, 'source_archive_sha256': pack.sha256(Path(args.archive)),
                      'package_git_commit': revision, 'changes': changes, 'native_components_unchanged': True,
                      'component_tree_sha256': components,
                      'engine_git_commit': args.source_sha, 'runtime_git_commit': args.source_sha}
        manifest.update(release_id=release_id, git_commit=revision, maintenance=provenance)
        with (bundle / 'PACKAGE-MANIFEST.json').open('w') as stream:
            json.dump(manifest, stream, sort_keys=True, indent=2)
        archive = write_archive(bundle, output, int(git('show', '-s', '--format=%ct', revision)))
        provenance['archive_sha256'] = pack.sha256(archive)
        with (output / 'STATION-BUILD-PROVENANCE.json').open('w') as stream:
            json.dump(provenance, stream, sort_keys=True, indent=2)
    print(str(archive))


if __name__ == '__main__':
    main()
