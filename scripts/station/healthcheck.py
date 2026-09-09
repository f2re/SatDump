#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Verify BOTH public backend and authenticated control; never print the token."""
from __future__ import print_function
import argparse
import json
import time
from urllib.request import Request, urlopen
from urllib.error import URLError, HTTPError


def check(host, port, control_port, token):
    host = '127.0.0.1' if host == '0.0.0.0' else host
    def get(url, auth=False):
        request = Request(url, headers={'Authorization': 'Bearer ' + token} if auth else {})
        with urlopen(request, timeout=2) as response:
            return json.loads(response.read().decode('utf-8'))
    public = 'http://' + host + ':' + str(port)
    local = 'http://127.0.0.1:' + str(control_port)
    health = get(public + '/health.json')
    board = get(public + '/api/v1/board')
    control = get(local + '/api/v1/control/health', True)
    if not (health.get('web_alive') and health.get('worker_alive') and control.get('control_alive') and board.get('schema') == 'satdump.board/1'):
        return False
    try:
        get(local + '/api/v1/control/config')
        return False
    except HTTPError as error:
        return error.code == 401


def main():
    parser = argparse.ArgumentParser(description='Проверка запуска Station')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8090)
    parser.add_argument('--control-port', type=int, default=8091)
    parser.add_argument('--token-file', required=True)
    args = parser.parse_args()
    with open(args.token_file) as stream:
        token = stream.read().strip()
    for unused in range(20):
        try:
            if check(args.host, args.port, args.control_port, token):
                print('WEB, BOARD, worker heartbeat and authenticated control OK')
                return
        except (OSError, ValueError, URLError):
            pass
        time.sleep(1)
    raise SystemExit('Station health check failed; inspect systemd logs. No credentials printed.')


if __name__ == '__main__':
    main()
