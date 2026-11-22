# Phase 7 background Wi-Fi

The first increment keeps a verified Pi Wi-Fi connection open while the
kernel monitor and EL0 shell run. The monitor owns the Wi-Fi commands.

| Command | Effect |
| --- | --- |
| `netup [-r\|-R] <SSID>` | Start Wi-Fi and DHCP. `-r` reconnects to the original access point. `-R` also allows the hotspot's BSSID to change |
| `netping [IPv4]` | Queue three ICMP requests to the DHCP gateway or a supported numeric target |
| `netrenew` | Queue early renewal of the current DHCP lease for testing |
| `netinfo` | Show address, link, polling, ping, and cleanup state |
| `netdown` | Stop background service and run the existing radio and key cleanup |
| `networktest [el0]` | Exercise the worker locally. The `el0` form uses `/boot/SPIN.ELF` for a deliberate one-second app timeout |

`netup` performs the same bounded setup and hidden password entry as
`wificonnect`. Once IPv4 is ready, it returns to the normal `>` prompt.
`wificonnect` still opens the foreground `net>` console for DNS and hostname
pings. Either mode uses the same one-shot Pi radio initialization.

The worker uses the fourth guarded kernel-task stack. It owns the live SDIO
association and yields between polls. The monitor, idle app scheduler, and
EL0 timer checkpoints give it a turn about every 10 ms when available. SDIO
operations execute on the worker's stack after the exception returns. A long
synchronous monitor command can delay polling. The timer, other interrupts,
and UART input stay owned by the existing kernel and shell.

The worker renews at T1 with unicast to the lease server and tries a broadcast
at T2 if the lease is still awaiting renewal. The server can supply both timers.
The defaults are 50% and 87.5% of the lease period. The request carries the
current client IP in `ciaddr` and uses a fresh transaction ID. The worker
maintains the existing address, verified mask, gateway and DNS settings
during a renewal. A compatible ACK advances the deadline only after a
successful request write and before the old deadline. A NAK from the
current server ends the session. [DHCP renewal rules](https://www.rfc-editor.org/rfc/rfc2131.html#section-4.4.5) · [Timer options](https://www.rfc-editor.org/rfc/rfc2132.html#section-9.11)

Disconnect, expiry, NAK and link failure run the association's normal
teardown and wipe the worker stack. Support remains scoped to direct DHCP
replies and a one-hour limit for each association. Live group key rotation
has native model coverage and awaits Pi verification.

## Verify on Raspberry Pi 5

Use the boot updater on the existing `OUROSBOOT` volume. Set the path to its
Windows drive letter. The second command applies the previewed files and
saves a rollback copy.

```powershell
$boot = 'D:\'
$zip = 'build/rpi5-header/OurOS-Pi5-header-phase7-hotspot-reconnect-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip -Apply
```

Boot the Pi, type `exit` at `user:/$`, and run the first checks at the
`>` monitor prompt. In `networktest el0` the middle character is a lowercase
L and the last one is a zero. The monitor also accepts `networktest e10`.
The Wi-Fi setup uses the firmware files on `/boot`. Prepare your own
WPA3 test network as in the existing Pi workflow. [Radio preparation](wifi-session.md#run-on-the-pi)

```text
exit
networktest
networktest el0
wififiles
wifiinit
wificores
wifiload
wifistart
wificontrol
wificaps
wifiprepare IN
wifiscan
wifitarget YourSSID
netup YourSSID
```

Use the country code for your location. Enter the password at the hidden
PuTTY prompt. After `Background Wi-Fi ready`, queue a gateway ping and enter
the EL0 shell. For repeatable evidence, leave it open for about five seconds
before returning to the monitor.

```text
netping
netrenew
appshell
echo shell-responsive
exit
netinfo
netdown
netinfo
fileinfo
```

Verify that the first `netinfo` has increasing background polls, a current
address, and a ping result. A server that accepts the renewal should also
report `DHCP renewal requests=1 rebind_requests=0 completed=1` after an
early `netrenew`. The last `netinfo` should show that the session
ended, the address is inactive, and the firmware acknowledged DOWN and
key deletion. A separate boot and hotspot shutdown exercises link loss.
`fileinfo` checks that no application file handles were left open.

## Background worker verification

The Pi booted the first worker build and passed `networktest` at the monitor.
That capture covers the inactive session and worker smoke test.
[Pi capture](../proofs/pi5-network-worker-smoke-2026-11-22.txt)

The native session tests passed IPv4 echo, receive service, lease expiry,
conflict, and I/O failure cases without taking UART input. The native
association model verified port deauthorization, key deletion, policy
restoration, and private state erasure after stop and link loss.
`test-shell-qemu.ps1 -NetworkWorker` passed Cortex-A53/EL1 and Cortex-A76/EL2
with both the monitor and busy EL0 checkpoints. The Windows updater
passed its scratch tests, and the Phase 5 boot fixture preview selected
only `OUROS.IMG` for the first worker build. Live Pi Wi-Fi verification remains
open.

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-phase7-background-boot-update.zip` | `41d0ad7d793d5f7365a7e36fe52de163380f1a901ac733a17db1a062f128c32d` |
| `OUROS.IMG` | `e425af9b2908d7a5675b808d2e35c36937cf92bae8b13403bc70b48016b292f6` |

## DHCP renewal verification

Native DHCP fixtures checked the request layout and both timer transitions,
including a compatible reply from a second server at T2, stale or changed
replies, write failure, lease expiry, and NAK. The Wi-Fi association model
renewed a lease during ping and verified cleanup after a NAK. Both QEMU
profiles passed the shell guidance, `networktest e10` alias, and the worker
test under busy EL0. The Pi passed `networktest el0`, two gateway ping sets,
and three manual lease renewals. Broadcast rebinding and disconnect cleanup
remain open on hardware. [Pi renewal capture](../proofs/pi5-dhcp-renewal-2026-11-22.txt)

The renewal boot package preview selected `OUROS.IMG` and `SHELL.ELF`.
The updater passed its preview, apply and rollback tests on scratch files.

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-phase7-dhcp-renewal-boot-update.zip` | `6e1229d201b028a24f1b7ba277428ddddfdb63301851f652e01dd81c2c78c269` |
| `OUROS.IMG` | `b8bd58cb47b7345cf9f6ea17e961730c57b1ea415eb07a72158d221bb6828ac7` |
| `SHELL.ELF` | `ffd7986e6af3c5a7bb0a2dad2ef76da21997ed9b3761d3356afdb3bcc82fcedc` |

## Same-access-point reconnect

`netup -r YourHotspotName` keeps reconnect pinned to the original BSSID.
Use `netup -R YourHotspotName` for a hotspot that may change its BSSID when
toggled. Both modes refresh the passive scan before each attempt and use
the advertised 2.4 GHz channel. The uppercase mode requires a single
matching SSID and the original WPA3 SAE method and supported security profile.
Plain `netup` connects once.

After confirmed link loss the worker retries up to three times, with 5, 10,
and 20 seconds of backoff. A successful reconnect resets that budget for
the next outage. It requires acknowledged radio DOWN, verified key deletion
and restored policy before scanning. A join uses fresh SAE randomness, a
distinct firmware transaction range, and new DHCP acquisition.
The driver accepts Broadcom's explicit `BCME_NOTASSOCIATED` status as a
closed port after confirmed peer loss. Radio DOWN and key deletion still
have their own checks. [Broadcom status definitions](https://android.googlesource.com/platform/hardware/broadcom/wlan/+/froyo/bcm4329/src/include/bcmutils.h)

With `-r`, the worker keeps the SAE password element for the original
peer and erases the password after derivation. With `-R`, it keeps the
password in RAM on its guarded stack so it can derive a fresh element for
the new BSSID. `netdown` cancels any pending retry. The worker wipes the
retained credential when it ends.

Once `netdown` confirms the worker stopped and `netinfo` confirms cleanup,
you can start another session in the same boot. Run `wifiscan`,
`wifitarget YourHotspotName` and the chosen `netup` command again. Reboot
if the radio or event-mask cleanup could not be verified.

Apply the boot-file update above. Boot the Pi and type `exit` at `user:/$`.
With your WPA3 hotspot enabled, run these commands at the `>` prompt,
waiting for each to finish. This example uses `Jack's` and country code `IN`.

```text
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
```

1. Enter the hotspot password at the hidden prompt. When `Background Wi-Fi
   ready. Hotspot reconnect enabled.` appears, run `netinfo` and check for
   `Reconnect state=online` and `Session active=yes`.
2. Turn the hotspot off. Run `netinfo` every few seconds until the state
   shows `waiting` or `scanning`. Turn the hotspot back on and leave the
   worker running. Repeat `netinfo` every 10 seconds. A scan alone can take
   up to 30 seconds plus I/O.
3. After `Reconnect state=online`, `restored=1` and `Session active=yes`,
   run `netping`. Wait a few seconds and run `netinfo` to check for three
   gateway replies. Run `netdown` and then `netinfo` to check cleanup.
4. If the state reaches `stopped` with `restored=0`, save the `netinfo`
   output. `Reconnect scans`, `last_scan`, `target`, and the peer and join
   phase fields distinguish discovery, target selection, and join failures.

The native association model passed peer-event and periodic link-loss
detection, verified cleanup, and a new SAE/DHCP exchange with distinct
firmware cookies. The updater preview against the renewal build selected
only `OUROS.IMG` for the original reconnect build. The Pi subsequently
confirmed automatic link-loss cleanup after a plain `netup` session.
[Pi cleanup capture](../proofs/pi5-link-loss-cleanup-2026-11-22.txt)

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-phase7-reconnect-boot-update.zip` | `1b5ab31a8d288bfabedf06207c9f6478137aebb3330a62a1c25474bb70c7e56d` |
| `OUROS.IMG` | `6587a5fca768dc5f67bf2d59494de6482bbba33ee312001c091c6a06ba425fc7` |

The rejoin update passed native cleanup checks and both QEMU worker
profiles, including the monitor hint for a hotspot name with an
apostrophe. The updater preview selected `OUROS.IMG` and `SHELL.ELF`.

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-phase7-wifi-rejoin-boot-update.zip` | `5670c0eccfaca9691d32a30f2bca6148960e25aa7f6dcbad9be58365e45e184c` |
| `OUROS.IMG` | `09acf2b61d366ddd4152b946da05b0ce0f09bdfa3192391391c3d56d3838bcfc` |
| `SHELL.ELF` | `435446df1a1f87c3e5870cc9ec7300ba78538921a93f523eba0957f5760899da` |

The subsequent Pi capture showed three retries with `restored=0` before
any DHCP attempt. The retry path had reused the original scanned BSSID.
Earlier captures of the same hotspot showed a different BSSID. The BSSID
after the failed toggle is unknown. The updated scan model exercises fresh
scan IDs, stale event filtering, cancellation, cleanup, and retry gating.
The native target tests cover channel changes and changed-BSSID selection.
Both QEMU worker profiles passed. The updater passed its scratch tests,
and the preview against the prior rejoin files selected only `OUROS.IMG`.
The Pi passed the hotspot toggle test. The first scan did not see the
hotspot. The third found it at a different BSSID, and the worker rejoined
automatically, obtained DHCP, and passed a separate gateway ping. `netdown`
confirmed DOWN, key deletion, private erasure and policy restoration.
[Pi reconnect capture](../proofs/pi5-hotspot-reconnect-2026-11-22.txt)

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-phase7-hotspot-reconnect-boot-update.zip` | `55f0b900a4ef3c4d8cc486261a42d55ac91b1acc8f15f2aef9041f57351cdf90` |
| `OUROS.IMG` | `5b134152726d87a9fdb69b109f8270938f52a4c746034bb0e563ebfed355f888` |

## WPA3 group key rotation

The offline SAE/CCMP/PMF validator checks the sender, replay counter, CMAC,
AES key wrap, and GTK/IGTK layout of group message 1. An authorized session
now queues that frame in the driver and handles it outside the receive
callback. It waits for the firmware to acknowledge new group keys before
writing the authenticated reply. Repeated active keys skip installation.
A bounded private history allows up to 16 fresh changes of each key per
association and refuses reuse of a retired key. Teardown deletes all
attempted key slots, including those first used during rotation.
[Hostap supplicant](https://android.googlesource.com/platform/external/wpa_supplicant_8/+/0be32f16ad21da5b5f30ab56b2b49a10260c41fc/src/rsn_supp/wpa.c#3211)

The independent OpenSSL fixtures compare the complete reply byte-for-byte.
The native association model exercises rotation with an active ping,
higher-counter retries without reinstallation, stale and invalid MIC
rejection, and cleanup after partial key installation, reply failure,
retired-key reuse and link loss. On-air rotation awaits a Pi capture.

The Pi build and both QEMU worker profiles passed. The updater verified
installation and rollback on scratch files. The preview against the
hotspot reconnect package selected only `OUROS.IMG`.

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-phase7-group-rekey-boot-update.zip` | `72a6e8a5371ff6ec2dc313fde6516c12baf46cf11250130f95e815541cabd7c6` |
| `OUROS.IMG` | `25c914460af59d4678618816da8486e4b0a500005331a0192ef76b0cb08115b7` |

### Test rotation on Raspberry Pi 5

Use an owned 2.4 GHz WPA3-SAE access point with DHCP that can rotate keys
while a client stays connected. For a test AP already running `hostapd`,
set `wpa_group_rekey=60` in its configuration and restart the AP before
connecting the Pi. The value is in seconds. [Hostapd configuration](https://android.googlesource.com/platform/external/wpa_supplicant_8/+/0be32f16ad21da5b5f30ab56b2b49a10260c41fc/hostapd/hostapd.conf)

With the pendrive mounted as `OUROSBOOT` in Windows, apply the boot-file
update from the repository. Set `$boot` to the assigned drive letter.

```powershell
$boot = 'D:\'
$zip = 'build/rpi5-header/OurOS-Pi5-header-phase7-group-rekey-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip -Apply
```

Safely eject the drive and boot the Pi. At `user:/$`, type `exit`. Then
run these at the `>` monitor prompt. Replace `YourSSID` with the test AP's
actual name and use the country code for that AP's location.

```text
wififiles
wifiinit
wificores
wifiload
wifistart
wificontrol
wificaps
wifiprepare IN
wifiscan
wifitarget YourSSID
netup YourSSID
```

Enter the password at the hidden prompt. Keep the AP on. Run `netinfo`
after connecting and again after a group timer interval. Look for
`Group rekey rotations=1` or higher, `result=ok`, and `Session active=yes`.
On a `hostapd` test AP, check its log for a completed group key handshake
for the Pi at the same time.
Run `netping`. Wait a few seconds and run `netinfo` to confirm three
gateway replies. Finish with `netdown` and `netinfo`. The cleanup line
should report `keys_cleared=yes`, and any extra key slots listed should
have matching issued and cleared masks. Zero rotations means no completed
rotation was observed during the session.
