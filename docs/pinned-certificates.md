# Phase 10 pinned certificate verification

`CERTCHK.ELF` reads a DER certificate, extracts its DNS subject alternative
name, P-256 public key and validity dates, and verifies the self-signature
with ECDSA and SHA-256. It asks the kernel to check the complete certificate's
SHA-256 against the operator's boot-local trust store and compare its validity
dates with current UTC. The trust query is read-only and checks the EL0 buffer.
[Certificate structure](https://www.rfc-editor.org/rfc/rfc5280.html#section-4.1) · [EC public keys](https://www.rfc-editor.org/rfc/rfc5480.html#section-2.2)

This increment accepts an exact pinned self-signed P-256 leaf with a DNS SAN,
digitalSignature key usage, serverAuth extended usage, and end-entity basic
constraints. Name matching is ASCII case-insensitive and exact. Wildcards,
IP identities, additional extensions and certificate chains are outside the
profile. The DER file limit is 2,048 bytes. The TLS transcript test reuses
the leaf verifier and checks proof of possession through CertificateVerify
in an OpenSSL capture. [DNS identity rules](https://www.rfc-editor.org/rfc/rfc9525.html#section-6.3) · [TLS transcript test](tls-transcript.md)

## Raspberry Pi verification

Run these from the repository in Windows PowerShell when `OUROSBOOT` is
mounted. Change `$boot` to the drive letter shown in Windows.

```powershell
$boot = 'D:\'
$zip = 'build/rpi5-header/OurOS-Pi5-header-phase10-cert-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip -Apply
```

Safely eject the pendrive and boot the Pi. Generate the clock command in
Windows PowerShell, then paste the resulting line into the Pi monitor.

```powershell
$utc = [DateTime]::UtcNow.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", [Globalization.CultureInfo]::InvariantCulture)
"utc set $utc"
```

In PuTTY, type `exit` at `user:/$` to reach `>`. Paste the `utc set` line,
press Enter, and wait for `>` before each following command. The demo
certificate is embedded in the application. OpenSSL verified its
self-signature and the `demo.ouros.test` DNS name on the PC.

```text
appterm 15 /boot/CERTCHK.ELF demo demo.ouros.test
trustadd 7a2c46997c1c27ba1fb3fae02cc88759655c345eb3391e3220b1966a36cca467
appterm 15 /boot/CERTCHK.ELF demo demo.ouros.test
appterm 15 /boot/CERTCHK.ELF demo wrong.ouros.test
trustclear
```

Expect the first check to report that the fingerprint is unenrolled. The
second check should print `PASS: pinned certificate name, self-signature,
trust and UTC.` and exit with code 0. The wrong-name check should report a
DNS mismatch and exit with code 1. The commands use local boot files and
the serial monitor. A custom certificate can use `appterm 15
/boot/CERTCHK.ELF /boot/PEER.DER <DNS name>` with its own verified fingerprint.

## Local verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-trust-time.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test -KeepBuildFiles
$build = powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header -DataVersion 5 -BuildOnly -KeepBuildFiles
$image = ($build | Where-Object { $_ -like 'IMAGE: *' }) -replace '^IMAGE: ', ''
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -PinnedCert -Image $image
```

The native suite passed the OpenSSL fixture, every truncated DER prefix,
modified signature and signed fields, DNS matching, and trust and time
checks. Both QEMU profiles passed the embedded and disk-file workflows,
including missing UTC, missing pin, wrong name, tampered signature, and
expiry. The UTC regression passed on both profiles. The updater passed
preview, apply and rollback on scratch files. Against the Phase 10 UTC
build it selected `OUROS.IMG` and `CERTCHK.ELF`. The Pi passed the embedded
certificate workflow, rejected the missing pin and the wrong DNS name,
and cleared the trust store. [Pi capture](../proofs/pi5-pinned-cert-2026-11-22.txt)

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-phase10-cert-boot-update.zip` | `bde0413fd714fd07695ae11e9168dcf8d318a149e636804ecfeff162d44cd33a` |
| `OUROS.IMG` | `4b564c29264271bff40ff6e8f9a9d13d078a3ac77bdecbd407d2c2237fba68b6` |
