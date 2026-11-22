# Phase 11 HDMI framebuffer and text mirror

OurOS reads the enabled `simple-framebuffer` node under `/chosen`
in the live firmware device tree. It checks the pixel format,
dimensions, stride and physical range, then reserves the
page-rounded buffer before the page allocator starts. The MMU
maps it as writable, non-executable Normal-NC memory, including
the Pi 5 case where firmware places scanout in the gap at the top
of the first GiB. The HDMI commands require the verified MMU
mapping. [Simple framebuffer binding](https://www.kernel.org/doc/Documentation/devicetree/bindings/display/simple-framebuffer.yaml) · [Pi 5 firmware notes](https://github.com/raspberrypi/rpi-eeprom/blob/master/firmware-2712/release-notes.md)

The driver renders RGB565 and common 32-bit RGB/BGR modes with
the public-domain Basic Latin font8x8. The text mirror wraps and
scrolls within an 80-column by 25-row maximum viewport. PuTTY
continues to provide input. USB keyboard support is a separate
milestone. [Font source](https://github.com/dhepper/font8x8/blob/master/font8x8_basic.h)

## Raspberry Pi verification

In PuTTY, type `exit` if you are at `user:/$`, then `halt` at
`>`. After the Pi reports that the CPU halted, power it off and
move the pendrive to Windows. Use the letter Windows shows for
`OUROSBOOT`. Run these commands in the repository.

```powershell
$boot = 'D:\'
$zip = 'build/rpi5-header/OurOS-Pi5-header-phase11-hdmi-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip -Apply
```

The preview from the Phase 10 package build selects
`Replace OUROS.IMG`. Safely eject the pendrive. Connect a powered
monitor or TV to the Pi's `HDMI0` port, select that display input,
then boot with the pendrive installed. [Raspberry Pi display connection guide](https://www.raspberrypi.com/documentation/computers/getting-started.html#displays)

In PuTTY, type `exit` at `user:/$` to reach `>`. Run these two commands
first. Look at the HDMI display before entering `fbon`.

```text
fbinfo
fbtest
```

`fbinfo` prints the firmware dimensions, format, stride and address.
`fbtest` draws four vertical bars in red, green, blue and white. If
the monitor says the MMU mapping is required, run `mmuinit` and then
`fbtest` again. If it says there is no enabled firmware framebuffer,
capture `fbinfo` and `memmap` with HDMI0 connected at power-on.

Now run the console mirror test.

```text
fbon
echo HDMI mirror check
appstart netdemo
fboff
echo UART check
```

The first echo and the saved `netdemo` greeting should appear on
HDMI and in PuTTY. The last echo should appear in PuTTY. The mirror
starts disabled on the next boot.

## Local verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test -KeepBuildFiles
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-framebuffer.ps1
```

The native tests check the Pi scanout gap, rejection of conflicting
memory, RGB565 and both 32-bit channel orders, padded strides, text
editing, wrapping and scrolling. The Cortex-A53 and Cortex-A76 QEMU
tests capture guest memory after the test pattern and after mirroring,
with guards around the framebuffer. The MMU and package-download
regressions passed on both QEMU profiles. The updater passed preview,
apply and rollback on scratch files. Raspberry Pi HDMI scanout remains
pending.

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-phase11-hdmi-boot-update.zip` | `b970ec958b059012c86197ae6bcf7ca2ba759f4424a77dc92d90b0a277137750` |
| `OUROS.IMG` | `716369ade829fe3daa87ef58a11e54f58c0105fa0e3de27653b7b6bfa9576da1` |
