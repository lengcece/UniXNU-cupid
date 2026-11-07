# First UEFI boot on cupid — 2026-11-07

Image: `boot-v4.img`, 186040320 bytes,
sha256 `72814ad98e0a1b813b6aa6f7b3db5845e5e54e7b2dd33e354572c51cc8257a52`.
It is the run `42391718599` kernel + payload repacked locally with
`mkbootimg.py --header_version 4 --pagesize 4096` (no rebuild of the FD).

## What happened

1. `fastboot boot` sent 181676 KiB and returned `OKAY`.
2. BootShim ran, the UEFI firmware started, and the console printed
   `loading drivers` and `memory allocation`.
3. The Project Silicium logo appeared, then the **UniXNU UEFI menu** with four
   options and the countdown `booting return to firmware in 9s`.
4. The menu found **no payload entries**, so it returned to firmware.
5. BDS reported:
   ```
   failed to locate any 964E5B22-6459-11D2-8E39-00A0C969723B based files - code=Not Found
   USB boot desired, but no USB devices found on first attempt
   Error: Image at 000F3C1000 start failed: Not Found
   Image Return Status = Not Found
   ```
   `964E5B22-6459-11D2-8E39-00A0C969723B` is
   `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL`: there is no filesystem at all.
6. `failed to find operating system`, then the phone powered off; back to
   PixelOS with `fastboot reboot`.

## Conclusions

- **The firmware is real on cupid**: BootShim, SEC/PEI/DXE, SimpleFbDxe, the
  UniXNU application and its menu all run. The display path works.
- **Header v4 is required.** The v0 image (`kernel_addr 0x8000`) left ABL in
  fastboot; v4 makes ABL use BootShim's arm64 `text_offset` (`0xA7000000`).
- The blocker is narrower: `UniXNURamDiskDxe` published no block device, so
  `DiskIoDxe`/FAT had nothing to mount and UniXNU had no `unixnu.toml` and no
  `Payloads/kernel.macho`.

## Next measurement

The firmware console was slowed to 0.25 s per line with `DEBUG_ERROR` only, so
a phone video can capture the `UNIXNURAMDISK:` diagnostics:
`no entry argument HOB`, `x0 ... is not a device tree`, `no /chosen`,
`no initrd in /chosen`, or `empty initrd`. That decides whether ABL failed to
write `/chosen/linux,initrd-*`, wrote the wrong range, or the HOB is missing.
