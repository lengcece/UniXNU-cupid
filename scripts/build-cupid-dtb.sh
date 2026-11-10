#!/usr/bin/env bash
# Build the cupid firmware device tree (the one appended to boot.img).
#
# Source: LineageOS android_kernel_xiaomi_sm8450-devicetrees (open source DTS)
# plus the kernel's include/dt-bindings and include/uapi headers.
#   waipio.dts              -> base SoC tree
#   cupid-sm8450-pm8008-overlay.dts -> cupid board tree (FTS touch, CS35L41,
#                                      panel, PMIC, reserved memory)
# fdtoverlay merges the overlay onto the base.
#
# Output: build/cupid/cupid.dtb (git-ignored).
#
# Environment overrides (use local checkouts instead of cloning):
#   UNIXNU_CUPID_DTS_DIR        devicetrees checkout
#   UNIXNU_KERNEL_INCLUDE_DIR   kernel include/ checkout
#   CPP                         preprocessor command (default: cpp, else clang -E)
set -euo pipefail

# Mirror this step's output into the diagnostics file the workflow uploads
# (kernel-diag.txt), so a failure here is readable from the artifact.
DIAG=${RUNNER_TEMP:-/tmp}/kernel-diag.txt
mkdir -p "$(dirname "$DIAG")"
exec > >(tee -a "$DIAG") 2>&1

DEVICE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CACHE=${UNIXNU_DTB_CACHE:-$DEVICE/.cache}
DTS=${UNIXNU_CUPID_DTS_DIR:-$CACHE/dts-xiaomi-sm8450}
INC=${UNIXNU_KERNEL_INCLUDE_DIR:-$CACHE/kernel-sm8450/include}
OUT=$DEVICE/build/cupid
WORK=$CACHE/dtb-work

need() { command -v "$1" >/dev/null || { echo "missing tool: $1" >&2; exit 1; }; }
need dtc
need fdtoverlay
need fdtput

if [ -z "${CPP:-}" ]; then
  # macOS `cpp` is a driver shim that mis-parses -x assembler-with-cpp;
  # clang -E is the supported preprocessor on both macOS and Linux builders.
  if command -v clang >/dev/null; then CPP="clang -E"; else CPP=cpp; fi
fi

if [ ! -d "$DTS/qcom" ]; then
  echo "== clone devicetrees"
  mkdir -p "$CACHE"
  rm -rf "$DTS"
  git clone --depth 1 https://github.com/LineageOS/android_kernel_xiaomi_sm8450-devicetrees.git "$DTS"
fi
if [ ! -d "$INC/dt-bindings" ]; then
  echo "== clone kernel include tree"
  mkdir -p "$CACHE"
  rm -rf "${INC%/include}/.include-repo"
  git clone --depth 1 --filter=blob:none --sparse \
    https://github.com/LineageOS/android_kernel_xiaomi_sm8450.git "${INC%/include}/.include-repo"
  git -C "${INC%/include}/.include-repo" sparse-checkout set include
  rm -rf "$INC"
  ln -s "${INC%/include}/.include-repo/include" "$INC"
fi

mkdir -p "$WORK" "$OUT" "$CACHE/dtb-stub/dt-bindings/arm/msm"

# A live FDT pulled from the phone (MT Manager or adb) is the authoritative
# ABL merge; use it verbatim when supplied.
if [ -n "${UNIXNU_CUPID_LIVE_FDT:-}" ]; then
  [ -f "$UNIXNU_CUPID_LIVE_FDT" ] || { echo "live FDT not found: $UNIXNU_CUPID_LIVE_FDT" >&2; exit 1; }
  cp "$UNIXNU_CUPID_LIVE_FDT" "$OUT/cupid.dtb"
  dtc -I dtb -O dts "$OUT/cupid.dtb" > "$WORK/cupid.dts" 2>/dev/null
  grep -q 'Cupid' "$WORK/cupid.dts" || { echo "live FDT is not a cupid tree" >&2; exit 1; }
  echo "cupid.dtb (live): $(stat -c%s "$OUT/cupid.dtb") bytes, sha256 $(sha256sum "$OUT/cupid.dtb" | cut -d' ' -f1)"
  exit 0
fi
# The dt-bindings symlink for this header points outside the sparse checkout.
cat > "$CACHE/dtb-stub/dt-bindings/arm/msm/qcom_dma_heap_dt_constants.h" <<'EOF'
#ifndef QCOM_DMA_HEAP_DT_CONSTANTS_H
#define QCOM_DMA_HEAP_DT_CONSTANTS_H
#define HEAP_TYPE_SECURE_CARVEOUT 0
#define HEAP_TYPE_CARVEOUT 1
#define HEAP_TYPE_CMA 2
#endif
EOF

preprocess() {
  local src=$1 out=$2
  # shellcheck disable=SC2086
  $CPP -nostdinc -undef -P -x assembler-with-cpp \
    -I "$(dirname "$src")" -I "$DTS/qcom" -I "$DTS/qcom/display/display" \
    -I "$INC" -I "$INC/uapi" -I "$INC/uapi/linux" -I "$CACHE/dtb-stub" \
    "$src" -o "$out"
}

echo "== preprocess + compile waipio-v2.dts"
# cupid reports ro.boot.dtb_idx=10 and soc revision 2.2: Waipio v2.
preprocess "$DTS/qcom/waipio-v2.dts" "$WORK/waipio.dts"
dtc -@ -I dts -O dtb -o "$WORK/waipio.dtb" "$WORK/waipio.dts" 2>/dev/null

echo "== preprocess + compile cupid overlay"
preprocess "$DTS/qcom/cupid-sm8450-pm8008-overlay.dts" "$WORK/cupid-overlay.dts"
dtc -@ -I dts -O dtb -o "$WORK/cupid-overlay.dtbo" "$WORK/cupid-overlay.dts" 2>/dev/null

echo "== merge"
fdtoverlay -i "$WORK/waipio.dtb" -o "$OUT/cupid.dtb" "$WORK/cupid-overlay.dtbo"

echo "== root identity"
# fdtoverlay applies fragments but not the overlay root properties; ABL does
# apply them. Set the cupid identity the live tree reports.
fdtput -t s "$OUT/cupid.dtb" / model "Cupid based on Qualcomm Technologies, Inc SM8450"
fdtput -t s "$OUT/cupid.dtb" / compatible "qcom,waipio-mtp" "qcom,waipio" "qcom,mtp"
fdtput -t x "$OUT/cupid.dtb" / qcom,msm-id 0x1c9 0x20000 0x1e2 0x20000 0x1c9 0x10000 0x1e2 0x10000
fdtput -t x "$OUT/cupid.dtb" / qcom,board-id 0x10008 0x0
fdtput -t x "$OUT/cupid.dtb" / qcom,pmic-id 0x2f 0x0 0x0 0x0 0x0 0x0 0x0 0x0
fdtput -t x "$OUT/cupid.dtb" / qcom,pmic-id-size 0x8
fdtput -t x "$OUT/cupid.dtb" / xiaomi,miboard-id 0x2 0x0

echo "== adt /cpus fixup"
# AppleARMCPU::_validateConfiguration() (recovered from the 24A446 payload) rejects
# the Qualcomm /cpus layout: it counts every child of /cpus (cpu-map included),
# reads a 4-byte cpu-id (falling back to reg, which here is 2 cells), and requires
#   cpu-id == interrupts[0],  interrupts[2] == interrupts[0] + 1,
#   function-ipi_dispatch of 3 cells consistent with interrupts.
# So drop cpu-map and give every CPU node the three Apple-shaped properties.
dtc -I dtb -O dts "$OUT/cupid.dtb" > "$WORK/cupid-fixup.dts"
echo "== fixup: cupid-fixup.dts $(wc -l < "$WORK/cupid-fixup.dts") lines"
python3 - "$WORK/cupid-fixup.dts" "$WORK/cupid-fixup-out.dts" <<'PY'
import re, sys

src, dst = sys.argv[1], sys.argv[2]


def block_end(text, open_idx):
    depth = 0
    for i in range(open_idx, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    raise SystemExit("unbalanced braces")


text = open(src, encoding="utf-8", errors="surrogateescape").read()
m = re.search(r"\n([ \t]*)cpus\s*\{", text)
if not m:
    raise SystemExit("no /cpus node found in cupid.dtb")
indent = m.group(1)
ob = text.index("{", m.start())
cb = block_end(text, ob)
body = text[ob + 1:cb]

def drop_child(name):
    """Remove a direct child node by name (AppleARMCPU counts every child of /cpus)."""
    global body
    while True:
        m = re.search(r"\n([ \t]*)" + name + r"\s*\{", body)
        if not m:
            return False
        ob2 = body.index("{", m.start())
        cb2 = block_end(body, ob2)
        semi = body.find(";", cb2)
        body = body[:m.start()] + body[semi + 1:]


# cpu-map and idle-states are topology/power hints XNU never reads from here,
# but AppleARMCPU counts every child of /cpus as a CPU (mCPUCount would become 9).
drop_child("cpu-map")
drop_child("idle-states")

nodes = list(re.finditer(r"cpu@([0-9a-fA-F]+)\s*\{", body))
count = len(nodes)
reg_re = re.compile(r"reg\s*=\s*<([^>]*)>;")

for idx in range(len(nodes) - 1, -1, -1):
    nm = nodes[idx]
    node_open = body.index("{", nm.start())
    node_close = block_end(body, node_open)
    node_span = body[node_open:node_close]
    mreg = reg_re.search(node_span)
    cpu_id = idx
    if mreg:
        cells = [int(x, 0) for x in mreg.group(1).replace(",", " ").split()]
        if cells and cells[-1] % 0x100 == 0 and cells[-1] // 0x100 < 64:
            cpu_id = cells[-1] // 0x100
    inner = body[node_open + 1:node_close]
    if "cpu-id = <" in inner:
        continue
    ins = "\n{n1}cpu-id = <{i}>;\n{n1}interrupts = <{i} 0 {p}>;\n{n1}function-ipi_dispatch = <{i} 0 {p}>;".format(
        n1=indent + "\t\t", i=cpu_id, p=cpu_id + 1)
    body = body[:node_open + 1] + ins + inner + body[node_close:]

if "max_cpus =" in body:
    body = re.sub(r"max_cpus\s*=\s*<[^>]*>\s*;", "max_cpus = <%d>;" % count, body, count=1)
else:
    body = "\n" + indent + "\tmax_cpus = <%d>;\n" % count + body.lstrip("\n")

open(dst, "w", encoding="utf-8", errors="surrogateescape").write(text[:ob + 1] + body + text[cb:])
print("adt /cpus: %d cpus, cpu-map+idle-states dropped, max_cpus=%d" % (count, count))
PY
dtc -I dts -O dtb -o "$OUT/cupid.dtb" "$WORK/cupid-fixup-out.dts"

# Assert the fixup really landed and /cpus holds nothing but cpu@N children.
dtc -I dtb -O dts "$OUT/cupid.dtb" > "$WORK/cupid-fixed.dts"
grep -q 'max_cpus' "$WORK/cupid-fixed.dts" \
  || { echo "adt /cpus fixup did not stick" >&2; exit 1; }
python3 - "$WORK/cupid-fixed.dts" <<'PY2'
import re, sys
# Brace-depth scan: indentation in dtc output is not a stable signal.
text = open(sys.argv[1], encoding="utf-8", errors="surrogateescape").read()
m = re.search(r"\n([ \t]*)cpus\s*\{", text)
assert m, "no /cpus node in fixed dts"
ob = text.index("{", m.start())
kids = []
depth = 0
line_start = ob
for i in range(ob, len(text)):
    ch = text[i]
    if ch == "\n":
        line_start = i + 1
        continue
    if ch in "{}":
        if ch == "{" and depth == 1:
            name = text[line_start:i].strip().split()[0] if text[line_start:i].strip() else ""
            name = name.rstrip("{").strip()
            if name:
                kids.append(name)
        depth += 1 if ch == "{" else -1
        if depth <= 0:
            break
cpus = [k for k in kids if k.startswith("cpu@")]
extra = [k for k in kids if not k.startswith("cpu@")]
print("adt /cpus children: %d (%s)%s" % (
    len(kids), ",".join(sorted(kids)),
    "" if not extra else "  FOREIGN: " + ",".join(extra)))
assert len(cpus) == 8, "expected 8 cpu@ nodes, got %d (%s)" % (len(cpus), ",".join(sorted(kids)))
assert not extra, "/cpus still has non-CPU children: %s" % extra
PY2

echo "== verify"
dtc -I dtb -O dts "$OUT/cupid.dtb" > "$WORK/cupid.dts" 2>/dev/null
for needle in cs35l41 fts@0; do
  grep -q "$needle" "$WORK/cupid.dts" \
    || { echo "missing node: $needle" >&2; exit 1; }
done
echo "cupid.dtb: $(stat -c%s "$OUT/cupid.dtb") bytes, sha256 $(sha256sum "$OUT/cupid.dtb" | cut -d' ' -f1)"
