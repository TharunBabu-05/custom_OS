# OurOS architecture and provenance

The project implements its AArch64 entry and exception handling in `arch/`,
board support in `boards/`, hardware support in `drivers/`, the VFS and disk
formats in `filesystem/`, process and memory management in `kernel/`,
freestanding helpers and cryptography in `lib/`, networking in `network/`,
and the EL0 runtime and programs in `user/`. The root MIT license covers
original OurOS source. The inventory below records the identified external
materials. [License](LICENSE) · [Source map](README.md#source-map)

| External material | Use and terms |
| --- | --- |
| `bcm2712-rpi-5-b.dtb`, packaged as `PI5.DTB` | Firmware-loaded Raspberry Pi board description. Preserve `COPYING.linux` and satisfy applicable corresponding-source requirements when distributing the DTB. The download manifest pins its upstream revision and hash. [Boot assets](boot/rpi5/assets.json) |
| `FW43455.BIN` and companion radio assets | Radio firmware and configuration loaded into the Broadcom device. Keep the applicable upstream notices, including `LICENCE.broadcom`. [Wi-Fi assets](docs/wifi-firmware.md) |
| `drivers/display/font8x8.h` | Public-domain Basic Latin bitmap font with attribution retained in the file. [Font notice](drivers/display/font8x8.h) |
| GNU tools, QEMU, and OpenSSL | Host build tools, emulator, and TLS or package test fixtures. [Build](scripts/build.ps1) · [TLS tests](docs/tls-live.md) |

## Why the entry header and registers look familiar

The Pi firmware loads a raw AArch64 `Image`. The first word is an AArch64
branch to OurOS startup. The 64-byte header carries the load offset, image
span, flags, and ARM64 magic. This boot path uses the raw image directly and
has no MZ/PE stub. The firmware passes the device tree in `x0`. OurOS uses
architecturally defined exception, MMU, and timer registers to set up EL1.
[Image entry](arch/aarch64/boot/firmware.S) · [AArch64 boot contract](https://www.kernel.org/doc/html/latest/arch/arm64/booting.html)

TCP, TLS and FAT32 have wire or disk formats that interoperating implementations
share. OurOS implements the supported paths in its own source and links the
kernel and bundled user programs as freestanding binaries with no glibc, lwIP,
or OpenSSL runtime dependency. OpenSSL runs on the host to generate public
fixtures and provide an interoperable test peer. [TCP](network/tcp.c) ·
[TLS](lib/tls13_client.c) · [FAT32](filesystem/fat32.c) · [Application build](scripts/build-apps.ps1)
