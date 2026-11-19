#!/usr/bin/env python3
"""Restore the omitted DriverKit and TrustCache headers from the offline precache.

Same contract as UniXNU's script (same destinations, same log line) but it never
contacts GitHub. The headers ship in xnu-precache.tar.gz, which the CI step unpacks
into $CM_BUILD_DIR/.xnu-headers; the DeviceTree build runs this twice (once in the
patch phase and once in the baseline bootstrap), so every step is idempotent and a
missing `.cache/sources` listing can no longer fall back to a 403.
"""
import hashlib
import json
import os
from pathlib import Path

WORK = Path('.cache/xnu-work')
DEST = WORK / 'iokit' / 'DriverKit'
TRUST = WORK / 'EXTERNAL_HEADERS' / 'TrustCache'


def stage_root():
    candidates = []
    if os.environ.get('UNIXNU_HEADER_STAGE'):
        candidates.append(Path(os.environ['UNIXNU_HEADER_STAGE']))
    if os.environ.get('CM_BUILD_DIR'):
        candidates.append(Path(os.environ['CM_BUILD_DIR']) / '.xnu-headers')
    candidates.append(Path('/tmp/xnu-headers'))
    for candidate in candidates:
        if (candidate / 'iokit' / 'DriverKit').is_dir():
            return candidate
    return None


def install(source_dir, destination, relative=''):
    restored = []
    for source in sorted(source_dir.glob('*.h')):
        target = destination / relative / source.name
        if target.exists():
            continue
        data = source.read_bytes()
        if not data:
            raise SystemExit(f'empty header in the precache: {source}')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        restored.append({
            'name': relative + source.name,
            'source_tag': 'offline-precache',
            'git_blob': hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest(),
            'sha256': hashlib.sha256(data).hexdigest(),
        })
    return restored


def main():
    if not DEST.is_dir():
        raise SystemExit('Create the local XNU work source before preparing headers')
    stage = stage_root()
    if stage is None:
        raise SystemExit('the offline header precache is missing (xnu-precache.tar.gz)')
    restored = install(stage / 'iokit' / 'DriverKit', DEST)
    restored += install(stage / 'iokit' / 'DriverKit' / 'crypto', DEST, 'crypto')
    restored += install(stage / 'EXTERNAL_HEADERS' / 'TrustCache', TRUST)
    record = Path('.cache/sources/xnu-restored-headers.json')
    existing = json.loads(record.read_text()) if record.exists() else []
    by_name = {entry['name']: entry for entry in existing + restored}
    record.parent.mkdir(parents=True, exist_ok=True)
    record.write_text(json.dumps(list(by_name.values()), indent=2) + '\n')
    print(f'Restored {len(restored)} omitted public headers from Apple; blob hashes verified')


main()
