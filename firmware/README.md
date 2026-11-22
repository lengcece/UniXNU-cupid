# cupid firmware overlay

The firmware is the current Mu-Silicium checkout (`UniXNU/uefi-sheng`). This
directory carries only the board-specific overlay that the CI copies into the
checkout before building:

- `Platforms/Xiaomi/cupidPkg/` — the cupid platform package (`cupid.dsc`,
  `cupid.fdf`, the include lists and the memory/configuration map libraries).
- `Resources/Configs/cupid.toml` — the FD window and boot-image settings.

`board.toml [firmware]` names the copied paths; nothing here is built in
place. See `.github/workflows/macos.yml` for the copy step and
`Platforms/Xiaomi/cupidPkg/README.md` for the platform details.
