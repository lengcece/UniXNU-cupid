# Device agent guide — cupid

This repository owns one board: Xiaomi 12 (`cupid`, SM8450). Shared platform
description lives in `UniXNU-sm8450-common`; drivers live in component
repositories; OS profiles live in this repository until upstream ships an
iOS 27 one.

## Non-negotiables

- **iPhone, not iPad.** The target is iOS 27 with an iPhone17,2 / D94AP
  identity. Do not switch `board.os_profile` to `ipados-27.0.1-24A446`: the
  build may succeed, but product idiom, display and userland assumptions are
  wrong for a phone.
- **Never guess a hardware value.** Cite mainline, vendor DTS or a measurement
  in `porting.toml`. SM8450 differs from SM8550 (UFS PHY `0x1d87000`, GMU PDC
  `0xb290000`); copying SM8550 values is a bug.
- **Do not point Adreno 740 at an Adreno 730**, and do not enable
  `CONFIG_XNU_NATIVE_ADRENO` until an Adreno730 component exists.
- **Do not reuse ShengUfs.** It matches `qcom,sm8550-ufshc` and hardcodes
  SM8550 GCC offsets.
- **Apple inputs are never committed.** Kernelcache, IPSW members, trust caches
  and seeds stay out of Git and CI artifacts.

## Read first

| Task | Reading |
| --- | --- |
| Every change | https://unixnu.com/porting/ , this `porting.toml` |
| arm64e / PAC | https://unixnu.com/porting/arm64e |
| Board config | https://unixnu.com/porting/device-repository |
| Device tree | https://unixnu.com/porting/device-tree |
| Storage | https://unixnu.com/porting/ufs |
| Display / input / GPU | https://unixnu.com/porting/display , /input , /gpu |
| Images | https://unixnu.com/building |
| Bring-up | https://unixnu.com/porting/bring-up |

## Workflow

1. Install the profile: `scripts/install-os-profile.sh ../UniXNU`.
2. Resolve: `./unixnu defconfig cupid_ios27_defconfig -D ../UniXNU-cupid`,
   `./unixnu fetch -D ../UniXNU-cupid`, `./unixnu status --json -D ../UniXNU-cupid`.
3. Kernel: `./unixnu build kernel -D ../UniXNU-cupid` (macOS runner or a Mac
   with Xcode), or rely on `.github/workflows/macos.yml`.
4. Update the owning `[[area]]` in `porting.toml` (findings or evidence) with
   every code change, then run
   `python3 scripts/porting-record.py check --device ../UniXNU-cupid`.
5. Record device results separately, with the command, full revision and an
   accessible log. `planned` is honest; do not inflate status.

## Device gates

Before any kernel work is trusted, the phone must pass the arm64e requirements:
PAuth keys writable at EL1, PAC instructions not trapped at EL0/EL1, native
4 KiB stage 1. Record each result in the `cpu` and `memory` areas.
