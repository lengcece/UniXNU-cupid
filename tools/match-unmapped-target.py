#!/usr/bin/env python3
"""Find the closest native function for an unmatched original kernel function.

Uses the XnuPrelink relocation-masked instruction shape (`kc_image.scan`) and
reports the best native candidates with their symbol names. This is a review
aid, not a replacement for the reviewed fixture: always inspect the candidate
before binding it.
"""

import argparse
import os
import pathlib
import sys

DEFAULT_KIT = pathlib.Path(__file__).resolve().parents[3] / 'build/unixnu-ios/kits/XnuPrelink'
KIT = pathlib.Path(os.environ.get('UNIXNU_KIT', DEFAULT_KIT))
sys.path.insert(0, str(KIT))

from kc_image import Image, scan  # noqa: E402
from fileset_match import Side  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--kernelcache', required=True)
    parser.add_argument('--native', required=True)
    parser.add_argument('--entry', default='com.apple.kernel')
    parser.add_argument('--target', action='append', required=True,
                        type=lambda value: int(value, 0))
    parser.add_argument('--top', type=int, default=8)
    args = parser.parse_args()

    raw = pathlib.Path(args.kernelcache).read_bytes()
    cache = Image(raw)
    entry = Image(raw, cache.entries[args.entry][1])
    original = Side(entry, cache.fixups(), entry.mapped())

    native_raw = pathlib.Path(args.native).read_bytes()
    native_image = Image(native_raw)
    native = Side(native_image, native_image.fixups(), native_image.mapped())

    for target in args.target:
        function = original.functions.get(target)
        if function is None:
            print(f'{target:#x}: not a function start in the original')
            continue
        shape = scan(original.image.read(target, function['end'] - target), target)[2]
        print(f'target {target:#x}  size={function["end"] - target}  '
              f'shape={len(shape)}  strings={list(function["strings"].values())[:3]}')
        best = []
        for start, candidate in native.functions.items():
            if abs(candidate['length'] - len(shape)) > max(12, len(shape) // 5):
                continue
            other = scan(native.image.read(start, candidate['end'] - start), start)[2]
            span = max(len(shape), len(other))
            if not span:
                continue
            same = sum(1 for left, right in zip(shape, other) if left == right)
            # Short functions: require a high ratio; long functions tolerate a
            # few compiler-inserted checks.
            score = same / span
            if score >= 0.55:
                name = native.name_at(start)[0] or '?'
                best.append((score, start, name, len(other)))
        best.sort(reverse=True)
        for score, start, name, length in best[:args.top]:
            print(f'  {score:5.2f}  {start:#018x}  {name:60s}  shape={length}')
        if not best:
            print('  no candidate above 0.55')
    return 0


if __name__ == '__main__':
    sys.exit(main())
