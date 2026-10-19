# ARMXBOX

[Download ARMXBOX from GitHub Releases](https://github.com/niilo/ARMXBOX/releases/latest)

ARMXBOX is an original Xbox emulator for Android, based on xemu.

> [!IMPORTANT]
> ARMXBOX is resource-heavy. A device with **8 GB RAM or more is recommended**.
> Lower-end devices may run poorly or fail to run games correctly.
> **Vulkan support is required.**

## Builds and versions

- **ARMXBOX** is the signed release app (green Xbox-style icon).
- **ARMXBOX Dev** is the separate Debug/Profile app (red Xbox-style icon with DEV badge).
- Settings and About show the installed version, variant and build number.
- APK filenames include the version, variant and build host. Desktop executables are `armxbox` / `armxbox.exe`, and the macOS bundle is `ARMXBOX.app`.
- Android package IDs, signing keys, existing settings and storage paths remain compatible with earlier X1 BOX builds. Dev and release retain separate app data.

## Project Origin

- This project is forked from: https://github.com/xemu-project/xemu
- Upstream xemu project: https://xemu.app

## What It Does

ARMXBOX packages xemu for Android with an Android-first flow and launcher UI.

## Key Features

- Setup wizard to configure required Xbox files
- Game library browser with list and cover-grid views
- Optional online box-art lookup in the game library
- On-screen virtual Xbox controller for touch devices
- Automatic hide/show of on-screen controls when a physical controller is connected
- Support for physical USB/Bluetooth game controllers
- ARM64 (`arm64-v8a`) Android target

## Device Requirements

- Android 10+ (API 29 or newer)
- 64-bit ARM device (`arm64-v8a`)
- Vulkan-capable GPU/device
- Recommended: 8 GB RAM or higher for better stability and performance

## Setup Requirements (User Files)

To use the emulator, you must provide your own legally obtained original Xbox files, such as:

- MCPX Boot ROM
- Flash ROM / BIOS
- Xbox hard disk image
- Game files/images from your own discs

ARMXBOX does not include copyrighted BIOS or game content.

## License

This repository inherits xemu/QEMU licensing:

- Emulator code is primarily under **GNU GPL v2**
- Some components are under other GPLv2-compatible licenses (for example BSD/MIT in parts of TCG)

See these files for details:

- `LICENSE`
- `COPYING`
- `COPYING.LIB`

## Notes

- ARMXBOX is not affiliated with Microsoft.
- Compatibility and performance vary by device and game.
