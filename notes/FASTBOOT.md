# First boot on cupid — fastboot procedure

The image is **not flashed**. `fastboot boot` loads it into RAM for one boot; a
power cycle returns to PixelOS.

## Before booting

- Bootloader unlocked. PixelOS is installed, but `ro.boot.flash.locked=1` was
  read under Android, so confirm in bootloader:
  `fastboot getvar unlocked` (expect `unlocked: yes`).
- `boot.img` from the macOS workflow artifact (`cupid-bootimg`).
- Phone at the home screen, USB attached, `adb devices` shows `1cb22738`.

## Commands

```sh
adb reboot bootloader
fastboot devices
fastboot getvar unlocked
fastboot getvar current-slot
fastboot boot boot.img
```

`fastboot boot` returns `OKAY` when the image is accepted, then the phone
reboots into it. Nothing is written to `boot`, `vendor_boot` or `userdata`.

## What should happen

1. ABL decompresses BootShim + `SILICIUM_UEFI.fd`.
2. `cupidPkg` UEFI starts; `SimpleFbDxe` publishes a GOP on the scan-out ABL
   left running, so the UniXNU menu and text should appear on the phone.
3. DarwinBoot loads `Payloads/kernel.macho` + `kernel.macho.adt` from the FAT
   payload and hands off to XNU.

## What we learn

- **Gunyah / PAC gate.** `/sys/hypervisor/type=gunyah` at EL2. If Gunyah sets
  `HCR_EL2.APK`/`API`, the first EL1 PAC key write (`native-pac-keys.patch`)
  traps and XNU dies before console output. A clean entry to `bsd_init` means
  PAC keys are writable at EL1.
- Firmware memory map: whether DarwinBoot hands XNU the intended window from
  `owned_memory_size = 0x12e000000`.
- Whether the v2 DTB (compiled, root identity patched) is accepted, or the live
  FDT must be used instead.

## If it fails

- Black screen / immediate reboot → firmware or DTB problem; try
  `fastboot boot` again, then capture the screen.
- “Failed to load/verify” → image or header problem.
- XNU panic text on screen → photograph it; that is the first useful log.
- Recovery: `fastboot reboot`, or hold Power until the phone restarts.

## Capture

Record in `notes/`:

- `fastboot getvar all` output (no secrets beyond the serial).
- Result of `fastboot boot` (OKAY/FAILED + message).
- Photos of the screen at each stage (menu, DarwinBoot, XNU panic).
- Time to failure or to the menu.

Serial and `ro.boot.cpuid` are already recorded in
`notes/device-2026-11-07.md`; do not post the full `getvar all` publicly.
