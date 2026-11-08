#!/usr/bin/env python3
"""Name the original kernel function behind a fileset uncategorized-call panic.

`unixnu_fileset_unmapped` panics with the return address of the original
component that called a kernel function this native kernel did not bind.  The
emitted kernel keeps the caller's address, but replaces the callee with a
stub.  To recover the callee, this tool:

  1. finds the owning component and the call instruction at ``pc - 4``,
  2. follows a direct call or a component import stub to its GOT slot,
  3. reads the same slot in the pinned original kernelcache, whose chained
     rebase still points at the original function,
  4. reports the owning segment, the function range and the strings the
     function references (panic wrappers usually name themselves).

The original kernelcache is the decompressed Mach-O whose sha256 matches the
profile's ``kernelcache_sha256`` pin, not the emitted ``kernel.macho``.

Example:

    tools/diagnose-unmapped-call.py \
        --kernel /tmp/run13-kernel.macho \
        --kernelcache /tmp/kernelcache.decompressed \
        --pc 0xfffffff00ab15578
"""

from __future__ import annotations

import argparse
import functools
import struct
import sys

MASK64 = (1 << 64) - 1
LC_SEGMENT_64 = 0x19
LC_FILESET_ENTRY = 0x80000035
LC_FUNCTION_STARTS = 0x26
UNMAPPED_STRING = b"fileset: original component called an unmapped kernel function"


@functools.lru_cache(maxsize=None)
def read_bytes(path: str) -> bytes:
    with open(path, "rb") as handle:
        return handle.read()


class MachO:
    """One Mach-O header inside a file; segment file offsets are absolute."""

    def __init__(self, data: bytes, at: int = 0):
        self.data = data
        magic, cpu, _, self.kind, count, _ = struct.unpack_from("<6I", data, at)
        if (magic, cpu) != (0xFEEDFACF, 0x0100000C):
            raise SystemExit(f"{at:#x}: not a 64-bit arm64 Mach-O (magic {magic:#x}, cpu {cpu:#x})")
        self.at = at
        self.segments: list[dict] = []
        self.entries: dict[str, tuple[int, int]] = {}
        offset = at + 32
        for _ in range(count):
            command, length = struct.unpack_from("<II", data, offset)
            if command == LC_SEGMENT_64:
                name = data[offset + 8:offset + 24].rstrip(b"\0").decode()
                va, size, fileoff, filesize = struct.unpack_from("<4Q", data, offset + 24)
                self.segments.append({"name": name, "va": va, "size": size,
                                      "fileoff": fileoff, "filesize": filesize})
            elif command == LC_FILESET_ENTRY:
                va, fileoff, name_at = struct.unpack_from("<QQI", data, offset + 8)
                end = data.index(b"\0", offset + name_at)
                self.entries[data[offset + name_at:end].decode()] = (va, fileoff)
            offset += length
        self.base = self.segments[0]["va"] if self.segments else 0

    def segment(self, va: int) -> dict | None:
        for segment in self.segments:
            if segment["va"] <= va < segment["va"] + segment["filesize"]:
                return segment
        return None

    def read(self, va: int, length: int) -> bytes:
        segment = self.segment(va)
        if segment is None:
            raise KeyError(f"unbacked address {va:#x}")
        at = segment["fileoff"] + va - segment["va"]
        return self.data[at:at + length]

    def read32(self, va: int) -> int:
        return struct.unpack("<I", self.read(va, 4))[0]

    def read64(self, va: int) -> int:
        return struct.unpack("<Q", self.read(va, 8))[0]

    def function_starts(self) -> list[int]:
        for command, offset, length in self._commands():
            if command == LC_FUNCTION_STARTS:
                dataoff, datasize = struct.unpack_from("<II", self.data, offset + 8)
                return self._uleb_starts(dataoff, datasize)
        return []

    def _commands(self):
        offset = self.at + 32
        count = struct.unpack_from("<I", self.data, self.at + 16)[0]
        for _ in range(count):
            command, length = struct.unpack_from("<II", self.data, offset)
            yield command, offset, length
            offset += length

    def _uleb_starts(self, at: int, size: int) -> list[int]:
        starts, current, end = [], 0, at + size
        while at < end:
            value = 0
            shift = 0
            while True:
                byte = self.data[at]
                at += 1
                value |= (byte & 0x7F) << shift
                if not byte & 0x80:
                    break
                shift += 7
            if value == 0:
                break
            current += value
            starts.append(self.base + current)
        return starts

    def strings_from(self, start: int, end: int, limit: int = 4) -> list[str]:
        found = []
        for pc in range(start, end - 8, 4):
            first, second = struct.unpack_from("<II", self.data, self.segment(pc)["fileoff"] + pc - self.segment(pc)["va"]) \
                if self.segment(pc) else (0, 0)
            if first & 0x9F000000 != 0x90000000 or second & 0xFFC00000 != 0x91000000:
                continue
            if (second & 31) != (first & 31):
                continue
            immediate = (((first >> 5) & 0x7FFFF) << 2) | ((first >> 29) & 3)
            if immediate & (1 << 20):
                immediate -= 1 << 21
            page = ((pc & ~0xFFF) + (immediate << 12)) & MASK64
            target = (page + ((second >> 10) & 0xFFF)) & MASK64
            segment = self._top_segment(target)
            if segment is None or segment["name"] not in ("__TEXT", "__PRELINK_TEXT", "__DATA_CONST", "__DATA"):
                continue
            at = segment["fileoff"] + target - segment["va"]
            stop = self.data.find(b"\0", at, at + 256)
            if stop <= at:
                continue
            text = self.data[at:stop]
            if not all(32 <= byte < 127 or byte in (9, 10) for byte in text):
                continue
            value = text.decode("latin-1")
            if value not in found:
                found.append(value)
            if len(found) >= limit:
                break
        return found

    def _top_segment(self, va: int) -> dict | None:
        return self.segment(va)


def owner_of(macho: MachO, address: int) -> tuple[str, str]:
    for name, (_, fileoff) in macho.entries.items():
        image = MachO(macho.data, fileoff)
        for segment in image.segments:
            if segment["va"] <= address < segment["va"] + segment["size"]:
                return name, segment["name"]
    return "com.apple.kernel", "?"


def function_containing(image: MachO, address: int) -> tuple[int, int]:
    starts = image.function_starts()
    if not starts:
        return address, address + 4
    import bisect
    index = bisect.bisect_right(starts, address) - 1
    if index < 0:
        return address, address + 4
    start = starts[index]
    end = starts[index + 1] if index + 1 < len(starts) else start + 4
    return start, end


def signed(value: int, bits: int) -> int:
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def decode_stub_slot(macho: MachO, address: int) -> int | None:
    """Follow the component's adrp/add/ldr/braa import stub to its slot."""
    words = [macho.read32(address + 4 * i) for i in range(4)]
    if words[0] & 0x9F000000 != 0x90000000 or words[1] & 0xFFC00000 != 0x91000000:
        return None
    if (words[1] & 31) != (words[0] & 31) or words[2] & 0xFFC00000 != 0xF9400000:
        return None
    # adrp/add x17; ldr x16, [x17] -- the load base is Rn, not the loaded register.
    if (words[2] >> 5) & 31 != (words[0] & 31):
        return None
    immediate = (((words[0] >> 5) & 0x7FFFF) << 2) | ((words[0] >> 29) & 3)
    if immediate & (1 << 20):
        immediate -= 1 << 21
    page = ((address & ~0xFFF) + (immediate << 12)) & MASK64
    offset = ((words[1] >> 10) & 0xFFF) << (12 if (words[1] >> 22) & 3 == 3 else 0)
    return (page + offset) & MASK64


def original_target(original: MachO, slot: int) -> tuple[int, str]:
    value = original.read64(slot)
    if value >> 63 & 1:
        if value >> 62 & 1:
            return value & 0xFFFF, "bind"
        return (original.base + (value & 0xFFFFFFFF)) & MASK64, "rebase"
    if value >> 62 & 1:
        return value & 0xFFFF, "bind"
    return value, "raw"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--kernel", required=True, help="emitted kernel.macho from the panicking image")
    parser.add_argument("--kernelcache", required=True, help="pinned decompressed original kernelcache")
    parser.add_argument("--pc", required=True, type=lambda value: int(value, 0),
                        help="return address from the unixnu_fileset_unmapped panic")
    args = parser.parse_args()

    emitted = MachO(read_bytes(args.kernel))
    original = MachO(read_bytes(args.kernelcache))
    pc = args.pc

    component, segment = owner_of(emitted, pc)
    print(f"panic pc:        {pc:#x}")
    print(f"caller:          {component}/{segment}")

    call_site = pc - 4
    instruction = emitted.read32(call_site)
    if instruction & 0xFC000000 != 0x94000000:
        print(f"call site:       {call_site:#x} is not a direct BL ({instruction:#x}); "
              "recover the target from the component's import stub or the prelink report")
        return 1
    target = (call_site + signed(instruction & 0x3FFFFFF, 26) * 4) & MASK64
    print(f"call:            {call_site:#x} bl -> {target:#x}")

    slot = None
    if owner_of(emitted, target)[0] == component:
        slot = decode_stub_slot(emitted, target)
    if slot is None:
        print(f"callee:          {target:#x} (direct; recover it in the original kernelcache at {call_site:#x})")
        if original.segment(call_site) is None:
            print("original call site is not backed; cannot recover the target")
            return 1
        original_instruction = original.read32(call_site)
        if original_instruction != instruction:
            target = (call_site + signed(original_instruction & 0x3FFFFFF, 26) * 4) & MASK64
            print(f"original callee: {target:#x}")
        else:
            print("original instruction is unchanged; the call already targets the stub")
            return 1
    else:
        print(f"import stub:     {target:#x} -> GOT slot {slot:#x}")
        target, kind = original_target(original, slot)
        if kind == "bind":
            print(f"original slot:   chained bind ordinal {target}; resolve it in the original kext's import table")
            return 0
        print(f"original callee: {target:#x}")

    owner, segment = owner_of(original, target)
    start, end = function_containing(MachO(original.data, original.entries[owner][1]), target)
    print(f"callee owner:    {owner}/{segment}")
    print(f"callee function: {start:#x}..{end:#x} ({end - start} bytes, pc+{target - start:#x})")

    image = MachO(original.data, original.entries[owner][1])
    strings = image.strings_from(start, min(end, start + 0x300))
    for text in strings:
        print(f"  string:        {text}")

    try:
        import capstone
        engine = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
        for index, instruction in enumerate(engine.disasm(original.read(start, min(end - start, 32)), start)):
            if index >= 6:
                break
            print(f"  {instruction.address:#x}: {instruction.mnemonic:9s} {instruction.op_str}")
    except ImportError:
        print("  (install capstone for disassembly)")

    resolved = emitted.read64(slot) if slot is not None else None
    if resolved is not None:
        resolved_target = (emitted.base + (resolved & 0xFFFFFFFF)) & MASK64
        print(f"emitted slot:    {resolved:#x} -> {resolved_target:#x}"
              + ("  (still the unmapped stub)" if resolved_target == find_stub(emitted) else ""))
    return 0


def find_stub(emitted: MachO) -> int | None:
    at = emitted.data.find(UNMAPPED_STRING)
    if at < 0:
        return None
    for segment in emitted.segments:
        if segment["fileoff"] <= at < segment["fileoff"] + segment["filesize"]:
            string_va = segment["va"] + at - segment["fileoff"]
            break
    else:
        return None
    for segment in emitted.segments:
        if segment["name"] != "__TEXT_EXEC":
            continue
        for pc in range(segment["va"], segment["va"] + segment["filesize"], 4):
            first, second = struct.unpack_from("<II", emitted.data, segment["fileoff"] + pc - segment["va"])
            if first & 0x9F000000 != 0x90000000 or second & 0xFFC00000 != 0x91000000:
                continue
            if (second & 31) != (first & 31):
                continue
            immediate = (((first >> 5) & 0x7FFFF) << 2) | ((first >> 29) & 3)
            if immediate & (1 << 20):
                immediate -= 1 << 21
            page = ((pc & ~0xFFF) + (immediate << 12)) & MASK64
            if (page + ((second >> 10) & 0xFFF)) & MASK64 == string_va:
                for back in range(pc, pc - 0x200, -4):
                    if emitted.read32(back) in (0xD503245F, 0xD503237F):  # bti c or pacibsp
                        return back
    return None


if __name__ == "__main__":
    sys.exit(main())
