# First boot attempt — 2026-11-07

Image: `boot.img` sha256
`65cfa2fbe4c396f19ce19f58953c144e56240cd9e1c8fb01d9f5b50423c8f9a8`
(run `42383457434`, commit `50a8c5d`).

```
C:\platform-tools>fastboot boot C:\Users\<user>\Downloads\cupid-unixnu\boot.img
Sending 'boot.img' (181676 KB)                     OKAY [  4.206s]
Booting                                            OKAY [  0.110s]
Finished. Total time: 4.383s

C:\platform-tools>fastboot devices
1cb22738         fastboot
```

Result: black screen, no text, no colour. The device stayed in fastboot, and
`fastboot reboot` returned to PixelOS. So ABL accepted and transferred the
image, but BootShim/UEFI produced no output and did not hand off to XNU.

## What was ruled out

- `fastboot boot` itself works; the bootloader accepts a 181676 KiB image.
- The image header is valid (`ANDROID!` v0, pagesize 4096, ramdisk = payload).
- The payload FAT contains `EFI/UniXNU/unixnu.toml` and
  `Payloads/kernel.macho` (93 MB) + `.adt`.
- `pstore` is empty; `/proc/last_kmsg` is absent, so there is no UEFI/XNU log.
- `xbl_sc_logs` contains only XBL/SBL (ends at `boot_platform_dtb_init`), no
  ABL/UEFI stage.

## Cause found

`cupid`'s stock `uefi.img` is an ELF64 with one `PT_LOAD` at
`vaddr 0xA7000000`, `memsz 0x2A0000`, entry `0xA7000000`; the `uefi`
partition is exactly `0x2A2000` bytes.

The firmware was built for `0xA7100000` + `0x300000` (ingres layout). BootShim
therefore relocated the 3 MiB FD to `0xA7100000..0xA7400000`, one MiB past the
stock window and over memory ABL keeps. Fix: `base = 0xA7000000`,
`size = 0x2A2000`, with the memory map reserving
`0xA7000000..0xA72A2000` and spare to `0xA7600000`.

Rebuild and retest after the uefi-cupid commit that changes
`Resources/Configs/cupid.toml` and the memory map.
