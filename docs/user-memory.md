# Private dynamic application pages

Read the largest available virtual run and failure counters with the
pressure query. [Memory pressure](memory-pressure.md)

The current kernel supports resizing a live page allocation in place. To
test the page syscall and allocator together, run `run /boot/HEAPCHK.ELF resize` in
the serial shell. Expect the PASS message and `child exit=0`. The update
pairs the kernel with the rebuilt allocator applications. The Pi run passed
with child exit 0. [Pi capture](../proofs/pi5-heap-resize-2026-11-21.txt) · [Boot update guide](boot-updates-tharun_verified.md)

The Pi's [latest profile capture](../proofs/pi5-session-profile-2026-11-15.txt)
verifies expansion, profile save and automatic loading in a new shell session
on the same boot. A power cycle is not shown. The preceding tests are accepted;
this phase adds private dynamic memory for EL0 applications. The subsequent
[Pi capture](../proofs/pi5-user-memory-2026-11-15.txt) passes the single-process
MEMCHK test and MEMDEMO, with all 20 explicit allocations/frees balanced and
restored resources. Broader lifecycle/concurrent and reboot hardware cases
still need their own evidence.

Applications can now request zeroed memory after loading, use it as normal
read/write storage or a checked syscall buffer, then free it. Each process has
a separate **64 KiB arena: sixteen 4096-byte pages**. Executable pages and the
existing four-page stack are separate and do not consume this quota. Dynamic
pages are readable/writable at EL0, non-executable at EL0/EL1, and unavailable
to other processes. Their virtual addresses may match in different processes;
the backing pages and contents remain private.

This phase is the page-allocation foundation. The subsequent
[byte allocator](user-allocator.md) adds malloc/calloc/realloc/free in user
space without changing these system calls. This page API does not include
shared memory, file mappings, demand paging, swap, copy-on-write or a general
POSIX mmap API. Filesystem and ELF image-size bounds are unchanged.

## Try an application

After the established storage/MMU/timer setup, run at the kernel `>` prompt:

```text
appterm 120 /boot/MEMCHK.ELF
```

Wait for PASS and code 0. The test needs no scratch files and performs no
persistent writes. It checks zeroed allocation/reuse, quota and fragmentation,
invalid frees, cross-page syscall buffers, stale unmapped-buffer rejection,
timer scheduling and resource restoration.

For a small working-buffer example, enter `appshell`, then:

```text
run memdemo
```

Expected output includes:

```text
Application page arena: 64 KiB; live pages=0
Working buffer: 16 KiB; live pages=4; checksum=2088960
Released buffer; live pages=0
```

This allocates four pages, verifies zero contents, fills the whole buffer,
computes a checksum and frees it. It can also be installed on `/data`, used
with `runio`, or connected to another program through the existing pipeline.
When PATH is customized, use `run /boot/MEMDEMO.ELF` explicitly.

These reports describe different owners of memory:

| Interface | Meaning |
| --- | --- |
| Monitor `meminfo` | Physical RAM discovery and kernel page allocator totals |
| Monitor `heapinfo` | EL1 kernel heap objects/pages |
| `uos_memory_info` / MEMDEMO | The calling EL0 process's dynamic page arena |

A 64 KiB application quota does not describe the Pi's total RAM capacity.

## Allocation and ABI

The page-allocation and resize calls use the following ABI.

| Call | Arguments/result |
| --- | --- |
| `ALLOC_PAGES=37` | x0: page count 1..16. Returns a page-aligned positive virtual address, ENOMEM (-13) when no contiguous arena run or backing pages are available, or ERROR for invalid counts/other recoverable mapping errors. |
| `FREE_PAGES=38` | x0: exact base of one live allocation. Frees its entire run. Returns 0 or ERROR; NULL, interior/unaligned addresses, immediate double frees and addresses outside this process's owned allocations are refused. |
| `MEMORY_INFO=39` | x0: writable 48-byte `uos_memory_info`. Returns 0 or EFAULT for invalid output ranges. Fields are page_bytes, limit_pages, live_pages, peak_pages, allocations and free_calls, all uint64_t. |
| `RESIZE_PAGES=44` | x0: exact allocation base. x1: new page count 1..16. Returns 0 on success, including the same size, ENOMEM when an adjacent run or backing pages are unavailable, or ERROR for invalid parameters or a rejected mapping update. |

Runtime wrappers are `uos_alloc_pages`, `uos_free_pages`, `uos_memory_info`
and `uos_resize_pages`.
For example:

```c
int64_t address = uos_alloc_pages(4);
if (address < 0) return 1;
unsigned char *buffer = (void *)(uintptr_t)address;
buffer[0] = 42;
return uos_free_pages(buffer) != 0;
```

Runs are contiguous in virtual address space; physical contiguity is not
promised. Allocation uses free adjacent arena slots. Enough total free pages
can still be fragmented into runs smaller than a request; ENOMEM then leaves
existing data/mappings unchanged. Freed pages become available for reuse.

Resize growth preserves the base address and the existing prefix, and maps
new pages as zeroed RW/NX memory. Shrink unmaps the tail and returns those
pages to the physical allocator. Allocation or table publication failure
preserves the original mappings and contents. `MEMORY_INFO` reports the
new live and peak page counts. The allocation and free counters count their
respective calls, so a resize does not increment either counter.
Memory is zeroed before allocation and wiped before release.

These are ordinary virtual pointers, not generation-tagged handles. Accessing
a freed address faults while it remains unmapped, but a later allocation may
reuse that address. An old pointer with the same numeric value cannot then be
distinguished from the new pointer. Pointer values from another process do
not identify its physical pages; a process can only free its own live run at
that numeric address. These rules differ from the token-based file API.

The current arena layout, relative to `USER_VA_BASE=0x2000000000`, is:

| Offset range | Mapping |
| --- | --- |
| 0x00000..0x07fff | Up to eight static ELF image pages, existing RX/RO/RW rules |
| 0x08000..0x0ffff | Unmapped gap/stack guard |
| 0x10000..0x13fff | Four existing RW/NX stack pages |
| 0x14000..0x1ffff | Unmapped gap/lower heap guard |
| 0x20000..0x2ffff | Sixteen demand-allocated RW/NX arena pages |
| 0x30000 onward | Unmapped upper heap guard/outside the supported arena |

All user pointers, including dynamic buffers crossing page boundaries, still
pass the existing checked user walker. A failed range check happens before
file cursor movement or copying. Executable heap mappings are never offered.

## Lifetime and mapping updates

Spawn starts with an empty private arena; it does not copy the parent's dynamic
pages. Successful exec replaces the image/stack and frees the old arena, while
retaining the existing PID/cwd/environment/handle contract. Failed exec leaves
the old arena and contents intact. Exit, contained faults, timeout, Ctrl+C and
batch teardown reclaim all owned mappings, including allocations the program
did not explicitly free.

Mapping changes run under the canonical kernel root while the user's root is
inactive, with CPU IRQs masked. New backing pages and replacement tables are
prepared before publication. A failed allocation or table publication rolls
back temporary pages without changing the old mappings. Free first publishes
the removal, then wipes/releases the old pages. The root-switch path supplies
the required barriers/TLB invalidation before returning to EL0. This depends
on the existing single-CPU, non-preemptible-kernel design; SMP requires a
separate synchronization and shootdown design.

An internal PMM release failure marks the arena failed, retains unreleased
pages in the owner's cleanup list and contains the process as an internal
fault. Cleanup failure is reported; it is not presented as successful resource
restoration. This is distinct from an ordinary application quota/ENOMEM error.

`appinfo` adds heap_live_pages, heap_peak_pages, heap_allocations and
heap_free_calls. Live pages become zero at teardown. Peak and successful
allocation/free-call counters follow the process across exec; free_calls counts
explicit calls, so automatic teardown can reclaim memory without incrementing
it. Stats of a child that was collected are not a system-wide history.

## Verification

The resize tests inject a failure at each of the five new pages in a
ten-to-fifteen-page growth and check both grow and shrink table publication
failures. The emulator's `HEAPCHK.ELF resize` exercises the new syscall
and the byte allocator through the same user entry point as the Pi. Run it
on both CPU profiles with `scripts/test-shell-qemu.ps1 -UserAllocator`.

`test-user-heap.ps1` runs the actual allocation manager with a PMM model and
real private-table builder. It checks all 136 partial allocation failures,
32 publication failures, release failures, all 12 failure positions when
expanding an existing four-page allocation, zero/reuse, fragmentation,
invalid frees and 3000 operations against independent ownership/occupancy
state. Every live byte and final allocation count is checked. User-MMU tests
also check all dynamic entries, exact permissions, guards and damaged
executable-heap descriptors.

`MEMCHK.ELF` checks actual SVCs and page accesses. Its parent mode repeatedly
starts children that leak pages, exec, fail exec, fault, or time out, then
verifies child results/slot reuse. The emulator also runs all three slots with
PID-specific patterns at identical virtual addresses, tests read-after-free,
NX and both guards as contained faults, and verifies Ctrl+C reclamation.
`MEMDEMO.ELF` covers working buffers, pipes, redirected file output and installed
application execution after a fresh emulator boot.

`test-shell-qemu.ps1 -UserMemory` uses regular scratch disks on A53/EL1 and
A76/EL2. Logs are `build/phase-usermem-*.log`. Source image and boot/unused
sectors must remain unchanged, with owned handles zero and PMM/kernel-heap
counts restored. The supplied single-process Pi MEMCHK/MEMDEMO now pass;
the wider lifecycle and concurrent cases still need hardware evidence.
Images and verified full-image ZIPs remain flat in `image_files`, preserving
earlier deliveries. No physical medium is written by the automated tests.

All checks above passed on both emulator profiles. The earlier application
suite also passes against this kernel: original syscall pointer checks,
RX/NX/kernel isolation, preemption, task I/O, timeout/owner cleanup, metadata,
sleep/wake and installed-ELF execution after fresh boots. All 47 delivered
ELFs, 25000 ELF parser mutations and eight disk corruption/profile checks pass.

The original private-memory release used a 134217728-byte image and an
880646-byte ZIP.
Image SHA-256:
`d45b5d37bd6d61bc7069ea9560f1ffc59256eb25447b1b7079a5e22b4b87150e`.
Kernel: 446196 bytes, reserved end `0x1d0000` below firmware DTB `0x200000`.
SHELL.ELF: 8094 bytes; MEMCHK.ELF: 4408; MEMDEMO.ELF: 1160. The full image
starts with empty v3 `/data` and public DEMO.BAK. Preserve any existing files,
including `/data/session.env`, using the established export/restore workflow
before reflashing if they need to be retained.
