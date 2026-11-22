# Phase 10 live TLS 1.3 on the LAN

`TLSECHO.ELF` connects over the existing EL0 TCP API and authenticates
a self-signed P-256 leaf using the boot-local certificate fingerprint
and UTC. It derives the live handshake and traffic secrets from P-256
ECDHE, verifies the server's CertificateVerify and Finished, sends
client Finished, and checks the encrypted echo of the nine bytes
`ouros-tls`. The demo exchanges authenticated close notifications.
[TLS 1.3 record protection and key schedule](https://www.rfc-editor.org/rfc/rfc9846.html#section-5) · [GCM](https://csrc.nist.gov/pubs/sp/800/38/d/final)

The profile uses TLS_AES_128_GCM_SHA256, a single pinned leaf and a
4,096-byte handshake buffer. This LAN demo uses the test DNS name
`demo.ouros.test`, with a separately supplied IPv4 address.
`PKGGET.ELF` uses this TLS profile for HTTP package downloads.
[Package download test](package-download.md)

`UOS_SYS_RANDOM` (57) serves requests of 1 through 64 bytes into checked
writable EL0 memory. It returns `UOS_ENORANDOM` without modifying the
destination if the RNG200 source is unavailable. The monitor's
`rngtest` initializes and checks the source. No random samples are
printed. The TLS client erases its ephemeral scalar and traffic keys.

## Update the boot pendrive

In PuTTY, type `exit` if at `user:/$` and `halt` at `>`. Once the
CPU reports that it has halted, power off the Pi and plug the
pendrive into the Windows laptop. Run this from the repository.
Use the drive letter Windows shows for `OUROSBOOT`.

```powershell
$boot = 'D:\'
$zip = 'build/rpi5-header/OurOS-Pi5-header-phase10-live-tls-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip -Apply
```

The preview against the offline transcript build selects
`Replace OUROS.IMG`, `Replace TLSCHK.ELF` and `Add TLSECHO.ELF`.
Safely eject the pendrive and boot the Pi. Connect Windows to the same
hotspot as the Pi.

## Bring up Wi-Fi and set the clock

In PuTTY type `exit` from `user:/$`. In a fresh boot, run the monitor
commands below individually, waiting for `>` after each one. Replace
`Jack's` on both lines if you are using a different hotspot. Enter the
hotspot password at the hidden prompt.

```text
rngtest
appterm 5 /boot/TLSECHO.ELF rng-check
wififiles
wifiinit
wificores
wifiload
wifistart
wificontrol
wificaps
wifiprepare IN
wifiscan
wifitarget Jack's
netup -R Jack's
netinfo
```

On the Pi, the RNG app should report two distinct samples with exit
code 0. Copy the Pi's IPv4 address from `ACK address=` in `netinfo`.
Run this in Windows PowerShell to find the laptop's Wi-Fi IPv4 address
and print a fresh UTC command.

```powershell
Get-NetIPAddress -InterfaceAlias Wi-Fi -AddressFamily IPv4
$utc = [DateTime]::UtcNow.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", [Globalization.CultureInfo]::InvariantCulture)
"utc set $utc"
```

Paste the printed `utc set` line into the Pi's `>` prompt.

## Run the encrypted echo

Start the Windows echo server. These are the previous successful TCP
test addresses. Replace either address that changed. Keep the window open.
The helper uses Node.js and MSYS2 OpenSSL to generate a new disposable
certificate and prints a fresh `trustadd` command for each run.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tls-echo.ps1 -BindAddress 10.103.105.51 -PeerAddress 10.103.105.102 -Port 4042
```

Paste that window's `trustadd …` line into the Pi monitor and wait for
`Anchor enrolled for this boot.` Then run this in PuTTY. Use the same
laptop address and port as the Windows server.

```text
appterm 90 /boot/TLSECHO.ELF 10.103.105.51 4042 demo.ouros.test
```

PuTTY should show `PASS: live TLS 1.3 pinned handshake and encrypted echo.`
and `state=exited code=0`. Windows should print
`PASS: authenticated TLSv1.3 client Finished and echoed 9 encrypted bytes.`
At the Pi's `>` prompt, run these commands after the test.

```text
netinfo
trustclear
netdown
```

## Local verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-tls-live.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-trust-time.ps1
```

The live loopback test links the same client code as the EL0 app and
exercises AES-GCM, a Node/OpenSSL peer, fragmented I/O, unavailable
entropy, a changed record tag, the wrong DNS name and a wrong pin.
The Cortex-A53 and Cortex-A76 QEMU runs passed the TCP and offline TLS
regressions. They also checked that the RNG syscall rejects invalid
arguments and preserves output when the hardware source is unavailable.
The updater passed preview, apply and rollback on scratch files. The
boot-update ZIP has SHA-256
`dff7c27363ee56583184b701e874af205873dfa5815e35ae83698a2e9dc36fb4`.
The Pi passed `rngtest` and the EL0 RNG check, obtained
`10.103.105.102` from DHCP, set UTC to `2026-11-22T10:17:15Z`,
and enrolled the fingerprint printed by Windows. The live echo
completed with exit code 0 and restored application resources.

```text
Pi: appterm 90 /boot/TLSECHO.ELF 10.103.105.51 4042 demo.ouros.test
Pi: PASS: live TLS 1.3 pinned handshake and encrypted echo.
Windows: PASS: authenticated TLSv1.3 client Finished and echoed 9 encrypted bytes.
```
