# NEXT — active queue

1. Build `42408717308` (device `a21ef96`, uefi-cupid `2629889`):
   high DRAM as conventional + `owned_memory_size=0x80000000` + DarwinBoot
   console diagnostics. On completion:
   - `gh run download 42408717308 --repo obaosp/UniXNU-cupid --name cupid-bootimg`
   - verify v4 header, Shim `text_offset 0xa7000000`, ramdisk 184549376
   - stage as `boot-v4-win2g.img` under `%USERPROFILE%\Downloads\cupid-unixnu\`
   - give the owner `fastboot boot`; ask for video/photo.
2. Read the console:
   - `DARWINBOOT: NO WINDOW 0x80000000 BIG RUN 0x… @ 0x…` + `FREE` lines.
   - If BIG RUN ≥ 0x80000000 but allocation still fails, the high banks are
     not in UEFI's conventional map: fix page tables/memory map.
   - Otherwise set `owned_memory_size` to a run that exists, rebuild.
3. On a successful window: DarwinBoot loads kernel.macho + ADT → XNU.
   Capture the Gunyah/PAC result (panic = key finding).
4. Then: storage (SM8450 UFS kext), display (SDE + d94ap ABI), input (FTS),
   GPU (Adreno 730), network, audio, power.

## Owner actions pending

- Run all `fastboot` commands; the agent must not.
- Provide video/photos of the UEFI console when asked.
- Device remains on PixelOS with KernelSU; adb root available.
