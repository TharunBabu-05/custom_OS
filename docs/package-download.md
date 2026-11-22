# Phase 10 HTTPS package download

`PKGGET.ELF` downloads one AArch64 ELF from the Windows test server to a
new file at the root of `/data`. It establishes the pinned TLS 1.3
connection before making the request and accepts a bounded HTTP 200
response with one Content-Length. It checks for an authenticated TLS
close and compares the complete body with the operator-supplied
SHA-256. The same ELF parser used by the kernel loader checks the result.
[HTTP response framing](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3)

A writable v3 or later `/data` volume is required. The transfer is
limited to the volume's existing per-file bound and 32 KiB. The app
holds bytes in private pages until verification succeeds. It writes
them to a staging file, checks the readback hash, then renames the
file into place without overwriting an existing destination. Install
or run the downloaded file explicitly. A power interruption before
publication can leave a `.fetch-*` directory in `/data`.

## Update and start Wi-Fi

In PuTTY, type `exit` if at `user:/$`, then `halt` at `>`. Once the
CPU confirms it halted, power off the Pi and plug the pendrive into
Windows. Run the following from the repository, using the drive letter
Windows shows for `OUROSBOOT`.

```powershell
$boot = 'D:\'
$zip = 'build/rpi5-header/OurOS-Pi5-header-phase10-package-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip -Apply
```

The preview from the live TLS build selects `Add PKGGET.ELF` and
`Replace TLSECHO.ELF`. Safely eject the pendrive, boot the Pi, and
follow the commands for the same hotspot and fresh UTC in the live
TLS guide. [Wi-Fi and UTC commands](tls-live.md#bring-up-wi-fi-and-set-the-clock)

## Download, install and run the fixture

Run the server in Windows PowerShell after Wi-Fi and UTC are ready.
Substitute the current laptop and Pi IP addresses if they changed.
The package path should name the built `HELLO.ELF` on the PC.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tls-echo.ps1 -BindAddress 10.103.105.51 -PeerAddress 10.103.105.102 -Port 4043 -Package build/apps/HELLO.ELF
```

Paste the printed `trustadd` line into PuTTY at `>`. Then paste the
printed `appterm 120 /boot/PKGGET.ELF …` line. The long hexadecimal
argument is the expected SHA-256. The default destination printed
by the test server is `DLHELLO.ELF`. Use a short new name if that file
already exists.

Expect `PASS: HTTPS package SHA-256 and ELF verified. Saved /data/DLHELLO.ELF`
with exit code 0. Windows confirms that it served 512 package bytes.
Then run these commands in PuTTY, using the destination you chose.

```text
appadd netdemo 1.0 /data/DLHELLO.ELF
appstart netdemo
applist
trustclear
netdown
```

Expect the managed app to print `Hello from an OurOS EL0 application.` and exit with
code 0. The install copies the package into the managed app store.

## Local verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-tls-live.ps1 -Package build/apps/HELLO.ELF
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test -KeepBuildFiles
$build = powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header -DataVersion 5 -BuildOnly -KeepBuildFiles
$disk = ($build | Where-Object { $_ -like 'IMAGE: *' }) -replace '^IMAGE: ', ''
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -PackageDownload -Image $disk
```

The Windows loopback test transferred a 32 KiB ELF fixture through
OpenSSL and matched its bytes. The native framing tests covered
fragmented input, conflicting lengths, truncation, excessive data,
unsupported encodings and overlong headers. Both QEMU profiles passed
the missing-clock, unavailable-network, wrong-hash and invalid-ELF
checks. They also checked storage readback, destination preservation,
stage cleanup and app resource cleanup.

The Pi accepted the operator-set clock and the Windows certificate pin,
saved the verified 512-byte ELF and exited with code 0. A repeated
download to the same path reported that the destination already
existed. `appadd` installed `netdemo` 1.0, `appstart` printed the
expected greeting with exit code 0, and `applist` showed the active
version. The session cleared its trust store and stopped Wi-Fi.
[Pi and Windows capture](../proofs/pi5-package-download-2026-11-22.txt)
