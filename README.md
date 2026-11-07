# UniXNU cupid — iOS 27 on the Xiaomi 12

Bring-up of **iOS 27.0.1 (24A446, arm64e)** on the **Xiaomi 12** (`cupid`,
Qualcomm **SM8450**), presented to iOS as an **iPhone 16 Pro Max**
(`iPhone17,2`, `D94AP`).

This is a phone port, so it uses an **iPhone-flavoured** OS profile. It does
**not** boot the iPadOS 27 profile that ships in UniXNU main: same build and XNU
source, different IPSW, original kexts and product identity.

## Hardware

| Block | cupid |
| --- | --- |
| SoC | SM8450 (Snapdragon 8 Gen 1), 8 cores: 4x A510, 3x A710, 1x X2 |
| RAM | 8 / 12 GiB LPDDR5 |
| Storage | UFS 3.1; UFS HC `0x1d84000`, QMP PHY `0x1d87000` |
| Display | 1080x2400, DSI command mode + DSC, 60/90/120 Hz, cont-splash `0xb8000000` |
| Touch | STMicroelectronics FTS over QUPv3 SE4 SPI, IRQ GPIO 21, reset GPIO 20 |
| GPU | Adreno 730 `0x3d00000`, GMU 730.1 `0x3d6a000`, SMMU `0x3da0000` |
| PMIC | PM8550 / PMK8550 |
| Identity | iPhone17,2 / D94AP, `qcom,msm-id = <457>` |

Sources: mainline Linux `sm8450.dtsi` at
`f1586048fb1dabb91c82fbe11557e505587973d3`; LineageOS
`android_kernel_xiaomi_sm8450-devicetrees` (`cupid-sm8450.dtsi`,
`waipio-sde-display.dtsi`, `dsi-panel-l3-42-02-0a-dsc-cmd.dtsi`).

## Structure

| Path | Contents |
| --- | --- |
| `board.toml` | Format 2 board: SM8450 source, iOS 27 profile, stage-0 components |
| `profiles/os/ios-27.0.1-24A446/` | The iPhone17,2 iOS 27 profile UniXNU main does not ship |
| `configs/cupid_ios27_defconfig` | Stage-0 kernel configuration |
| `dts/cupid.dts` | Memory, splash, CPU clusters; peripherals as they are enabled |
| `feeds.toml` | Pinned [`UniXNU-sm8450-common`](https://github.com/obaosp/UniXNU-sm8450-common) source |
| `porting.toml` | Per-area inputs, findings, limits and next action |
| `scripts/install-os-profile.sh` | Copies the iOS 27 profile into a UniXNU checkout |
| `scripts/package.sh` | Standard UniXNU packaging script (do not edit) |

## Why a shipped profile

UniXNU resolves `profiles/os/<name>` from its own checkout and main has only
`ipados-17.7.11`, `ipados-26.5.2` and `ipados-27.0.1`. The iOS 27 profile here is
reconstructed from the published UniXNU-ishtar patches (0001/0002/0005):
iPhone17,2 IPSW pins, the masked-code-verified AMFI early-init address
`0xfffffff00921000c`, the iPhone prelink roots (no `IOHIDEventDriver` bundle),
OS/SystemOS image digests and volume UUIDs, and `abi/original-classes.json`.
`scripts/install-os-profile.sh` installs it before `defconfig`.

## Build (host)

```sh
git clone https://github.com/UniXNU/UniXNU ../UniXNU
scripts/install-os-profile.sh ../UniXNU
cd ../UniXNU
./unixnu defconfig cupid_ios27_defconfig -D ../UniXNU-cupid
./unixnu fetch -D ../UniXNU-cupid
./unixnu status --json -D ../UniXNU-cupid
./unixnu build kernel -D ../UniXNU-cupid      # Xcode + iphoneos SDK
```

Full images need `uefi-cupid` (not published yet), the platform seed
`UNIXNU_UNIQUE_CHIP_ID` and a Data seed:

```sh
scripts/package.sh --os 27 --uefi ../uefi-cupid
```

## CI

`.github/workflows/macos.yml` runs resolve + `build kernel` + prelink on a
`macos-26` runner and uploads text logs. It needs no firmware and no secrets.
`.github/workflows/package.yml` is the full image build and fails fast with an
explanation until `uefi-cupid` exists.

## Status

| Area | State |
| --- | --- |
| Configuration, CPU/interrupts | `implemented` (host) |
| Kernel build + prelink | CI job in this repository |
| Firmware, storage, display, input, GPU, network, audio, power | `planned` |

Device-side work is blocked on the phone: the build host has no `adb`,
`fastboot` or USB bus. The first device gates are the arm64e PAC requirement
checks from <https://unixnu.com/porting/arm64e> (PAuth keys writable at EL1, PAC
instructions not trapped, native 4 KiB stage 1).

## Documentation

- [Porting](https://unixnu.com/porting/) · [arm64e profiles](https://unixnu.com/porting/arm64e)
- [Device repository](https://unixnu.com/porting/device-repository) · [Records](https://unixnu.com/porting/records)
- [Building](https://unixnu.com/building) · [Bring-up](https://unixnu.com/porting/bring-up)
- [UFS](https://unixnu.com/porting/ufs) · [Display](https://unixnu.com/porting/display) · [Input](https://unixnu.com/porting/input)

Released under the BSD-2-Clause-Patent License.
