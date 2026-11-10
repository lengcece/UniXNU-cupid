#!/usr/bin/env python3
"""Check the AppleARMCPU CPU contract against the device tree XNU is handed.

AppleARMCPU::_validateConfiguration() (AppleARMCPU.cpp:111,
"CPU configuration in device tree is invalid") reads the ADT, and the ADT
UniXNU publishes comes from dts/cupid.dts + soc:dts/sm8450.dtsi. This script
takes the decompiled tree (`dtc -I dtb -O dts`) and asserts the structural
part of that contract, so a mismatch fails here instead of on the phone.

Usage: check-adt-cpus.py <adt.dts>
"""
import re
import sys

CELL_RE = re.compile(r"<([^>]*)>")


def cells(text):
    m = CELL_RE.search(text)
    if not m:
        return []
    out = []
    for tok in m.group(1).split():
        tok = tok.strip(",")
        if tok.startswith("0x"):
            out.append(int(tok, 16))
        else:
            try:
                out.append(int(tok, 10))
            except ValueError:
                out.append(tok)
    return out


def main():
    path = sys.argv[1]
    text = open(path, encoding="utf-8", errors="surrogateescape").read()

    m = re.search(r"\n\s*cpus\s*\{", text)
    if not m:
        sys.exit("no /cpus node in %s" % path)
    start = text.index("{", m.start())
    depth = 0
    end = start
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
    body = text[start + 1:end]

    # Split the cpus body into its direct children.
    children = {}
    depth = 0
    line_start = 0
    i = 0
    n = len(body)
    while i < n:
        ch = body[i]
        if ch == "\n":
            line_start = i + 1
        elif ch == "{":
            if depth == 0:
                pre = body[line_start:i].strip().rstrip("{").strip()
                toks = pre.split()
                name = toks[-1] if toks else ""
                if name.endswith(":") and len(toks) > 1:
                    name = toks[-2]
                # find the matching close brace
                d = 0
                j = i
                while j < n:
                    if body[j] == "{":
                        d += 1
                    elif body[j] == "}":
                        d -= 1
                        if d == 0:
                            break
                    j += 1
                if name:
                    children[name] = body[i:j + 1]
                i = j + 1
                line_start = i
                continue
            depth += 1
        elif ch == "}":
            depth -= 1
        i += 1

    problems = []
    cpu_nodes = {k: v for k, v in children.items() if k.startswith("cpu@")}
    others = [k for k in children if not k.startswith("cpu@")]

    print("adt /cpus children: %d (%s)" % (len(children), ",".join(sorted(children))))
    if others:
        problems.append("/cpus has non-CPU children: %s" % others)

    # contract: max_cpus exists, 4 bytes, non-zero, >= cpu count
    mc = re.search(r"max_cpus\s*=\s*<([^>]*)>", body)
    if not mc:
        problems.append("max_cpus missing on /cpus (Apple requires a 4-byte, non-zero value)")
    else:
        vals = cells(mc.group(0))
        if len(vals) != 1:
            problems.append("max_cpus has %d cells, Apple expects 1 (4 bytes): %s" % (len(vals), vals))
        elif not isinstance(vals[0], int) or vals[0] <= 0:
            problems.append("max_cpus is zero/invalid: %s" % vals)
        elif vals[0] < len(cpu_nodes):
            problems.append("max_cpus (%s) < cpu count (%d)" % (vals[0], len(cpu_nodes)))

    ids = []
    for name, blk in sorted(cpu_nodes.items()):
        cid = re.search(r"cpu-id\s*=\s*<([^>]*)>", blk)
        irq = re.search(r"interrupts\s*=\s*<([^>]*)>", blk)
        ipi = re.search(r"function-ipi_dispatch\s*=\s*<([^>]*)>", blk)
        if not cid:
            problems.append("%s: cpu-id missing (2-cell `reg` is 8 bytes, Apple rejects it)" % name)
            continue
        cvals = cells(cid.group(0))
        if len(cvals) != 1:
            problems.append("%s: cpu-id has %d cells, Apple expects 1" % (name, len(cvals)))
            continue
        cpu_id = cvals[0]
        ids.append(cpu_id)
        if not irq:
            problems.append("%s: interrupts missing" % name)
        else:
            ivals = cells(irq.group(0))
            if len(ivals) != 3:
                problems.append("%s: interrupts has %d cells, Apple expects 3 for max_cpus>=3"
                                % (name, len(ivals)))
            else:
                if ivals[0] != cpu_id:
                    problems.append("%s: interrupts[0]=%s != cpu-id=%s" % (name, ivals[0], cpu_id))
                if ivals[2] != ivals[0] + 1:
                    problems.append("%s: interrupts[2]=%s != interrupts[0]+1" % (name, ivals[2]))
        if not ipi:
            problems.append("%s: function-ipi_dispatch missing" % name)
        else:
            pvals = cells(ipi.group(0))
            if len(pvals) != 3:
                problems.append("%s: function-ipi_dispatch has %d cells, expects 3"
                                % (name, len(pvals)))
            elif irq and cells(irq.group(0)) and pvals != cells(irq.group(0)):
                problems.append("%s: function-ipi_dispatch %s != interrupts %s"
                                % (name, pvals, cells(irq.group(0))))

    if len(set(ids)) != len(ids):
        problems.append("duplicate cpu-id values: %s" % ids)
    if mc and cells(mc.group(0)):
        limit = cells(mc.group(0))[0]
        if isinstance(limit, int):
            for cid in ids:
                if isinstance(cid, int) and cid >= limit:
                    problems.append("cpu-id %s >= max_cpus %s" % (cid, limit))

    if problems:
        print("ADT /cpus contract FAILED:")
        for p in problems:
            print("  - " + p)
        sys.exit(1)
    print("ADT /cpus contract OK: %d cpus, max_cpus=%s, cpu-id 0..%d"
          % (len(cpu_nodes), cells(mc.group(0))[0], len(cpu_nodes) - 1))


if __name__ == "__main__":
    main()
