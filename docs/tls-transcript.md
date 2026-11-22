# Phase 10 TLS 1.3 transcript authentication

`TLSCHK.ELF` checks a captured TLS 1.3 server handshake from a local
OpenSSL client and server. It verifies the DNS name, self-signature,
enrolled SHA-256 pin and UTC validity of the certificate carried in the
handshake. It checks the P-256 CertificateVerify signature over the
transcript through Certificate and the server Finished HMAC over the
transcript through CertificateVerify. [TLS 1.3 authentication](https://www.rfc-editor.org/rfc/rfc9846.html#section-4.4)

The verifier accepts a plaintext handshake up to 4,096 bytes for the
P-256, TLS_AES_128_GCM_SHA256 profile with a single pinned leaf. The
fixture supplies the test server handshake traffic secret. The test
runs from `/boot` with the operator-set clock and requires no Wi-Fi.
`TLSECHO.ELF` exercises live key derivation, protected records and
application data. [Live TLS test](tls-live.md)

## Raspberry Pi verification

In PuTTY, type `exit` if you are at `user:/$`, then type `halt` at
`>`. Once the CPU reports that it has halted, power off the Pi and
plug the boot pendrive into Windows. Use the drive letter Windows
shows for `OUROSBOOT`. From the repository in Windows PowerShell,
run the preview and apply the boot-file update.

```powershell
$boot = 'D:\'
$zip = 'build/rpi5-header/OurOS-Pi5-header-phase10-tls-transcript-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip -Apply
```

Against the previous certificate build, the preview reports
`Replace CERTCHK.ELF` and `Add TLSCHK.ELF`. Reuse the existing
boot pendrive. Safely eject it after the update and boot the Pi.

Generate a fresh UTC command in Windows PowerShell.

```powershell
$utc = [DateTime]::UtcNow.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", [Globalization.CultureInfo]::InvariantCulture)
"utc set $utc"
```

In PuTTY, type `exit` at `user:/$` to reach the monitor's `>` prompt.
Paste the UTC line printed by PowerShell and press Enter. Run each of
the following at `>`, waiting for the monitor prompt to return.

```text
appterm 45 /boot/TLSCHK.ELF demo
trustadd 54019823def563f04bbf57dd1c4c0236f2ded3f5c1352f21619b84c1586b0585
appterm 45 /boot/TLSCHK.ELF demo
appterm 45 /boot/TLSCHK.ELF demo wrong.ouros.test
appterm 45 /boot/TLSCHK.ELF transcript
appterm 45 /boot/TLSCHK.ELF finished
trustclear
```

| Command | Expected output | Exit code |
| --- | --- | --- |
| First `demo` | `TLS certificate fingerprint is not enrolled.` | 1 |
| `demo` after enrollment | `PASS: TLS 1.3 pinned leaf, CertificateVerify and server Finished.` | 0 |
| `demo wrong.ouros.test` | `TLS certificate DNS name mismatch or unsupported name.` | 1 |
| `transcript` | `TLS CertificateVerify rejected.` | 1 |
| `finished` | `TLS server Finished rejected.` | 1 |

## Local verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-trust-time.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test -KeepBuildFiles
$build = powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header -DataVersion 5 -BuildOnly -KeepBuildFiles
$image = ($build | Where-Object { $_ -like 'IMAGE: *' }) -replace '^IMAGE: ', ''
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -TlsTranscript -Image $image
```

The host test covers the OpenSSL fixture, every truncated transcript
prefix, malformed inputs, wrong pin and name, and changes to the
transcript, Finished and handshake secret. The Cortex-A53 and Cortex-A76
QEMU profiles each passed the valid transcript, rejected missing UTC,
missing pin, wrong name, transcript tampering and Finished tampering,
and restored the resources from all six runs. The capture generator
`tests/tls13_capture.mjs` takes a disposable PEM key, its matching pinned
leaf for `demo.ouros.test`, and the output header path. It uses a loopback
TLS exchange, decrypts the capture with OpenSSL-backed AES-GCM and
independently checks the Finished value before writing the fixture.

The boot-update ZIP passed the updater's preview, apply and rollback
tests. Its SHA-256 is
`5f3023903182e589460676b088f84f3f35d52a43c404897fef6eed768dce8889`.

The Pi accepted the enrolled certificate and rejected the missing pin,
wrong DNS name, changed ClientHello and changed Finished. It restored
the resources after each run and cleared the pins. [Pi capture](../proofs/pi5-tls-transcript-2026-11-22.txt) · [Live TLS test](tls-live.md)
