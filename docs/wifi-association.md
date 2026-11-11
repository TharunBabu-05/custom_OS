# Association after verified SAE

**Current stage: [authenticated M3 retry handling](wifi-m3-retry.md).** The latest Pi log acknowledges all key SETs/deletions; a third M3 blocks M4. The implementation history below describes an earlier revision.



**Previous phase: [association-IE diagnostics and handshake foundations](wifi-key-handshake.md).**
The newest Pi capture passes aggregate RX, link/identity/security, EAPOL M1 and
cleanup. Its request-IE parser still rejects the 122-byte firmware reply.
This revision supports compact RSN defaults, retains exact failure diagnostics
and adds offline AES/CMAC/unwrap/PTK/M2 helpers; no live key handshake yet.

**Current step: [SDPCM aggregate receive support](wifi-sdpcm-glom.md).**
The newest Pi test passes hunting-and-pecking peer confirmation and firmware
authentication-success handoff, then stops at an unsupported glom descriptor.
Cleanup was unconfirmed in that run. The new transport handles bounded
aggregates; association/M1 still require a Pi capture.

**Earlier method blocker and correction:** the [Pi capture](../proofs/pi5-sae-no-h2e-advertisement-2026-11-10.txt)
advertises PSK+SAE/PMF-capable with no H2E. The old method guard refused before
password input. [Hunting-and-pecking support](wifi-sae-hunt.md) now selects
ordinary SAE for this case, while preserving SAE-only/required-PMF policy.
Use the same fresh-boot sequence below with the new image.

The [latest successful SAE log](../proofs/pi5-sae-confirm-verified-2026-11-10.txt)
passes `peer_confirm_verified=yes`, both firmware TX acknowledgements and
cleanup. Preparation took 3 seconds and peer key calculation took 2900 ms.
The first commit attempt succeeded. This establishes a matching SAE key with
Jack's; the old `wifisaeauth` test deliberately stopped and erased it afterward.
The password was not the missing connection step.

**New command: `wifiassoc <SSID>`.** It repeats SAE with fresh randomness,
passes verified authentication success to firmware, checks association and
observes the first EAPOL-Key packet. This new hardware path needs a Pi capture.
It does not reply to EAPOL or install keys. It ends DOWN even on success;
there is no persistent connection, authorized data port, IP address or ping yet.

The [subsequent Pi association attempt](../proofs/pi5-association-ie-format-stop-2026-11-10.txt)
stopped at phase 1: the old driver could not parse the initial `wpaie` GET.
No join, SAE commit, confirm or authentication-success notification was sent.
DOWN and security/mask restoration, storage and pages passed. This is a local
compatibility stop, not evidence of a rejected password. The log did not retain
the query bytes, so their exact format is unknown.

This revision removes that parsing prerequisite for the exclusively owned
fresh Pi station. It explicitly clears/sets the application RSN override and
later validates the association-request IEs reported by firmware. The optional
GET now records length, status and at most 32 public prefix bytes for diagnosis.
The setter and actual request checks remain mandatory; this does not accept
weaker security or authorize a port from a SET acknowledgement.

## Run this image

Flash [the full image](../image_files/OurOS-Pi5-header-experimental-sd.img),
or extract it from [the matching ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip).
Use your USB2 pendrive as before. No microSD is required. The builder keeps
older full images and ZIPs directly in `image_files`; it never flashes a device
or commits/pushes to GitHub.

The preceding IE-journal revision's kernel was **234988 bytes**, the full disk **134217728 bytes**
(128 MiB), and the ZIP **746955 bytes**. Full-disk SHA256:
`614df3b404a5fc7f3af109c1cf77032e918226b7c080d89d5d0a696780d25033`.

On a **fresh boot**, enable the hotspot and run these individually:

```text
storageinit
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
wifiassoc Jack's
```

Skip `wifijoinprobe`, `wifisaeprobe`, `wifisaeauth` and `wifiauthprep` in this
boot. The attempt is one-shot; DOWN does not clear every firmware candidate,
SSID or PMKID cache. Reboot before repeating. The target BSSID and channel
come from this boot's validated scan, not hardcoded hotspot addresses.

Set PuTTY **Local echo: Force off** and **Local line editing: Force off**.
Type the password only at the hidden prompt, without quotes, then Enter.
It is not saved or embedded in the image. UART is unencrypted. Backspace/Delete
edit, Ctrl+U clears, empty Enter cancels. The existing 1..128 printable-ASCII
limit, rejected-line draining and erasure apply. Long arithmetic self-tests
are optional; you do not need to run them before this procedure.

Wait for the prompt, then capture:

```text
wifiinfo
storagetest
pagetest
uptime
```

Share the complete output of `wifiassoc` and these checks, excluding password
input. Useful success markers are:

```text
peer_confirm_verified=yes
firmware_auth_success_SET=acknowledged
association_verified=yes
Association request IEs=verified
EAPOL_M1=observed (unauthenticated)
cleanup_clear_SET=acknowledged
```

Check `profile_SET=acknowledged`, `Association request IEs=verified`,
`cleanup_clear_SET=acknowledged` and verified security/event-mask restoration.
RSN cleanup now reports an acknowledged clear of the software-owned override;
it does not claim that a GET reproduced the old firmware bytes.
`Association/EAPOL probe: ok phase=6` means this bounded observation passed;
it does not mean the device stayed connected. If association passes but M1
is absent, the overall result is timeout and the separate markers retain
that distinction. Rejection diagnostics include event/status/reason. Capture
them before rebooting; do not retry other radio probes in the same boot.

## Implemented boundary

The new original module is [brcm_assoc.c](../drivers/storage/brcm_assoc.c).
For the Pi's exclusively owned fresh station, it establishes an acknowledged
empty `wpaie` override, then sets the fixed SAE AKM 8, CCMP-128 group/pairwise,
required-PMF/BIP-CMAC-128 RSN IE. This happens after the DOWN/station guard and
before WPA/security selection. Live security and the event mask are still
checked after UP. Setter errors stop; there is no WPA2 fallback. Only this
one-shot path writes the override, and earlier join/SAE attempts are refused.
The generic snapshot/readback mode remains for callers without that ownership
guarantee and still rejects unknown snapshot layouts.

Only after the peer confirm verifies and both SAE frame transmissions have
matching successful acknowledgements can the handoff export SAE keys.
The PMK is immediately erased in this observation stage. Only the PMKID goes
into the zero-initialized, exact 60-byte `auth_status` success structure,
alongside flags, BSSID and SSID. No key bytes are printed. This avoids sending
uninitialized PMKID/padding. The unsupported firmware supplicant is not used.
See the primary [brcmfmac connection/external-auth implementation](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c)
and [PMKID correction](https://lkml.rescloud.iu.edu/hypermail/linux/kernel/2608.0/05034.html).

Association requires acknowledged `auth_status`, a correctly bound successful
SET_SSID or LINK-up event, exact GET_BSSID/GET_SSID identity, read-back
SAE-only/AES/required-PMF settings and validated association-request IEs.
The new read-only `assoc_info` / `assoc_req_ies` queries follow
[brcmfmac's association-IE retrieval](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c#L6016).
Only the declared request length is parsed, within a 512-byte bound. The
request must contain the exact SSID and a single SAE-only/CCMP RSN element
requiring PMF and BIP-CMAC-128. If a PMKID is present it must match this session.
WPA1 fallback, duplicate/malformed security elements or mismatched credentials'
public key identifier are rejected. If RSNXE is present, its H2E bit must match
the selected SAE method; absence is reported separately. Other well-formed
request IEs are length checked.
Events are bound to the intended peer,
interface and BSS; rejected/deauthentication events stop the attempt.
The initial RSN IE has no PMKID; firmware receives the verified PMKID later.
This follows the Linux `wpaie` RSN path. Separate RSNXE transmission is not
implemented or claimed; any resulting AP rejection must be diagnosed from
the physical capture rather than weakening the requested security.

The control transport now exposes bounded receive-only BCDC data observation.
The parser checks BCDC version/interface/offset, Ethernet peer/local MACs,
EAPOL lengths/type and RSN descriptor 2. The narrow SAE/CCMP M1 profile expects
AKM-defined descriptor version 0, pairwise+ACK (`key_info=0x88`), key length 16,
nonzero ANonce, zero MIC and consistent key-data length. See the primary
[hostap authenticator](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/38ad1edd94f7b4f3c3937dd21a9b7ef52140babf/src/ap/wpa_auth.c)
and [EAPOL definitions](https://android.googlesource.com/platform/external/wpa_supplicant_8/%2B/master/src/common/wpa_common.h).
M1 has no validatable MIC and cannot authenticate the AP or authorize a port.
Callbacks only parse bounded data; identity/control I/O runs outside callbacks.

The new wait is at most 10 seconds/1000 polls, 100 identity queries and 256
events/data frames each, in addition to the existing bounded SAE exchanges
and control I/O. Cleanup sends DOWN, clears the owned override with an
acknowledged SET, then restores the full security/event policy. Setup/policy
failures before UP also close the IE journal. Generic mode instead restores
and reads back the saved IE. Unconfirmed cleanup disables
further driver I/O. Private SAE state is erased on every attempted exit.

## Validation and remaining work

Native models cover successful SAE-to-association handoff, early events inside
a SET, delayed identity, exact serializers, rejected/wrong-peer events,
weaker security, malformed/truncated EAPOL, failed acknowledgements, stalled
clocks, cleanup failures and failure injection at every successful-flow
operation. Parser checks include 10,000 mutated BCDC/EAPOL frames. Models
cannot prove this firmware/hotspot combination accepts association.

The complete native Wi-Fi suite passes, including 80 injected failures in the
association flow and the optional raw-data observer through split SDIO reads.
The ownership mode additionally injects failures at each of its 80 operations,
models opaque/unsupported GET, rejects bad/absent/truncated request IEs and
checks setter/cleanup failures. Only the format-stop location is physically
observed; the opaque GET fixtures are synthetic, not a reconstruction of its
unrecorded bytes.
The association-request parser also passes all required-prefix truncations,
10,000 mutations, PMKID/RSNXE cases and output guards. A deauthentication
event received inside the query prevents a successful association result.
Pi header, QEMU virt and handoff builds pass. Image validation, corrupted
MBR/FAT/kernel and wrong-console rejection, ZIP integrity and independent
decompression/hash comparison pass. The EL2 QEMU USB test reads the packaged
firmware through BOT/FAT, checks 512 sectors against the host and preserves
the delivered image hash. QEMU refuses `wifiassoc` without asking for a password.
Cortex-A53/A76 monitor and crypto tests and the A53 fault-handler test also pass.

After physical association/M1 verification, the next implementation is the
host four-way handshake: fresh SNonce/PTK, AES-CMAC authentication, replay
checks, authenticated key-data unwrap, pairwise/group/management key install
and EAPOL M2/M4 transmission. Only then may the controlled port be authorized.
ARP/IPv4, DHCP, ICMP and DNS follow, enabling gateway ping and eventually
`ping www.google.com`. Downloading/installing programs needs additional
application and persistent-storage support.
