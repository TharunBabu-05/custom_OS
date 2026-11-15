# USB storage lifetime recovery and update diagnostics

The [new physical capture](../proofs/pi5-usb-v2-capacity-2026-11-15.txt) passes
usbcheck/DATACAP with 152 recovered stalls and USB online, generation 51 and two
valid banks. The previous failure is resolved for this test. The current
[backup/restore phase](data-backup-restore.md) remains awaiting physical acceptance;
installed-shell execution and a true power-cycle test are still outstanding.

The [physical Pi capture](../proofs/pi5-data-v2-io-failure-2026-11-15.txt)
mounts v2 successfully, then DATACAP exits with code 3 during its sixteen
512-byte file writes. Installation, subsequent `/boot` reads and sync fail too.
Those later errors follow the transport shutdown; no installed shell was created.
This is a real storage failure, separate from the earlier prompt/path mistakes.

The driver contained a **32-recovery lifetime limit**. Each snapshot update
uses three flush/cache-policy checks. A drive that stalls its MODE SENSE status
transfer can require a valid halt recovery every time. The old driver eventually
rejects another legitimate recovery and shuts down USB. The larger v2 volume
made repeated writes expose this sooner. A new device-model test reproduces
the cutoff on update 11 (stall 33), matching the capture's failure stage. The
capture lacks post-failure `storageinfo`, so this remains the best supported
explanation, rather than a measurement of that Pi's exact failing completion.

The fix removes the lifetime cutoff and makes command and endpoint-zero rings
reusable with Link TRBs and producer cycle changes. Otherwise recovery would
eventually exhaust those formerly one-shot rings. Control TDs stay contiguous,
with non-interrupting Transfer No-ops padding an unused segment tail. TRB
ownership is published after parameters; control Setup is published after its
remaining stages. One synchronous request remains outstanding at a time.

Each recovery still has bounded command/event waits and matches the exact
pointer, endpoint and slot. BOT still retries a stalled CSW once; a repeated
stall, invalid status/tag, reset failure or timeout fails closed. WRITE10 is
never replayed. Flush policy still requires a successful SYNCHRONIZE CACHE or
a fresh complete CURRENT caching page explicitly reporting WCE=0.

This follows the [USB BOT specification, section 5.3.3 and Figure 2](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf)
and [xHCI ring/cycle and Link TRB rules, sections 4.9.2 and 6.4.4.1](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf).

## Retained diagnostics

`storageinfo` now includes completed halt clears and command/control ring wrap
counts. It correctly describes USB as boot-read-only/data-opt-in rather than
calling the entire transport read-only.

`usbcheck` is a new read-only runtime check. After `storageinit`, it issues 300
No-op commands and device-descriptor reads, checks the same VID/PID/USB version
and packet size, and compares sector zero before/after. This exercises actual
command/control ring wrap without relying on a device to stall. It performs no
disk write, flush, format or device reconfiguration. A failed check shuts down
the transport and asks for diagnostics/reboot; total time and individual waits
are bounded. It does not establish durability of existing data.

`datainfo` retains the first failed update phase, block result, LBA and a
separate readback-mismatch flag. Phases distinguish header invalidation,
payload writing, each flush, payload readback, committed header and header
readback. Failed updates keep the old RAM view and refuse more writes until
reboot. No formatting or automatic retry is introduced. An application session
ending after a data failure automatically prints both diagnostic reports.

## Retest on the Pi

Use the newest full image/ZIP in `image_files`. Writing a full image replaces
its data partition; retain any files you need before reflashing. The original
images remain archived. No device is flashed by the build scripts.

On a fresh boot with the new empty v2 volume, run at the kernel `>` prompt:

```text
storageinit
usbcheck
datainit
mmuinit
irqinit
timerstart
appterm 300 /boot/DATACAP.ELF
storageinfo
datainfo
```

Wait for DATACAP's explicit PASS and `state=exited code=0`. The 300-second
session gives a slower physical drive more time for synchronous commits;
DATACAP refuses a nonempty volume. Stop at a failure and save the reports;
do not continue installation against an offline transport.

After PASS, run:

```text
appinstall /boot/SHELL.ELF /data/shell.elf
appterm 60 /data/shell.elf
```

The prompt becomes `user:/$`. Enter:

```text
sha256 /boot/SHELL.ELF
sha256 /data/shell.elf
runio - /data/large.bin /boot/DATAGEN.ELF
sha256 /data/large.bin
run /boot/HELLO.ELF
sync
exit
```

Shell hashes must match. The binary is 8192 bytes with SHA-256
`dc404a613fedaeb54034514bc6505f56b933caa5250299ba7d094377a51caa46`.
Finish at `>` with `fileinfo`, `heaptest`, `pagetest`, `storageinfo`,
`datainfo`, `timerstop`. Handles should be zero and memory checks must pass.
Reboot **without reflashing**, repeat storage/MMU/IRQ/timer setup and launch
`appterm 60 /data/shell.elf` again. Recheck hashes to establish real persistence.

## Software validation

`scripts/test-usb.ps1` includes an integration model exercising the production
xHCI/BOT/block/snapshot code together, not just isolated callbacks. The model
uses independent hardware cursors that follow Link/No-op TRBs, generates a
CSW stall on every cache query and verifies 160 v2 updates, 483 recoveries,
repeated command/control/bulk/event cycling, exact WRITE10 count (no replay),
remount and all 8192 file bytes. It also exercises 300 three-stage control
requests through ring boundaries. The pre-fix failure is recorded in
`build/phase-usb-lifetime-before.log`.

Existing timeout, mismatch, failed reset/control and repeated-CSW-stall tests
pass. Snapshot tests check every v1/v2 injected update failure's
phase/result/LBA and distinguish successful-but-corrupt readback from transport
errors; 272 v1 and 1044 v2 failure cases pass, as do owned-file cleanup/cursor
checks. QEMU A53/EL1 and A76/EL2 pass `usbcheck` before/after DATACAP, capacity,
installed-shell execution after a fresh boot, exact ELF/binary hashes and
independent bank/unchanged-region checks. Each profile exercises two command
ring wraps and seven control ring wraps. V1 compatibility also passes on both.
Build/image validation and QEMU checks are recorded in
`build/phase-usb-lifetime-*.log`. Software tests cannot establish the
new driver's physical timing or power-loss durability. Physical retest is pending.

The USB-fix build before the backup phase had a 134217728-byte full disk and
855743-byte verified ZIP (archived; see the backup guide for the current build).
SHA-256 of the extracted disk image:
`d0841d059578fa7c8b6349eb23365a8779afc18535dab3030a407086e4020ccb`.
The Pi kernel is 425716 bytes and its reserved end is `0x1be000`, below the
firmware DTB at `0x200000`. Previous complete images/ZIPs are retained flat in
`image_files`; the build cleanup removes known generated objects and scratch disks.

The next storage milestones are hardware acceptance, preservation/export and
larger/nested persistent files. General installation/packages and local UI
remain on the [roadmap](remaining-os-roadmap.md).
