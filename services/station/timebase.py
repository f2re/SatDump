# -*- coding: utf-8 -*-
"""UTC metadata and clock-change-safe scheduling. Python 3.5+."""
from __future__ import print_function
from datetime import datetime, timezone
import json
import logging
import math
import os
from pathlib import Path
import re
import subprocess
import time

UTC = timezone.utc
UNKNOWN = ('', 'Время наблюдения не указано')
ISO = re.compile(r'^(\d{4}-\d{2}-\d{2})[T ](\d{2}:\d{2})(?::(\d{2})(\.\d{1,6})?)?(Z|[+-]\d{2}:?\d{2})$')
DAY = r'\d{2}\.\d{2}\.\d{4}'
HMS = r'\d{2}:\d{2}:\d{2}'


def configure_utc():
    """Set this process and its children to UTC; do not change the host clock."""
    os.environ['TZ'] = 'UTC'
    if hasattr(time, 'tzset'):
        time.tzset()
    logging.Formatter.converter = time.gmtime


def boot_id():
    try:
        value = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        return value if re.match(r'^[0-9a-f-]{36}$', value) else None
    except OSError:
        return None


def instant(value):
    """Parse an explicitly zoned ISO instant. Never assume a local timezone."""
    match = ISO.match(value)
    if not match:
        raise ValueError('Expected ISO timestamp with Z or an explicit UTC offset')
    day, hm, second, fraction, zone = match.groups()
    zone = '+0000' if zone == 'Z' else zone.replace(':', '')
    text = day + 'T' + hm + ':' + (second or '00') + (fraction or '') + zone
    fmt = '%Y-%m-%dT%H:%M:%S' + ('.%f' if fraction else '') + '%z'
    return datetime.strptime(text, fmt).astimezone(UTC)


def acquisition_interval(value):
    """Read ISO instants or the three UTC interval formats emitted by SatDump."""
    text = ' '.join(value.replace('·', ' ').split())
    if not text.endswith(' UTC'):
        moment = instant(text)
        return moment, moment
    text = text[:-4]
    if re.match(r'^\d{4}-', text):
        moment = instant(text + 'Z')
        return moment, moment
    same_day = re.match(r'^(' + DAY + r') (' + HMS + r')(?:\s*[–—]\s*(' + HMS + r'))?$', text)
    two_days = re.match(r'^(' + DAY + r' ' + HMS + r')\s*[–—]\s*(' + DAY + r' ' + HMS + r')$', text)
    if same_day:
        day, start, end = same_day.groups()
        first, last = day + ' ' + start, day + ' ' + (end or start)
    elif two_days:
        first, last = two_days.groups()
    else:
        raise ValueError('Unrecognized UTC observation interval')
    start = datetime.strptime(first, '%d.%m.%Y %H:%M:%S').replace(tzinfo=UTC)
    end = datetime.strptime(last, '%d.%m.%Y %H:%M:%S').replace(tzinfo=UTC)
    if end < start:
        raise ValueError('Observation end precedes start')
    return start, end


def iso_utc(value):
    return value.astimezone(UTC).isoformat().replace('+00:00', 'Z')


def acquisition_fields(raw):
    """Keep the input label, and derive canonical fields without using file mtime."""
    raw = raw if isinstance(raw, str) else str(raw)
    result = {'acquisition_time_raw': raw[:240], 'acquisition_time': '',
              'acquisition_start_utc': None, 'acquisition_end_utc': None,
              'acquisition_start': None, 'acquisition_end': None,
              'acquisition_time_status': 'missing' if raw.strip() in UNKNOWN else 'invalid'}
    if result['acquisition_time_status'] == 'missing':
        return result
    try:
        start, end = acquisition_interval(raw)
        if start == end:
            label = start.strftime('%d.%m.%Y · %H:%M:%S UTC')
        elif start.date() == end.date():
            label = start.strftime('%d.%m.%Y · %H:%M:%S') + '–' + end.strftime('%H:%M:%S UTC')
        else:
            label = start.strftime('%d.%m.%Y %H:%M:%S') + ' – ' + end.strftime('%d.%m.%Y %H:%M:%S UTC')
        result.update(acquisition_time=label, acquisition_start_utc=iso_utc(start),
                      acquisition_end_utc=iso_utc(end), acquisition_start=start.timestamp(),
                      acquisition_end=end.timestamp(), acquisition_time_status='ok')
    except (ValueError, OverflowError, OSError):
        pass
    return result


def normalize_entry(entry):
    result = dict(entry)
    result.update(acquisition_fields(entry.get('acquisition_time_raw', entry.get('acquisition_time', ''))))
    return result


def finite_number(value):
    return not isinstance(value, bool) and isinstance(value, (int, float)) and math.isfinite(value)


def catalog_order(entry):
    observed = entry.get('acquisition_start')
    published = entry.get('published_at', 0)
    return (finite_number(observed), observed if finite_number(observed) else
            (published if finite_number(published) else 0), entry.get('id', ''))


def heartbeat_fields():
    return {'updated_at': time.time(), 'updated_monotonic': time.monotonic(),
            'boot_id': boot_id(), 'timezone': 'UTC'}


def heartbeat_alive(status):
    """Monotonic age on the same boot; reject future or stale legacy timestamps."""
    try:
        limit = status['stale_after']
        if not finite_number(limit) or limit <= 0:
            return False
        if status.get('boot_id'):
            if status['boot_id'] != boot_id():
                return False
            last = status['updated_monotonic']
            if not finite_number(last):
                return False
            age = time.monotonic() - last
        else:
            last = status['updated_at']
            if not finite_number(last):
                return False
            age = time.time() - last
        return 0 <= age < limit
    except (KeyError, TypeError, ValueError):
        return False


def clock_status():
    """Read the host's synchronization report; an active NTP service is not proof."""
    result = {'display_timezone': 'UTC', 'host_timezone': None,
              'synchronized': None, 'rtc_local': None, 'provider': None,
              'checked_at': time.time()}
    env = os.environ.copy()
    env['LC_ALL'] = 'C'
    commands = [['timedatectl', 'show', '-p', 'NTPSynchronized', '-p', 'Timezone', '-p', 'LocalRTC'],
                ['timedatectl', 'status']]
    for command in commands:
        try:
            output = subprocess.check_output(command, stderr=subprocess.STDOUT, env=env, timeout=3).decode('utf-8', 'replace')
        except (OSError, subprocess.SubprocessError):
            continue
        for line in output.splitlines():
            line = line.strip()
            for prefix, field in [('NTPSynchronized=', 'synchronized'), ('LocalRTC=', 'rtc_local'),
                                  ('System clock synchronized:', 'synchronized'), ('NTP synchronized:', 'synchronized'),
                                  ('RTC in local TZ:', 'rtc_local')]:
                if line.startswith(prefix):
                    value = line[len(prefix):].strip().lower()
                    result[field] = {'yes': True, 'no': False}.get(value)
            for prefix in ('Timezone=', 'Time zone:'):
                if line.startswith(prefix):
                    result['host_timezone'] = line[len(prefix):].strip()
        result['provider'] = 'timedatectl'
        if result['synchronized'] is not None:
            break
    result['status'] = 'synchronized' if result['synchronized'] is True else ('unsynchronized' if result['synchronized'] is False else 'unknown')
    return result


if __name__ == '__main__':
    configure_utc()
    print(json.dumps(clock_status(), ensure_ascii=False, indent=2))
