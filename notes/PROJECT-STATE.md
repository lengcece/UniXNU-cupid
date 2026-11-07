# PROJECT STATE — UniXNU cupid (read this first)

Last updated: 2026-11-07, after build `42408717308` was dispatched.

## Goal

Fully port **UniXNU** to the **Xiaomi 12 (`cupid`, Qualcomm SM8450, waipio v2)**
booting **iOS 27 (arm64e, iPhone-flavoured profile)**, not iPadOS, with all
fixes. Treatment of this file: authoritative session memory. Update it before
ending a turn that changes state. Read it at the start of every turn.

## Hard rules

1. **No personal name in any file, commit, or output.** Use `<user>` in docs and
   `%USERPROFILE%` in Windows commands.
2. **The agent never runs fastboot.** All fastboot commands are executed by the
   owner. The agent stages images and prints commands only. `adb` is allowed.
3. Storage budget 30 GiB. `/tmp` is a 3.8 GiB tmpfs; delete videos/frames after
   use (`/home` has plenty).
4. GitHub account `obaosp`; all repos public; macOS Actions free for public.
5. Apple artifacts (kernelcache, IPSW members, seeds) never go into a repo.

## Repositories

| Repo | Purpose | Head |
| --- | --- | --- |
| `github.com/obaosp/UniXNU-cupid` | device repo | `a21ef96` |
| `github.com/obaosp/UniXNU-sm8450-common` | SM8450 SoC layer | `156e460` |
| `github.com/obaosp/uefi-cupid` | fork of uefi-sheng, `sheng` branch | `2629889` |

Workspace layout: `upstream/` reference clones, `port/` the two repos above,
`build/unixnu-ios` scratch UniXNU checkout, `notes/` supporting notes.

## Build pipeline (what works)

Device repo `.github/workflows/macos.yml`, macOS 26 arm64:

1. checkout device, `UniXNU/UniXNU` (main, `b6de3eb`), `obaosp/uefi-cupid`
   (default `sheng`) + firmware submodules.
2. Repair stale gitlink: `drivers/ArmCpu` pinned to `2e11cda` no longer exists;
   workflow sets `93a5030c5d983d0ef90f0fca93879d6e36ad1ba8`.
3. Apply `patches/unixnu/*.patch`.
4. `scripts/install-os-profile.sh` copies `profiles/os/ios-27.0.1-24A446`.
5. DTB first (`scripts/build-cupid-dtb.sh`, `clang -E`, dtc, fdtoverlay,
   waipio-v2 + cupid overlay, root identity patched; sha `7604f605`).
6. kernel + prelink (~10–17 min), package, firmware, then DarwinBoot submodule
   patch, payload, boot image. Total 17–23 min per run.
7. Artifacts: `cupid-bootimg` (`boot.img`, `payload.img`).

Patches carried by the device repo:

| Patch | Purpose |
| --- | --- |
| `patches/unixnu/0001-…nm-headers` | skip non-hex `llvm-nm` header lines (payload crashed on `int('Symbols')`) |
| `patches/unixnu/0002-…find-fat-in-initrd` | scan initrd for FAT (512-byte walk + `CUPIDESP` label fallback) |
| `patches/darwinboot/0001-…log-window-failure` | DEBUG the `NO WINDOW`/`FREE` run sizes; applied inside the submodule after the kernel step |

## OS profile (iOS 27, phone)

`profiles/os/ios-27.0.1-24A446` (vendored; UniXNU main has only iPadOS):
iPhone17,2 / D94AP, IPSW `iPhone17,2_27.0.1_24A446` (iPhone 16 Pro Max),
kernelcache sha `fa07057b…`, AMFI early init `0xfffffff00921000c`, root UUID
`E3F4AED0-4454-4D5B-833C-E27E99C4F67B`, SystemOS `8B599D21-…`, ABI classes.
UniXNU main ships no iOS 27 profile; this one came from the published
UniXNU-ishtar patches 0001/0002/0005.

## Measured cupid facts (device, 2026-11-07)

- `soc_id=457`, revision 2.2 → **waipio v2**; `dtb_idx=10`.
- 8 cores; `paca pacg` on every core (FEAT_PAuth present).
- **Gunyah** at EL2 (`/sys/hypervisor/type=gunyah`, api 1, variant 81);
  kernel cmdline `kvm-arm.mode=protected`, `CONFIG_ARM64_PTR_AUTH=y`.
  Whether Gunyah traps `HCR_EL2.APK/API` is still untested.
- UFS `1d84000.ufshc` (mem), ICE `0x1d88000`/`0x1d90000`, PHY `0x1d87000`.
- userdata `sda38`: start512 `25362432`, size512 `466862040` →
  LBA `3170304..61528058` at 4096 bytes.
- `/proc/iomem` high banks: `0x800000000+0x39700000`,
  `0x839D00000+0x1E00000`, `0x840000000+0x140000000` (reserved tail from
  `0x96EF68000`).
- Live FDT `3d669081…` (773371 bytes, model “Cupid…”), pulled via MT Manager to
  `Download/cupid-fdt/fdt`; `/sys/firmware/fdt` is SELinux-blocked.
- Stock `uefi.img` is an **ELF64**: `PT_LOAD` vaddr `0xA7000000`, memsz
  `0x2A0000`, entry `0xA7000000`; `uefi` partition `0x2A2000`.
- Stock boot image header v4; `msm_drm.dsi_display0=…l3_42_02_0a_dsc_cmd`.
- Serial `1cb22738`, cpuid `0x0000043252a25a21`. PixelOS + KernelSU root; adb
  reachable from WSL via `/mnt/c/platform-tools/adb.exe`.

## Board/config decisions

- `board.os_profile = ios-27.0.1-24A446`
- components: ArmDTPlatform, ArmCpu, XnuPrelink, DarwinBoot
- kernel args: `-v serial=3 debug=0x14e kt_fixed_zones=120 -no_slto_panic
  use_contiguous_hint=0`
- `[userdata] 3170304..61528058`, `lba_bytes 4096`
- `[adt.defaults.u32] vmm-present=1`, `sleep-disabled=1`
- `[payload.adt] chip_id=457`, `dram_size=0x80000000`,
  `owned_memory_size` **currently `0x80000000`** (was `0x12e000000`)
- `[payload] size_mib=176`, volume `CUPIDESP`, panic_screen true
- uefi-cupid `Resources/Configs/cupid.toml`: FD base `0xA7000000`, size
  `0x2A2000`, **header_version 4**, pagesize 4096
- cupidPkg: `WaipioPkg`, `CUPID_VENDOR_HW=0`, SimpleFbDxe, console slowed to
  250000 µs/line, `PcdDebugPrintErrorLevel=0x80000000` (errors only)
- MemoryMapLib: ingress map + high DRAM Conv banks (`0x800000000`,
  `0x839D00000`, `0x840000000+0x12EF68000`), tail reserved.

## Device boot history (all via owner-run `fastboot boot`)

1. **v0 header** (`65cfa2fb…`): `fastboot boot` OKAY, black screen, stayed in
   fastboot. Cause: v0 `kernel_addr=0x8000`; v4 needed.
2. **v4 repack** (`72814ad9…`): UEFI ran — Project Silicium logo, UniXNU menu.
   Payload missing: `failed to locate SimpleFileSystem 964E5B22-…`.
   Cause: initrd `0x8C373000` size `0xBC8BE00` = LZ4 vendor ramdisk prefix
   (13,155,840) + our 176 MiB payload; 512-byte scan missed the FAT.
3. **v4 with label scan** (`cd7507f4…`): payload found, menu lists **iOS 27**;
   selecting it returns **“Out of resources”**. DarwinBoot `AllocateWindow`
   cannot find one conventional run of the requested 4.69 GiB window.
   Menu input (power/volume) does nothing: no `ConIn` driver.
4. **Current attempt** (`42408717308`, device `a21ef96`, firmware `2629889`
   with high DRAM + `owned_memory_size=0x80000000` + DarwinBoot console logs):
   build in progress at the time of this file.

## Immediate next step

When `42408717308` completes: download `cupid-bootimg`, verify header v4 +
Shim `text_offset 0xa7000000`, stage to
`%USERPROFILE%\Downloads\cupid-unixnu\boot-v4-win2g.img`, give the owner the
`fastboot boot` command, and ask for a video/photo.

Expected console on failure:

```
DARWINBOOT: NO WINDOW 0x80000000 BIG RUN 0x… @ 0x…
DARWINBOOT: FREE 0x… + 0x…
```

Decide from `BIG RUN`:

- If `BIG RUN >= 0x80000000`, allocation should succeed; if not, the high
  banks are not in UEFI’s conventional map — fix `MemoryMapLib`/page tables or
  lower the window.
- If a window is allocated, DarwinBoot loads `kernel.macho` + ADT and XNU
  starts; the **Gunyah/PAC gate** is the next result. A PAC trap panic is a
  valid, critical finding.

## After XNU starts (roadmap)

Storage (SM8450 UFS component in `package/Sm8450Ufs`, host-tested registers +
UFSHCI command path; needs kext transport, UCD/PRDT, READ(10), SMMU), display
(SimpleFbDxe works; XNU display needs SDE nodes and d94ap ABI), input (FTS over
SPI, no ConIn), GPU (no Adreno 730 component), network (WCN6750/6855 TBD),
audio (CS35L41/ADSP), power (PM8550/PMK8550).

## Environment gotchas

- `python3` 3.14; no gcc/clang locally; dtc + libfdt extracted under
  `/tmp/dtc-local` and `/tmp/fdt-lib`; pcpp installed for local DTS checks.
- `gh` lives at `~/.local/bin/gh`; export PATH in every shell.
- `/tmp` fills with 80 MB videos: copy, extract, then delete.
- `unixnu` resolver: `/home/rohith/projects/unixnu-cupid/build/unixnu-ios`,
  device via `-D ../../port/UniXNU-cupid`.
- Porting record checker: `python3 scripts/porting-record.py check --device
  ../../port/UniXNU-cupid`; every changed path needs its owning area’s findings
  or evidence updated or CI fails.
- macOS `cpp` breaks on `-x assembler-with-cpp`; use `clang -E`.
- `mkbootimg` v4 page size is fixed at 4096 (no page-size field); parse v4
  headers accordingly.
- `git apply` of DarwinBoot patches must run inside the submodule after fetch.

## Context protocol (compaction-proof)

- This file and `NEXT.md` are the authoritative memory. Read both first in any
  new or compacted session; update them before ending a turn that changes
  state (commits, hashes, run results, device results).
- Delegate routine context-heavy work to subagents and keep only their short
  reports: watching a build, downloading/verifying/staging artifacts, reading
  videos or long logs, repo audits, record-check fixes.
- Subagents must never run adb or fastboot, and must not print personal names.
- Build artifact staging subagent (session `ses_e485c202affeSo32gh7YO3vzIA`)
  is handling run `42408717308` -> `boot-v4-win2g.img`; it writes
  `notes/BUILD-42408717308.md`.
- If a subagent reports a new fact, fold it into this file immediately.
