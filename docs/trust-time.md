# Phase 10 UTC and certificate trust foundations

The serial monitor lets the operator set UTC from a trusted clock. The
kernel advances that sample using the ARM physical counter. The sample
expires after 24 hours, and a counter regression or frequency change
invalidates it. The clock and trust store start empty on each boot. The
operator must verify the time source before setting UTC.
[UTC date format](https://www.rfc-editor.org/rfc/rfc3339.html#section-5.6)

| Monitor command | Effect |
| --- | --- |
| `utc` | Show UTC, source, age, and remaining lifetime |
| `utc set <YYYY-MM-DDTHH:MM:SSZ>` | Set UTC explicitly using year 2000 or later and seconds 00–59 |
| `utc clear` | Invalidate the current sample and allow a backward correction |
| `trustadd <SHA-256>` | Approve an exact certificate fingerprint for this boot |
| `trustlist` | Show the approved fingerprints |
| `trustcheck <SHA-256> <not-before UTC> <not-after UTC>` | Evaluate fingerprint membership and the supplied inclusive validity window |
| `trustclear` | Remove the approved fingerprints |

Use the 64 hex digits of the SHA-256 digest of the exact DER certificate,
verified through a trusted source, when enrolling a real trust anchor.
The store holds four unique anchors. `trustcheck` is a policy test for
the supplied inputs. It requires a valid clock and rejects an unknown
fingerprint or an expired or future window. A future TLS client must
verify the certificate chain, signatures and peer name and obtain its
validity bounds from the authenticated certificate data.
[RFC 5280 path validation](https://www.rfc-editor.org/rfc/rfc5280.html#section-6.1)

EL0 can query time through syscall 55, `uos_utc_time(&sample)`. Success
returns Unix seconds, sample age, and seconds remaining. An unavailable
time returns `UOS_ENOTIME` and preserves the output buffer. The kernel
checks user pointers before writing. `uos_time_ms()` remains the elapsed
counter interface used by existing app deadlines.

## Pi verification

From `C:\Users\tharu\custom_OS`, apply the Phase 10 boot-file package when
Windows mounts `OUROSBOOT`. Change `$boot` if Windows assigns another letter.

```powershell
$boot = 'D:\'
$zip = 'build/rpi5-header/OurOS-Pi5-header-phase10-trust-time-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip -Apply
```

Safely eject the drive and boot the Pi. In PuTTY, type `exit` at `user:/$`
and then run these at the `>` prompt.

```text
utc
trustlist
appterm 5 /boot/UTCCHECK.ELF
```

Expect `UTC unset`, `Trusted SHA-256 anchors=0`, and
`PASS: UTC unavailable and output preserved.`. In Windows PowerShell,
generate the command to paste into PuTTY.

```powershell
$utc = [DateTime]::UtcNow.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", [Globalization.CultureInfo]::InvariantCulture)
"utc set $utc"
```

Copy the printed `utc set ...` line into PuTTY promptly. Then run these
one at a time. The fingerprint is a public test value for the policy
check. Use it only for this test and clear it afterward.

```text
utc
appterm 5 /boot/UTCCHECK.ELF
trustadd a8c7ef11b44f99d24ca05a81a5cee1be9ed05f4ea2f12df3b83bc2b90e851aa3
trustcheck a8c7ef11b44f99d24ca05a81a5cee1be9ed05f4ea2f12df3b83bc2b90e851aa3 2000-01-01T00:00:00Z 2100-01-01T00:00:00Z
trustcheck a8c7ef11b44f99d24ca05a81a5cee1be9ed05f4ea2f12df3b83bc2b90e851aa3 2000-01-01T00:00:00Z 2001-01-01T00:00:00Z
trustclear
```

The EL0 app should report `PASS: UTC advanced with the counter.`. The two
policy checks should report `Anchor and UTC validity passed.` and then
`UTC falls outside the certificate validity window.`. The Pi smoke test
can run from the serial monitor with the boot files and timer available.

## Local verification

`scripts/test-trust-time.ps1` exercises calendar conversion, leap years,
2038 and year rollover, expiry, counter discontinuity, input bounds and
certificate validity boundaries. `-TrustTime` passed on both QEMU profiles
through the serial monitor and the real EL0 app. The TCP and UDP socket
regressions also passed on both profiles. The Pi subsequently passed the UTC
syscall test, accepted the valid trust window, and refused an expired window.
[Pi UTC capture](../proofs/pi5-trust-time-2026-11-22.txt) · [Pinned certificate verification](pinned-certificates.md)

The updater preview against the TCP build selected `OUROS.IMG` and added
`UTCCHECK.ELF`.

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-phase10-trust-time-boot-update.zip` | `5c6a2381fb773637c112585dc145169fdb58598610ac95e494c28253fa753032` |
| `OUROS.IMG` | `021e4f0c3692368ae245ec681ed86561d76a5192368f85c5d437188074fe44b9` |
