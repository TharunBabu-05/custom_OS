# Firmware key installation and live M4 test

**Current stage: [authenticated M3 retry handling](wifi-m3-retry.md).** The Pi now acknowledges all three key SETs and deletions; a later M3 blocked M4. The details below describe the earlier revision.


The [new Pi capture](../proofs/pi5-key-data-offline-m4-verified-2026-11-10.txt)
passes the previous stage: verified SAE, association, M2/M3 MIC/unwrap, exact
AP RSN match (26 bytes), GTK (16 bytes, ID 1), IGTK (16 bytes, ID 4), and offline
M4 generation. DOWN, RSN clear, policy restoration, storage and pages pass.
That image deliberately sent no M4 and installed no keys.

## New behavior

`wifiassoc Jack's` now tests firmware key installation and **live M4 transmission**
after all existing SAE/M3 gates pass:

1. Retain the authenticated GTK/IGTK privately for this synchronous scope.
2. Install PTK's 16-byte TK, GTK and IGTK with three acknowledged `wsec_key`
   SETs. No PMK or password is sent to the firmware supplicant.
3. Re-read the AP identity and SAE/AES/required-PMF settings after installation.
   A mismatch or disconnect prevents M4.
4. Send the authenticated 113-byte M4 through a separate guarded BCDC/SDPCM
   path. Credit pumping can deliver a disconnect, so permission is checked
   again immediately before the FIFO write.
5. Poll 20 times with 10 ms delays, then check identity/security again.
6. Bring the interface DOWN, explicitly delete every key whose SET was
   attempted, clear the RSN override and restore policy/events. Erase all
   private keys, group counters, plaintext and transport payload buffers.

This is a **bounded hardware test**. It ends DOWN even on success and provides
no persistent authorized port, DHCP, IP, DNS or ping. An acknowledged key SET
is not key readback or proof of encrypted traffic. A completed M4 SDIO write
is not an over-air ACK or proof that the AP accepted M4. Post-M4 identity is
an additional observation, not a completed Internet connection.

The key encoder uses explicit byte offsets in a 164-byte firmware ABI, avoiding
host structure-layout assumptions. Pairwise index 0 binds to the selected AP
MAC. Group/management keys use their authenticated IDs, an empty peer address
and firmware primary-key flags. Six-byte receive counters are packed into the
firmware low/high IV fields. The firmware ABI represents both CCMP and the
BIP-CMAC management key with algorithm 4, distinguished by key scope/index.
The definitions are documented in primary
[firmware types](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil_types.h),
[key configuration](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c)
and [algorithm constants](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/net/wireless/broadcom/brcm80211/include/brcmu_wifi.h).
OurOS supplies its own encoder and scoped journal.

## Failure handling and limits

The journal contains only public identifiers, acknowledgement masks and status
codes. It records an attempt before issuing SET, since failed transport can
leave firmware state uncertain. Cleanup deletes attempted entries in reverse
order, including a partially failed SET. Deletion is attempted after DOWN even
if DOWN itself failed. Unconfirmed deletion, DOWN or restoration disables further
transport use; reboot before retrying. The log preserves the first deletion
failure. It does not infer that DOWN automatically erased the key table.

No secret-key GET is implemented. The typed key transport is separate from the
generic IOVAR allowlist and erases shared command/reply buffers on completion,
including errors and echoed SET replies. No keys or packet counters are printed,
saved to the pendrive, embedded in the kernel, or placed in public diagnostics.
The password remains confined to the hidden local prompt.

Keys install once per attempt. An identical M3 received during installation is
observed without reinstalling keys. Different M3 data or replay state fails
closed. This revision has no M4 retransmission, group rekey or persistent-link
state machine. Those must be implemented before keeping the link active.
The live profile remains SAE hunting-and-pecking, CCMP-128 and required PMF;
H2E M2/RSNXE binding remains separate work. There is no WPA2 fallback.

## Pi test

Flash the complete [image](../image_files/OurOS-Pi5-header-experimental-sd.img),
or extract it from the [ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip),
to the USB pendrive and power-cycle the Pi. Keep the existing CP2102/header
console at 115200 8N1, no flow control. In PuTTY, force Local echo and Local
line editing off. Run these separately, waiting for each prompt:

```text
keytest
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
wifiinfo
storagetest
pagetest
uptime
```

Enter the password only at the hidden prompt. Skip the other join/auth probes
in this boot. Long P256/H2E/SAE self-tests are unnecessary for this run.

Expected new markers, pending physical verification:

```text
Association/EAPOL probe: ok phase=9
Firmware key installation: ok phase=3 issued_mask=...7 ack_mask=...7 status=...0
M4_SDIO_write=completed (not air ACK)
Post-M4 identity=verified settle_polls=20
Firmware key cleanup=acknowledged clear_mask=...7
private_keys_erased=yes
Probe ended DOWN.
```

Bit 0 of the masks is PTK, bit 1 GTK and bit 2 IGTK. The actual output prints
full hexadecimal values. These are intended markers, not a claimed new Pi pass.
Share `wifiassoc` through `wifiinfo` plus storage/page/uptime output, or the full
transcript. Capture every install/delete status if the test stops.

## Validation

Current image: `image_files/OurOS-Pi5-header-experimental-sd.img`
(134,217,728 bytes), with the verified ZIP alongside it (765,059 bytes).
Packaged kernel: 285,100 bytes. SHA256 of the full image:
`f0fb063f528652a1b9a496d310159ca1d42ceb39f02efd082a97493289ba2501`.
The kernel/BSS/stack ends at `0x1f8000`, below the firmware DTB at `0x200000`;
the linker overlap assertion passes. Earlier complete images and ZIPs remain
as dated files directly in `image_files`.

Native tests compare the encoder with an independent ABI layout, exercise the
real guarded CDC exchange with echoed key payloads, check shared-buffer erasure
after success/failure, and test partial install/delete journals. The full
SAE/association model covers three key acknowledgements, exact M4 MIC/framing,
pre/post-M4 identity/security, deauth, partial rejection, failed M4, duplicate M3
without reinstall, failed deletion and 142 per-operation failures. Native tests
and QEMU cannot prove the firmware's hardware key behavior; that requires the
new Pi capture.

Completed checks for this image: the full native Wi-Fi suite, then targeted
key/M4 transport checks after expanding sensitive-buffer erasure; QEMU
Cortex-A53/128 MiB and Cortex-A76/512 MiB monitor, memory, RAMFS, FAT/storage
and crypto regressions; fault handling; and an EL2 USB BOT/FAT/firmware-file run
with ring wraps and a host-matched 512-sector hash. Read-only image hashes stay
unchanged. Image corruption checks reject a bad MBR, FAT mirror, kernel payload
and console profile. ZIP CRC and independently decompressed SHA256 checks pass.
The new physical key-install/live-M4 stage remains untested.
