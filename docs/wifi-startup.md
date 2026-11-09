# Controlled Wi-Fi firmware startup

The [upload capture](../proofs/pi5-firmware-upload-verified-2026-11-09.txt)
passes all 609,309 code bytes, packed NVRAM upload/readback and the full-RAM
code hash. CR4 remains halted and D11 reset. USB reads and page tests pass
afterward. That establishes upload, **not firmware execution or a connection**.

The [physical startup capture](../proofs/pi5-firmware-startup-verified-2026-11-09.txt)
now confirms `phase=7 shared_memory=ready`, CSR 0xd0, shared pointer 0x201cc0,
flags 1 and no trap/assert indication. USB/page tests still pass. The next test
is [wificontrol](wifi-control.md); its physical packet exchange is UNTESTED.
The `wifistart` command starts the Cypress firmware on the Wi-Fi chip's CR4,
not on the Pi's Cortex-A76. It checks HT clock availability and the firmware's
shared-memory boot structure. It leaves SDIO function 2 disabled and issues no
FullMAC, scanning, association, credential or IP requests. WPA3-Personal remains
the connection target for subsequent stages.

## Run on the existing USB pendrive

Use the newly generated **complete disk image**:
`build/rpi5-header/OurOS-Pi5-header-experimental-sd.img`.
The `-sd.img` name also applies to your USB boot image. Keep the same USB port,
CP2102 wiring and 115200 8N1 terminal settings. Reflash using your existing
method, then on a fresh boot run these commands individually:

```text
storageinit
wififiles
wifiinit
wificores
wifiload
wifistart
wifiinfo
storagetest
pagetest
uptime
```

Observed startup image size: 134217728 bytes (128 MiB); raw kernel: 122852 bytes.
SHA256: `df26b379821a50d3925a7c26152bd783a683f61f41dc631b10233ad7edc47a24`.
That image is preserved as `build/rpi5-header/OurOS-Pi5-header-wifi-startup-verified.img`.
The current candidate at the standard filename adds wificontrol; see its
adjacent `.sha256`/`.json` files and [new procedure](wifi-control.md).

Wait for `wifiload` to finish at `phase=6 upload/readback=verified` with all
609309 bytes verified before entering `wifistart`. The upload can take several
minutes at 400 kHz. Startup has bounded reset/clock/shared-pointer waits; wait
for its prompt too. Run `wifistart` again to display its cached result without
releasing/resetting the CPU again. A failed attempt that changed device state
requires a reboot before retrying.

The expected startup success line is:

```text
Wi-Fi firmware startup: ok phase=7 shared_memory=ready
```

The output also records reset vector, clock CSR, shared address/flags, firmware
console address, trap/assert information and mailbox. A valid shared structure
is narrower evidence than a working FullMAC control channel. A missing DEVREADY
mailbox indication is allowed at this stage because function 2 is still off;
if a ready indication exists, its SDPCM protocol version must be 4. The monitor
continues to say that scan/connect/WPA3/IP are unavailable.

If startup fails, **paste the entire output through `wifiinfo`**, including
phase, last address/word, clock CSR, shared pointer, flags, mailbox and any first
SDIO failure. A timeout after release means the CPU may be executing without
validated readiness; it does not mean the CPU is halted. The command performs
no cleanup writes to firmware-owned RAM after release. Reboot before another
attempt. `storagetest`, `pagetest` and advancing uptime check that USB, memory
and the monitor still operate; they do not prove radio readiness.

## Startup contract

1. Require successful staging and the exact measured BCM4345 revision-6
   core/RAM profile. Recheck live CR4 HALT/clock/reset, D11 reset, code's first
   word and the committed NVRAM token before any write.
2. Clear old SDIO-core interrupt status and install/read back exactly one
   reset-vector word at backplane address zero. This is a dedicated operation;
   the normal RAM API still rejects addresses outside measured TCM.
3. Finish D11's AI reset with PHY clock enabled while CR4 remains halted.
   The upload deliberately stopped with D11 reset asserted; startup releases
   that reset with bounded retries and leaves PHY clock/clock enabled.
   Then apply the CR4 AI reset sequence: force clock with CPUHALT, assert reset,
   verify it, configure clock while reset, release reset with bounded retries,
   and leave the clock enabled. Invalidate cached passive-state claims before
   the first operation that can remove CPUHALT. D11 ownership then belongs to
   the firmware; the host no longer reports it as held in reset.
4. Replace forced ALP with an HT clock request and poll availability. Read the
   last TCM word until firmware replaces the NVRAM token with its shared pointer.
   Validate alignment, TCM bounds and full 64-byte structure extent before any
   dereference. Reject length/complement tokens, zero/unsupported versions,
   firmware ASSERT/TRAP, invalid console pointers and mailbox FWHALT.
5. Preserve phase and first transport diagnostics. No automatic retries,
   function-2 enable, interrupt handlers, CLM download or network setup.

| Phase | Meaning |
| --- | --- |
| 1 | Upload/profile/live-state checks |
| 2 | Interrupt clear and reset-vector installation |
| 3 | D11 reset completion, then CR4 reset assertion |
| 4 | CPUHALT/reset release and clock readback |
| 5 | HT clock availability |
| 6 | Shared pointer, flags and mailbox validation |
| 7 | Shared-memory initialization confirmed |

Native tests exercise the exact successful sequence, delayed readiness, all
38 I/O failures in that sequence, 22 invalid boot states, reset/HT/shared-pointer timeouts, malformed
pointers, versions, ASSERT/TRAP/FWHALT and repeated-command behavior. SDIO tests
exercise the dedicated address-zero word write/readback through the PIO layer
while the general register/RAM APIs continue to reject that address. QEMU tests
check the command's explicit refusal on a machine with no Pi radio. These tests
could not establish physical firmware boot; the capture above now supplies
that separate evidence.

Next: function-2 transport, SDPCM framing and a real FullMAC control reply;
then firmware security-capability checks, country/CLM setup, scan and the
WPA3 SAE/PMF path. DHCP/ARP/IPv4/ICMP and DNS follow a working association.

Primary references for register/protocol definitions:
[CR4 AI activation](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/chip.c),
[reset vector, HT clock and shared-memory layout](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c),
[SDIO core register offsets](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.h).
