#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Installation identity regressions; never change system services or accounts."""
from __future__ import print_function
import os
import pwd
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/station'))
import configure


class InstallPermissions(unittest.TestCase):
    def setUp(self):
        self.script = (ROOT / 'scripts/station/install.sh').read_text()

    def nginx_probe(self):
        line = next(line for line in self.script.splitlines()
                    if 'ui_step ' in line and '/usr/sbin/nginx -t' in line)
        # Execute the actual installer command, without its display wrapper.
        return shlex.split(line)[2:]

    def test_runtime_switch_precedes_service_probe(self):
        switch = self.script.index('PYTHON="$TARGET/runtime/python"')
        self.assertLess(self.script.index('mv "$STAGE" "$TARGET"'), switch)
        self.assertLess(switch, self.script.index('MUTATED=1', switch))
        self.assertLess(switch, self.script.index('runuser -u satdump-station -- "$PYTHON"'))
        self.assertNotIn('PYTHON="$PACKAGE/runtime/python"', self.script[switch:])

    def test_nginx_probe_identity_matches_service(self):
        self.assertEqual(['runuser', '-u', 'satdump-web', '--', '/usr/sbin/nginx', '-t'],
                         self.nginx_probe()[:6])
        units = configure.unit_files('/opt/satdump-station', '/etc/satdump-station',
                                     '/var/lib/satdump-station', '127.0.0.1', 8090, 8091, 8092, 'nginx')
        self.assertIn('User=satdump-web\n', units['satdump-web.service'])

    @unittest.skipUnless(os.geteuid() == 0 and shutil.which('runuser'), 'requires disposable root test environment')
    def test_private_download_directory_is_not_needed_by_service(self):
        with tempfile.TemporaryDirectory(prefix='satdump-permissions-') as name:
            root = Path(name)
            root.chmod(0o755)
            private, target = root / 'private', root / 'installed'
            private.mkdir(mode=0o700)
            (private / 'runtime').mkdir()
            (target / 'runtime').mkdir(parents=True)
            for directory in (private, target):
                launcher = directory / 'runtime/python'
                launcher.write_text('#!/bin/sh\nexit 0\n')
                launcher.chmod(0o755)
            blocked = subprocess.call(['runuser', '-u', 'nobody', '--', str(private / 'runtime/python')],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self.assertNotEqual(0, blocked)
            switch = next(line for line in self.script.splitlines() if line == 'PYTHON="$TARGET/runtime/python"')
            shell = 'PACKAGE=$1; TARGET=$2; PYTHON="$PACKAGE/runtime/python";\n' + switch + '\nrunuser -u nobody -- "$PYTHON"'
            subprocess.check_call(['/bin/bash', '-ec', shell, 'probe', str(private), str(target)])

    @unittest.skipUnless(os.geteuid() == 0 and shutil.which('runuser') and os.path.isfile('/usr/sbin/nginx'),
                         'requires nginx and disposable root test environment')
    def test_nginx_probe_allows_first_unprivileged_start(self):
        account = pwd.getpwnam('nobody')
        with tempfile.TemporaryDirectory(prefix='satdump-nginx-identity-') as name:
            root = Path(name)
            root.chmod(0o755)
            runtime = root / 'run'
            runtime.mkdir(mode=0o750)
            os.chown(str(runtime), account.pw_uid, account.pw_gid)
            with socket.socket() as sock:
                sock.bind(('127.0.0.1', 0))
                port = sock.getsockname()[1]
            config = root / 'nginx.conf'
            config.write_text(configure.nginx_config(str(root), '127.0.0.1', port, 1)
                              .replace('/run/satdump-web', str(runtime)))
            config.chmod(0o644)
            probe = [str(config) if arg == '$CONFIG/nginx.conf' else
                     ('nobody' if arg == 'satdump-web' else arg) for arg in self.nginx_probe()]
            subprocess.check_output(probe, stderr=subprocess.STDOUT)
            for path in runtime.rglob('*'):
                self.assertEqual(account.pw_uid, path.stat().st_uid, str(path))
            proc = subprocess.Popen(['runuser', '-u', 'nobody', '--', '/usr/sbin/nginx',
                                     '-c', str(config), '-g', 'daemon off;'],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                deadline = time.monotonic() + 5
                ready = False
                while time.monotonic() < deadline and proc.poll() is None:
                    try:
                        with socket.create_connection(('127.0.0.1', port), timeout=0.2):
                            ready = True
                            break
                    except OSError:
                        time.sleep(0.05)
                self.assertTrue(ready, 'nginx must start without a service restart or root ownership repair')
                self.assertIsNone(proc.poll())
            finally:
                if proc.poll() is None:
                    os.killpg(proc.pid, signal.SIGTERM)
                try:
                    proc.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL)
                    proc.communicate()


if __name__ == '__main__':
    unittest.main()
