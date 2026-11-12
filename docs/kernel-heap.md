# Kernel heap and smaller RAMFS buffers

**Pi heap milestone verified:** the [latest capture](../proofs/pi5-heap-page-reclamation-2026-11-12.txt)
passes repeated heap tests, allocation/free and expected invalid-free rejection,
the two-page seed-file baseline and PMM counter restoration. File-edit sharing
tests for this allocator have native/QEMU coverage; the capture shows seeded
files via `fsinfo`, not the full edit/storage/radio regression. The new image
and next test are [MMU protection](mmu.md). Measurements below identify the
previous heap image.

The first Wi-Fi milestone is verified on the Pi: WPA3 authentication, protected
data keys, DHCP, ARP, DNS, hostname/numeric Internet ping and explicit disconnect
all pass. The [latest capture](../proofs/pi5-dns-hostname-ping-clean-disconnect-2026-11-12.txt)
shows two DNS answers, three replies each from `www.google.com` and `1.1.1.1`,
and acknowledged deauthorization, key deletion and policy restoration. The
brief disconnect at the end is the user's `disconnect` command.

This is sufficient to move on to another OS subsystem. Wi-Fi remains an
experimental foreground session: group rekey, automatic reconnect, DHCP
renewal and background operation still need future work. It is not a complete
general-purpose network stack or a package manager.

This phase adds the **kernel heap** and puts RAMFS file contents on it. A page
allocator hands out 4096 bytes at a time; a heap lets kernel components request
smaller buffers with independent ownership. These are kernel allocations in
the current identity-addressed RAM; no virtual memory or process isolation is
enabled by this change.

## What changed

The new `kmalloc`, `kcalloc`, `krealloc` and `kfree` interfaces support 1..4096
bytes per allocation with 16-byte alignment. Requests round up to classes
16/32/64/128/256/512/1024/2048/4096. Each class page comes from the existing PMM,
and slots in a page belong to separate allocations. Ownership bitmaps and
page records are in bounded kernel metadata, outside client buffers.

No pages are taken when the heap is initialized. New allocations are zeroed
through their rounded capacity. Freed buffers are wiped; the last free in a
page returns the cleared page to the PMM. A limit of 128 owned pages bounds
heap backing to 512 KiB for this first implementation. It does not limit the
installed/PMM-managed RAM. Metadata is separately part of kernel BSS.

Invalid/interior/double/foreign pointers are rejected by metadata lookup
without reading the supplied address. `kfree(NULL)` succeeds. A stale pointer
whose address has since been reallocated cannot be distinguished from the new
pointer; callers must still follow C ownership rules. No use-after-free or
buffer-overrun protection is claimed. This heap is not synchronized for IRQ
handlers or multiple CPUs, and is not an allocator for physically contiguous
multi-page or device DMA buffers.

`kcalloc` checks multiplication bounds. `krealloc` preserves data on growth,
allocates before freeing, leaves the old allocation valid if allocation fails,
and retains the original class when shrinking. Growth's new spare capacity is
zeroed. `krealloc(p,0)` frees; `krealloc(NULL,n)` allocates. A broken PMM returning
a duplicate/misaligned page or rejecting a valid page release poisons the heap
and stops further heap access; recovery is not attempted.

RAMFS uses these buffers for every nonempty file. Empty files/directories need
no data allocation. Growing a file allocates/copies before replacing its old
buffer, so normal out-of-memory failure preserves the old file. Shrinking
reuses capacity, clears its tail and supports overwriting with a slice of the
same file. Removing/truncating a file frees its buffer. File bytes remain
volatile, and `/boot` remains read-only FAT32.

On a fresh boot the three nonempty seed files occupy **two pages** rather
than three: README has a 512-byte buffer; hostname and board each use 16 bytes
on the same class page. `fsinfo` reports 3 buffers, capacity 544 bytes and
2 distinct data pages. The PMM also reports 2 allocated pages before manual
allocations. Other buffers may share the same class pages; `heapinfo` counts
the whole heap, while `fsinfo` counts RAMFS buffers/pages. Files still have a
4096-byte limit and the static node table still has 64 entries.

The network console now redraws any partially typed command after asynchronous
DNS/ping output. For example, if `ne` was typed when a ping completed, the new
prompt displays `net> ne`; finish with `tinfo`. No input is discarded or
automatically submitted. With the previous image, retyping the entire command
after an invisible prefix could produce `nenetinfo` and an unknown-command
response. That was a console interaction issue, not a Wi-Fi failure.

## Test this image on the Pi

Extract the complete [ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip)
and flash its `.img` to the USB pendrive, or use the local full
[IMG](../image_files/OurOS-Pi5-header-experimental-sd.img). Earlier full images
and ZIPs are preserved directly in `image_files`. Use the existing CP2102
header UART, 115200 8N1, no flow control; force PuTTY local echo/editing off.

First test the heap at the **normal `>` prompt**. Wi-Fi setup is not needed
for these checks:

```text
meminfo
heapinfo
heaptest
heapalloc 17
heapalloc 33
heapinfo
heapfree 0
heapfree 0
heapfree 1
heaptest
heapinfo
pagetest
```

Expect `Kernel heap ready` at boot and `Kernel heap: ready` in `heapinfo`.
Both heap tests should report PASS and restore live buffers/PMM page counts.
The first allocation rounds to 32 bytes, the second to 64. The second
`heapfree 0` should say `Invalid or unallocated heap slot.` At the end, heap
live pages/objects/capacity should return to **2 / 3 / 544**; cumulative and
peak counters continue increasing. The deliberate invalid-free case in
`heaptest` also increments `rejected_frees`.

Then check file edits and reclamation:

```text
fsinfo
mkdir /tmp/heapdemo
write /tmp/heapdemo/a.txt hello
write /tmp/heapdemo/b.txt world
cat /tmp/heapdemo/a.txt
cat /tmp/heapdemo/b.txt
fsinfo
heapinfo
rm /tmp/heapdemo/a.txt
rm /tmp/heapdemo/b.txt
rmdir /tmp/heapdemo
fsinfo
heapinfo
meminfo
storageinit
cat /boot/CONFIG.TXT
storagetest
pagetest
uptime
```

The two five-byte files should share the existing 16-byte class page: 5 heap
objects, still 2 pages, capacity 576 bytes. After removal the baseline should
return. `/boot` is read-only; none of these RAMFS operations persist on the
pendrive. Do not use `usbtest`/`usbenum` before `storageinit` on this boot.

To regress Wi-Fi in this image, continue with the bring-up sequence in
[wifi-dns.md](wifi-dns.md#image-and-fresh-boot), beginning at `wififiles` after
the successful storage mount. At `net>` run `nslookup www.google.com`,
`ping www.google.com`, `ping 1.1.1.1`, `netinfo`, then `disconnect`, waiting for
each result. Heap/filesystem commands run at the normal `>` prompt, not inside
`net>`. No credentials are embedded/saved and no network session is restarted
without a fresh boot.

Share the heap/file/storage test output and any errors. The long radio
bring-up transcript is only needed if a network step fails or changes.

## Verification and next OS work

The native heap tests exercise all allocation sizes, class packing and holes,
unaligned/inaccessible/foreign/double frees, calloc overflow, realloc failure
atomicity, metadata/PMM exhaustion and recovery, zeroing and full page wiping.
40,000 mixed operations check live data and overlap against a separate model,
with a guard page after every mock physical page. PMM release, duplicate-page
and misalignment failures are checked in separate processes.

RAMFS tests exercise file edits and binary/aliased buffers, 32 short files
sharing class pages, neighbor preservation during alternating deletes, tail
erasure, failed growth/new-file allocation and initialization rollback.
Physical-page tests and storage/FAT/VFS tests remain required. Network console
models check that partial input survives both DNS and ping completion, as well
as the existing network/cleanup paths. Hardware validation of this new
heap/RAMFS implementation is still required.

The next core stages are virtual-memory mappings/protection and correct device
memory handling, interrupt-controller/timer support, scheduling/context
switches, then EL0 processes and system calls. Executable loading, a richer
VFS, persistent filesystem writes/recovery and applications build on those
foundations. Networking maintenance (renewal/rekey/reconnect and background
service) and TCP/download support remain scheduled work, not completed features.
The updated [architecture](architecture.md) tracks that order.

## Packaged image

The full disk is **134217728 bytes** (128 MiB), ZIP **794988 bytes**, and
kernel `OUROS.IMG` **339388 bytes**. Full-disk SHA256:

```text
9fdee2862b4c89d93b2356c4b0a436b751f66a9da8b3cab4440e2b4d5a710271
```

Kernel/BSS/stack ends at `0x106000`, below the unchanged DTB at `0x200000`.
The external PMM bitmap remains separately reserved. The previous DNS image
and ZIP are saved as `OurOS-Pi5-header-experimental-sd-20261112-200408-127.img`
and `OurOS-Pi5-header-experimental-sd-20261112-200410-668.zip` in `image_files`.
Their full-disk hash is the `ef13ad...` hash recorded in the DNS phase guide.

Native memory/heap checks pass (`build/phase-heap-memory.log`). RAMFS tests
including shared-buffer isolation pass (`scripts/test-ramfs.ps1`). Real-image
FAT/VFS/SDHCI tests pass (`build/phase-heap-storage.log`). The complete native
Wi-Fi suite, including partial-input redraw and full authentication/session
cleanup models, passes (`build/wifi-tests/phase-heap-wifi-suite.log`).
The package verifies MBR/FAT/configuration, every extracted file and the
independently decompressed ZIP/disk hash (`build/phase-heap-image.log`). ZIP
integrity passes; MBR/FAT/kernel corruption and console-profile mismatch are
rejected, with the original disk unchanged (`build/phase-heap-image-negative.log`).

Cortex-A53/A76 monitor and exception checks pass (`build/phase-heap-qemu.log`).
They run the heap self-test twice, verify allocation/free accounting and the
new two-page RAMFS baseline, then exercise filesystem and read-only storage.
EL2-to-EL1 USB2 BOT/FAT/firmware-file checks, 512-sector host-matched hash,
all DMA ring wraps, repeated mounts and heap tests pass
(`build/phase-heap-usb-qemu.log`). Both scripts confirm unchanged disk hashes.
Generated object files, known temporary directories and test disk copies are
cleaned afterward; current images, dated backups, ELF/maps and diagnostic
logs are retained. No physical device was written and no commit/push was made.
