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
scrolls within an 80-column by 25-row maximum viewport.
[Font source](https://github.com/dhepper/font8x8/blob/master/font8x8_basic.h)

On a Pi 5 USB boot with a usable firmware framebuffer and
`ouros.startup=shell`, startup enables the framebuffer and scans the
storage controller's spare USB2 root ports for a boot-protocol keyboard.
Plug the keyboard directly into a spare port on the same controller as the
boot drive. `usbports` shows both controllers over UART for initial setup.
The keyboard uses a US ASCII layout with editing keys, Shift, Caps Lock,
and control letters. An unplugged keyboard is rediscovered when it returns.
UART is also available. The monitor supports `usbkbd`, `usbkbd auto`,
`usbkbd <port>`, `usbkbd off`, and `fbon` for manual setup.

## Raspberry Pi verification

In PuTTY, type `exit` if you are at `user:/$`, then `halt` at
`>`. After the Pi reports that the CPU halted, power it off and
move the pendrive to Windows. Connect one `OUROSBOOT` drive and run these
commands in the repository. Review the preview before running `-Apply`.

```powershell
$zip = 'build/rpi5-header/OurOS-Pi5-header-smp-local-console-boot-update.zip'
$volumes = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.VolumeLabel -eq 'OUROSBOOT' })
if ($volumes.Count -ne 1) { throw 'Connect exactly one OUROSBOOT drive.' }
$boot = $volumes[0].Name
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip -Apply
```

Safely eject the pendrive. Connect a powered monitor or TV to `HDMI0`,
select that display input, connect the boot drive and keyboard, and power
on the Pi. At the shell prompt on HDMI, enter these commands on the USB
keyboard. Disconnect and reconnect it before typing the second command.
[Raspberry Pi display connection guide](https://www.raspberrypi.com/documentation/computers/getting-started.html#displays)

```text
echo local-console-ok
echo keyboard-reconnected
```

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
fboff
echo UART check
```

The first echo should appear on HDMI and in PuTTY. The last echo should
appear in PuTTY.

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
apply and rollback on scratch files. QEMU also verified shell input and
keyboard reconnect with a live USB boot drive. Raspberry Pi HDMI scanout
and integrated console verification are pending.
