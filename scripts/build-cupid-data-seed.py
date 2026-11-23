#!/usr/bin/env python3
"""Create or reuse the cupid iOS 27 Data seed from the IPSW OS volume.

The seed is installation state: the first run creates it, later runs reuse the
same file so the APFS Data volume UUID stays bound to the kernel. The laptop /
runner needs macOS hdiutil; the seed is generated once and cached.

Emits a JSON object {data_ramdisk, uuid, bytes, sha256} on stdout and writes
<output>.json beside the seed.
"""
import argparse
import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
import uuid
from contextlib import contextmanager
from pathlib import Path

BLOCK = 4096


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        while chunk := stream.read(8 << 20):
            digest.update(chunk)
    return digest.hexdigest()


@contextmanager
def os_mount(image: Path, work: Path):
    """Attach a read-only OS disk image and yield its mount point."""
    info = plistlib.loads(subprocess.check_output(['hdiutil', 'info', '-plist']))
    for entry in info.get('images', []):
        if Path(entry.get('image-path', '')).resolve() != image.resolve():
            continue
        if entry.get('writeable', True):
            raise SystemExit(f'{image} is already attached writable')
        mounts = [Path(item['mount-point']) for item in entry.get('system-entities', [])
                  if item.get('mount-point') and (Path(item['mount-point']) / 'sbin/launchd').is_file()]
        if len(mounts) == 1:
            yield mounts[0]
            return
    mount = work / 'os-mount'
    mount.mkdir()
    attached = plistlib.loads(subprocess.check_output([
        'hdiutil', 'attach', '-readonly', '-nobrowse', '-mountpoint', str(mount),
        '-plist', str(image)]))
    device = attached['system-entities'][0]['dev-entry']
    try:
        yield mount
    finally:
        subprocess.run(['hdiutil', 'detach', device], check=True, capture_output=True)


def seed_uuid(unixnu: Path, path: Path):
    """The single APFS volume UUID of a packed (DBRDISK1) seed."""
    packed = unixnu / 'drivers/DarwinBoot/Test/packed_ramdisk.py'
    if not packed.is_file():
        raise SystemExit(f'missing {packed}; run unixnu fetch first')
    sys.path.insert(0, str(packed.parent))
    import packed_ramdisk
    _, extents = packed_ramdisk.regions(path.read_bytes())
    identities = set()
    for _, data in extents:
        for at in range(0, len(data), BLOCK):
            block = data[at:at + BLOCK]
            if block[32:36] == b'APSB':
                identities.add(str(uuid.UUID(bytes=bytes(block[240:256]))).upper())
    if len(identities) != 1:
        raise SystemExit(f'{path}: need one APFS volume UUID, found {sorted(identities)}')
    return identities.pop()


def report(path: Path, identity: str):
    result = dict(data_ramdisk=str(path), uuid=identity, bytes=path.stat().st_size,
                  sha256=sha256_file(path))
    path.with_suffix(path.suffix + '.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--unixnu', type=Path, required=True, help='UniXNU checkout')
    parser.add_argument('--os-image', type=Path, help='OS disk image or .aea from unixnu ipsw fetch OS')
    parser.add_argument('--output', type=Path, required=True, help='cupid-data.dbr')
    parser.add_argument('--gib', type=int, default=64)
    parser.add_argument('--build', default='24A446')
    parser.add_argument('--skip-setup', action='store_true')
    parser.add_argument('--if-missing', action='store_true', help='reuse an existing seed and only rescan it')
    args = parser.parse_args()
    unixnu = args.unixnu.resolve()
    output = args.output.resolve()
    if output.is_file():
        report(output, seed_uuid(unixnu, output))
        return
    if args.if_missing:
        raise SystemExit(f'{output}: missing and --if-missing was given')
    if not args.os_image:
        raise SystemExit('--os-image is required for a first install')
    image = args.os_image.resolve()
    if image.suffix == '.aea':
        sys.path.insert(0, str(unixnu / 'scripts'))
        from unixnu.ipsw import decrypt_aea
        image = decrypt_aea(image)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='cupid-data-', dir=output.parent) as td:
        work = Path(td)
        with os_mount(image, work) as mount:
            candidate = work / 'data.dbr'
            command = [sys.executable, str(unixnu / 'scripts/make-data-seed.py'), str(mount),
                       str(candidate), '--gib', str(args.gib), '--build', args.build]
            if args.skip_setup:
                command.append('--skip-setup')
            subprocess.run(command, check=True)
        staged = output.with_suffix(output.suffix + '.new')
        shutil.copyfile(candidate, staged)
        os.replace(staged, output)
    report(output, seed_uuid(unixnu, output))


if __name__ == '__main__':
    main()
