# USB writes and persistent files

The [new Pi write/sync capture](../proofs/pi5-usb-data-write-sync-2026-11-13.txt)
confirms that the endpoint-stall repair below works on the tested SanDisk:
`/data` mounts with WCE=0, accepts a write/read and sync, and advances to
generation 2. There is no second boot banner, so actual power-cycle persistence
remains unconfirmed. The current next phase is [owned file handles and task
I/O](file-handles.md); the previous image/phase details below are historical.

## Pi endpoint-stall fix (2026-11-13)

The [new Pi diagnostic excerpt](../proofs/pi5-usb-data-stall-2026-11-13.txt)
establishes the failing sequence: CACHE10 rejected with `05/20/00`, then
MODE SENSE6 (`0x1a`) reaches status stage 3 and the bulk-IN endpoint stalls
(xHCI completion 6). The previous transport treated that recoverable stall
as a fatal controller error. The data partition itself was read and validated;
the failure occurred at the cache-policy check. No data file writes were accepted.

The current image implements the USB BOT recovery for a data/status stall:
Reset Endpoint (TSP=0), `CLEAR_FEATURE(ENDPOINT_HALT)` addressed to the affected
device endpoint, then Set TR Dequeue Pointer past the stalled transfer with
the correct cycle bit. A status read retries once after clearing its stall;
data or WRITE commands are never replayed. CBW stalls, a repeated status
stall, invalid completions, timeouts or failed recovery still stop the session.
Host recovery is bounded to 32 cleared stalls per boot. These sequences follow
the [USB BOT specification, sections 5.3.3 and 6.7](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf)
and [xHCI section 4.6.8](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf).

Current cache-policy queries now try MODE SENSE6 with/without DBD, all-pages,
and MODE SENSE10 page-specific/all-pages alternatives, only after a valid
unsupported-command response or a valid response lacking the caching page.
`cache_query=1..5` identifies that order; `stalls_recovered` counts successful
pipe clears. The successful query form is reused at later syncs, with its
current WCE re-read each time. A complete current caching page must explicitly
report WCE=0. Missing/malformed pages, WCE=1 or write protection still refuse
writes. This repair does not assume that all Cruzer drives have the same cache
policy. Actual persistence still needs the Pi retest below.

Native tests replay the short-data/status-stall sequence, data stalls, CSW
retry limits, clear failures, command rejection and MODE6/10 alternatives.
A register/DMA model exercises the production xHCI transport's reset,
device clear request and dequeue commands for both bulk directions and at
ring wrap, plus successful subsequent transfers and cleanup failures.

## Earlier mount diagnostics

The first hardware test of `/data` failed with `storage I/O error` after
`/boot` mounted with the new 258048-sector layout. No `/data` file writes
were accepted. The old `RAM filesystem unavailable` message meant the data
mount was unavailable; it did not demonstrate a RAM allocator failure.
That initial capture lacked SCSI sense and failed command details. The next
capture above established the status-stall bug. Persistence remains unverified
on the physical Pi until writes and a power-cycle test pass.

The follow-up image records the mount phase, failed read LBA and block result,
plus BOT opcode/stage/CSW/sense validity and xHCI completion. `datainit`
automatically prints these on failure and retains the first mount failure
across repeated attempts; reboot before retrying. Stage 1/2/3/4 means
CBW/data/CSW/completed command. Opcode `0x28` is READ10, `0x35` CACHE10,
and `0x1a` MODE SENSE6. A failed mount never formats the disk.

There is also a bounded compatibility path for devices rejecting CACHE10
with current ILLEGAL REQUEST sense `05/20/00` or `05/24/00`. A read-only
MODE SENSE6 of the current caching page must explicitly report **WCE=0**
and no write protection. Only then can synchronous WRITE completion replace
flush; the policy is re-read on every sync. Missing, truncated, unsupported,
write-protected or WCE=1 responses refuse the mount/updates. No vendor-ID
assumption, cache-disable command, or blanket ignored flush error is used.
Other I/O errors remain errors. See the [SBC-3 caching-page definition,
section 6.3.3](https://t10.org/ftp/t10/document.05/05-344r0.pdf): WCE=0 requires
successful writes to reach the medium before GOOD status is returned.

Flash the updated full image, cold boot, then run:

```text
storageinit
datainit
datainfo
storageinfo
```

If mounted, continue with the write/sync and power-cycle test below. If it
fails, stop and share this short output; repeating `storageinit` cannot
restart a failed USB session. This compatibility change is tested with
native device models; whether it resolves this particular drive still needs
the Pi retest. Native tests cover 26 cache/sense cases and all 70 mount I/O
failure positions, in addition to the 272 update-failure cases.

The [latest Pi capture](../proofs/pi5-cooperative-tasks-usb-2026-11-12.txt)
passes the cooperative task phase: both three-task runs exit correctly,
register/stack/heap data survive, each task sleeps/wakes four times, IRQs run
on private stacks, and USB/FAT reads and memory tests pass between task runs.
The two skipped timer periods mean late service was accounted for; there are
no unexpected interrupts or controller faults in that capture.

This phase adds USB2 BOT **WRITE(10)**, synchronous **SYNCHRONIZE CACHE(10)**,
bounded block write/flush callbacks, and an original small persistent filesystem
at `/data`. The new write/durability path needs its own physical Pi test.
It is a storage milestone before general multitasking applications.

## Flash and test on Pi

Extract the complete [ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip)
and flash its `.img` to your USB pendrive as before. The full local image is
`image_files/OurOS-Pi5-header-experimental-sd.img`. This revision adds a second
partition, so copying only the kernel onto an older image will not create `/data`.
Previous full images and ZIPs remain in `image_files`; build tools write only
regular files and do not flash a physical drive or publish to GitHub.

At the normal `>` prompt, run one command at a time:

```text
mmuinit
irqinit
timerstart
schedtest
storageinit
datainit
datainfo
ls /data
write /data/hello.txt hello-after-reboot
cat /data/hello.txt
touch /data/empty
ls /data
sync
datainfo
cat /boot/CONFIG.TXT
storagetest
schedtest
taskinfo
timertest
heaptest
pagetest
timerstop
```

Expect `/data: OurOS snapshot v1`, `writes=synchronous` and `generation=1`
initially. Writing `hello.txt` commits generation 2; creating `empty` commits
generation 3. Each successful `write`, `touch` creating a file, or `rm` commits
and flushes synchronously before the prompt returns. Touching an existing file
preserves its contents and does not advance the generation. `sync` explicitly
flushes the device again.

After the final command returns, power-cycle the Pi **without reflashing**.
Run:

```text
storageinit
datainit
datainfo
ls /data
cat /data/hello.txt
cat /data/empty
```

`hello-after-reboot` and the empty file should still exist. `/tmp`, `/home`
and other RAM root files still disappear after reboot. You can use `cd /data`
and relative `ls`, `cat`, `write`, `touch`, `rm` commands. Test replacement and
deletion with `write /data/hello.txt updated` and `rm /data/empty`, followed by
another power-cycle and check. Share both boot captures, especially `datainfo`
and any `storageinfo` errors. The initial test does not need Wi-Fi credentials
or the slow crypto self-tests.

**Reflashing an image replaces its data partition too.** Persistence here
means across boots of that installed image, not automatic migration to a new
image. Keep important contents elsewhere before flashing another revision.

## Layout and scope

The disk remains 128 MiB with an MBR:

| Partition | Start / sectors | Use |
| --- | --- | --- |
| FAT32, type `0x0c` | LBA 2048 / 258048 | 126 MiB boot files, read-only to the kernel |
| OurOS data v1, type `0xda` | LBA 260096 / 2048 | 1 MiB reserved persistent data area |

`/data` is an OurOS format, not FAT32 or ext4. Its first implementation stores
eight flat files, 31-byte names and at most 4096 bytes per file. It supports
listing, reading, whole-file replacement, touch and removal; it has no nested
directories, permissions, open handles, append or writable boot FAT metadata.
Only 133 sectors of the 1 MiB partition are used; the rest is reserved.
Windows tools may show the second partition as unknown; the boot FAT remains
independently readable. The SD handoff and QEMU virtio backend remain read-only;
the new write/flush path targets the supported USB2 BOT pendrive.

`storageinit` still mounts only read-only `/boot`. `datainit` checks a unique,
non-overlapping, in-range type-DA partition, the preformatted superblock,
snapshot headers/payloads and write/flush support before mounting `/data`.
There is no guest format or arbitrary raw-sector write command. Missing or
unrecognized data partitions are refused. A write or flush error stops further
data updates until reboot; `datainfo` shows the failed state and `storageinfo`
retains BOT/controller diagnostics. Reads of the previous in-memory snapshot
remain possible if the data update failed.

## Commit and recovery

Each bank contains a 512-byte header and 65-sector payload: a fixed directory
sector plus eight 4 KiB data slots. Header identity, bank number, generation,
length, SHA256(header fields) and SHA256(payload) are checked. Names, lengths,
duplicates, reserved bytes and unused data tails are validated too. The initial
image contains one empty committed bank at generation 1.

For each change, the driver leaves the current bank untouched and:

1. Invalidates the other bank's header and flushes.
2. Writes the complete new payload, flushes and reads back every sector.
3. Writes its commit header with the next generation, flushes and reads it back.
4. Publishes the new in-memory view only after those checks succeed.

Mount chooses the highest complete valid generation. A damaged newer bank can
fall back to the older complete one; two invalid banks or ambiguous equal
generations are refused. A failed update may have committed before a later I/O
error; after reboot, either complete old or complete new contents are valid
outcomes. There is no automatic retry of an ambiguous write and no modification
of MBR, FAT, boot files or the active snapshot during an update.

This ordering depends on the device honoring successful cache flushes and on
failures staying within addressed sectors. Native fault models and QEMU are
evidence for the implementation, not proof against every flash-controller or
power-loss failure. SHA256 detects accidental corruption; it is not encryption
or authenticated access. Old file contents may remain in the other snapshot.

## Verification and next phase

Native tests cover create/replace/remove/full-volume limits, exact content and
EOF handling, remount, partition and format corruption, missing write/flush,
generation overflow and fallback. Each of 136 update I/O boundaries is checked
in no-completion and torn-write/ambiguous-flush cache models (272 cases). Recovery must
select complete old/new contents and leave sectors outside the data banks alone.
The PowerShell formatter is independently mounted by the C reader.

The QEMU test writes only copies under `build/usb-tests`: three cold boots
check create, replacement and deletion, independent host SHA256/content checks,
unchanged boot/MBR/unused sectors, read-only media refusal, timer IRQs and
cooperative tasks. Package/image checks cover both partitions and independent
FAT extraction. Use `scripts/test-persist.ps1` and `scripts/test-persist-qemu.ps1`.

Next: file handles and offsets with explicit ownership, then task-safe I/O and
application loading. Cooperative kernel test workers are not yet independently
loaded applications. EL0, system calls, process memory and preemptive scheduling
remain in the [OS/driver checklist](os-status.md).

Transport references: [USB BOT specification](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf)
and [T10 SBC-3 working draft](https://t10.org/ftp/t10/document.05/05-344r0.pdf).
The data format and snapshot policy are original OurOS code.

## Packaged artifact

The current full disk is **134217728 bytes** (128 MiB), ZIP **818983 bytes**
and kernel `OUROS.IMG` **380636 bytes**. Full disk SHA256:

```text
d9f63b4f30825120cef547d379beb3feb656d0e675b0369f166a6941a968b659
```

The previous diagnostic image and ZIP are preserved as
`OurOS-Pi5-header-experimental-sd-20261113-010316-581.img` and
`OurOS-Pi5-header-experimental-sd-20261113-010319-578.zip`.
The first data-phase image and ZIP are preserved as
`OurOS-Pi5-header-experimental-sd-20261112-225357-531.img` and
`OurOS-Pi5-header-experimental-sd-20261112-225400-588.zip`.
The earlier task-verified full disk and ZIP are preserved as
`OurOS-Pi5-header-experimental-sd-20261112-222708-100.img` and
`OurOS-Pi5-header-experimental-sd-20261112-222711-117.zip` in `image_files`.
Reserved kernel/BSS/stack ends at `0x15a000`, below firmware DTB `0x200000`.

Stall-fix checks pass in `build/phase-data-stall-usb-native.log`,
`build/phase-data-stall-persistence-qemu.log` and
`build/phase-data-stall-image-negative.log`.
Detailed QEMU logs are `build/handoff-test/data-cortex-a53-off-*.log`,
`data-cortex-a76-on-*.log` and `data-mount-failure.log`. Both CPU profiles
passed three cold boots, independent committed-content/hash checks, read-only
media failure and unchanged boot/unused sectors, alongside task/IRQ/MMU checks.
The separate corrupt-superblock run retains the first mount diagnostic across
retries, refuses file writes and still reads `/boot/CONFIG.TXT`. Source image
hash stayed unchanged. Image packaging independently verified both partitions,
boot FAT/files and the complete ZIP's decompressed SHA256.

Original data-phase test logs (before the follow-up diagnostics/cache check):

- `build/phase-data-final-native.log`: snapshot limits, remount, corruption,
  272 fault cases, actual disk seed and independent PowerShell-format checks.
- `build/phase-data-final-usb-native.log`: USB/DMA regressions, exact CBW/CDB
  write direction and data, readback, flush and failed-transfer handling.
- `build/phase-data-final-storage.log`, `phase-data-ramfs.log`: FAT/VFS/SDHCI
  regression and RAM root/heap/path/namespace tests.
- `build/phase-data-persistence-qemu.log`: A53/EL1 and A76/EL2 create/reopen/
  replace/remove over three boots, independent disk checks, read-only media
  failure and task/IRQ/MMU/heap regression. Source image unchanged.
- `build/phase-data-tasks-handoff.log`: EL1/EL2 task context/register/stack
  checks, refusal/restart and fatal-fault regression.
- `build/phase-data-monitor-qemu.log`: complete A53/A76 monitor, crypto,
  RAMFS/heap/pages/MMU/task/IRQ and read-only virtio/FAT tests; disk hash unchanged.
- `build/phase-data-image.log`, `phase-data-image-negative.log`: boot package,
  both partitions, independent FAT extraction, ZIP/hash and corruption of
  MBR/FAT/kernel/data extent/data payload plus wrong-profile refusal.
