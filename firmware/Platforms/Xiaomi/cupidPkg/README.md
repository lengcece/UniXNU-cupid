# cupid firmware platform

UEFI firmware overlay for the **Xiaomi 12** (`cupid`, Qualcomm **SM8450**).
It is copied into the current Mu-Silicium checkout (`UniXNU/uefi-sheng`) by
the CI; that checkout supplies UniXNU, DarwinBoot, the payload FAT driver and
the EDK II packages. The overlay adds only the cupid platform package and its
boot-image config.

## State

| Item | Value |
| --- | --- |
| SoC package | `Silicon/Qualcomm/WaipioPkg` (SM8450, 8 cores, GIC 0x17100000) |
| Vendor hardware | **off** (`CUPID_VENDOR_HW = 0`), no `Binaries/cupid` needed |
| Display | `SimpleFbDxe` on ABL's scan-out, 1080x2400 |
| FD window | `0xA7000000` + `0x2A2000` (measured stock `uefi.img`) |
| Boot image header | v4, 4096-byte pages |

The minimal configuration is deliberate: it produces a firmware volume with the
UniXNU menu, the XNU loader, the payload FAT driver and the shell without any
stock vendor module. `Binaries/cupid` is only required for
`CUPID_VENDOR_HW = 1` (stock clock, RPMh, UFS, USB, charger and display
drivers) and for the `SecParti.cfg` raw file.

## Memory map provenance

`Library/MemoryMapLib/MemoryMapLib.c` and
`Library/ConfigurationMapLib/ConfigurationMapLib.c` are copied from the
**ingres** platform, the other Xiaomi SM8450 (waipio) board in this tree:
same DRAM base `0x80000000`, same FD window and the same SoC carveouts
(HYP, SMEM, PIL, TZApps, DXE heap). `ingres` is 1080x2400 as well.

Confirm both tables against cupid's own map from the stock `uefiplat.cfg`
(HyperOS `3.0.4.0.VLCMIXM`, `uefi.img`) when a live dump is available.

## Build

The device repository drives the build. Copy this overlay into the firmware
checkout, then build with `--uefi ../uefi-sheng`:

```sh
cp -R firmware/Platforms/Xiaomi/cupidPkg ../uefi-sheng/Platforms/Xiaomi/cupidPkg
install -m 0644 firmware/Resources/Configs/cupid.toml \
  ../uefi-sheng/Resources/Configs/cupid.toml

./unixnu defconfig cupid_ios27_defconfig -D .
./unixnu fetch -D .
./unixnu build firmware -D . --uefi ../uefi-sheng
```

The expected output is
`Build/cupidPkg/{DEBUG,RELEASE}_CLANGPDB/FV/SILICIUM_UEFI.fd`.
The checkout needs its submodules (`Common/Mu`, `Common/Mu_OEM_Sample`,
`Mu_Basecore`, `Silicium-ACPI`, the OpenSSL submodule, and `Binaries` when
vendor hardware is enabled).

## Not yet done

- Import cupid's stock UEFI modules into `Binaries/cupid`
  (`QcomPkg/Drivers/*`, `RawFiles/SecParti.cfg`, panel XMLs).
- Verify the memory map and FD base on the phone.
- Vendor display chain (`USE_CUSTOM_DISPLAY_DRIVER = 1`) with the L3 panel XML.
- ACPICA/ACPI tables for cupid (the minimal build omits them).
