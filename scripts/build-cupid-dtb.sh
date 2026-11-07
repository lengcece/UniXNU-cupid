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
  if command -v cpp >/dev/null; then CPP=cpp; else CPP="clang -E"; fi
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

echo "== verify"
dtc -I dtb -O dts "$OUT/cupid.dtb" > "$WORK/cupid.dts" 2>/dev/null
for needle in cs35l41 fts@0; do
  grep -q "$needle" "$WORK/cupid.dts" \
    || { echo "missing node: $needle" >&2; exit 1; }
done
echo "cupid.dtb: $(stat -c%s "$OUT/cupid.dtb") bytes, sha256 $(sha256sum "$OUT/cupid.dtb" | cut -d' ' -f1)"
