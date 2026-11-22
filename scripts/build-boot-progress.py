#!/usr/bin/env python3
"""Build the cupid boot-progress page against this source kernel's ABI.

Only the cupid page is compiled; the optional sheng process/latency/diag
providers are not available on this board.
"""
import hashlib
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/boot-progress'
OUT.mkdir(parents=True, exist_ok=True)
record = json.loads((ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json').read_text().strip().rstrip(','))
source = ROOT / 'kits/sheng/BootProgressCupid.c'
obj = OUT / 'BootProgress.o'
command = []
args = record['arguments']
i = 0
while i < len(args):
    arg = args[i]
    if arg == record['file']:
        command.append(str(source))
    elif arg in ('-o', '-MF', '-MT', '-MJ'):
        if arg == '-o':
            command += ['-o', str(obj)]
        i += 1
    elif arg == '-Xclang' and args[i + 1].startswith('-W'):
        i += 1
    elif arg == '-D' and args[i + 1].startswith('XNU_BOOT_PROGRESS='):
        i += 1
    elif not arg.startswith(('-W', '-DXNU_BOOT_PROGRESS=')):
        command.append(arg)
    i += 1
font = Path(os.environ.get('UNIXNU_UEFI') or ROOT / 'uefi-sheng') / 'Silicon/Silicium/SiliciumPkg/Library/FrameBufferSerialPortLib'
command += ['-DXNU_BOOT_PROGRESS=1', '-mgeneral-regs-only', '-Wall', '-Wextra',
            '-I', str(ROOT / '.cache/xnu-work/osfmk'),
            '-I', str(font)]
subprocess.run(command, cwd=record['directory'], check=True, timeout=120)
(OUT / 'compile-command.json').write_text(json.dumps(command, indent=2) + '\n')
archive = OUT / 'libBootProgress.a'
archive.unlink(missing_ok=True)
subprocess.run(['xcrun', 'ar', 'rcs', str(archive), str(obj)], check=True, timeout=30)
(OUT / 'build-report.json').write_text(json.dumps({
    'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
    'hardware_execution': False,
}, indent=2) + '\n')
print('PASS: cupid boot-progress page built')
