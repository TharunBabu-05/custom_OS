# Byte allocation for applications

Applications now have original `uos_malloc`, `uos_calloc`, `uos_realloc` and
`uos_free` functions over the existing private-page system calls. Small objects
share pages; programs no longer have to reserve a whole page for every buffer.
The kernel and syscall ABI are unchanged in this phase.

The [supplied Pi capture](../proofs/pi5-user-memory-2026-11-15.txt) verifies the
preceding single-process MEMCHK run (20 allocations/frees, exit 0, live pages 0
and restored resources) and MEMDEMO (16 KiB buffer and child exit 0). It does
not show the three-process, exec/fault/timeout or physical-reboot cases. New
byte-allocator hardware acceptance is now recorded separately below.

The [new Pi capture](../proofs/pi5-user-allocator-2026-11-15.txt) passes the
default HEAPCHK and HEAPDEMO runs. Both exit with code 0, live pages 0 and
restored resources. HEAPCHK reaches the sixteen-page limit and balances 12
backing-run allocations/frees. HEAPDEMO packs 32 small buffers into one page,
grows to four pages, then balances all three backing-run allocations/frees.
This confirms these two application paths on Pi. The capture does not show
the parent-mode lifecycle suite, three concurrent allocators or a persistent
file read across a physical reboot; those remain separate evidence gaps.

## Update without reflashing

This section describes the allocator-stage delivery. The subsequent
[process-control update](process-control.md) includes a new kernel and its own
boot-update bundle. The allocator-only packager below is pinned to this older
kernel; the allocator API remains available in the later kernel.

Use [OurOS-byte-allocator-apps.zip](../build/apps/OurOS-byte-allocator-apps.zip).
Extract it on Windows. With the Pi powered off, connect its existing pendrive
to the PC and copy **HEAPCHK.ELF and HEAPDEMO.ELF** into the existing FAT boot
volume's root, beside CONFIG.TXT and OUROS.IMG. Safely eject it and boot the
Pi. This changes only the two applications and preserves the existing kernel,
partition layout and `/data` contents. `README.txt` and checksums are included.
The ZIP is an application update, not a flashable disk image.

At the kernel monitor, after a fresh boot:

```text
storageinit
mmuinit
irqinit
timerstart
appterm 120 /boot/HEAPCHK.ELF
appterm 120 /boot/HEAPDEMO.ELF
```

`datainit` is optional for these two tests. HEAPCHK should print PASS, exit
with code 0 and report restored resources. HEAPDEMO should print:

```text
Small buffers: requested=768 bytes; reserved_pages=1
After resize: requested=5744 bytes; reserved_pages=3
After calloc: requested=6000 bytes; reserved_pages=4
Released buffers; private_pages=0
```

You can also use `appshell`, then `run heapdemo` or `run heapchk`. No repeat of
the earlier MEMCHK procedure is needed. The longer lifecycle check is optional:
`appterm 120 /boot/HEAPCHK.ELF parent`. It deliberately creates contained child
faults and timeouts, then should print its own PASS line.

The application-only packager checks that the current kernel binary still has
the exact SHA-256 of the preceding Pi-tested MEMCHK-stage kernel:
`72852aa77b033b9f6330b0f93a8f9dbcbff8d9c864568db792d51d3946813065`.
The two ELFs statically contain the runtime; no library file or new syscall is
needed. Build the bundle with `scripts/package-user-allocator.ps1` after the
normal build. If the kernel changes later, compatibility must be reviewed.

## Programming interface

The API uses an explicit, initially zeroed context in caller-owned memory:

```c
#include "allocator.h"
struct uos_allocator heap = {0};
if (!uos_allocator_init(&heap)) { return 1; }
unsigned char *p = uos_calloc(&heap, 100, 24);
if (!p) { uos_allocator_destroy(&heap); return 2; }
void *larger = uos_realloc(&heap, p, 5000);
if (larger) { p = larger; } /* Failure keeps the old allocation. */
if (!uos_free(&heap, p)) { return 3; }
if (!uos_allocator_destroy(&heap)) { return 4; }
```

Link `user/runtime/allocator.c` and `allocator_svc.c` alongside the normal user
runtime. The context remains live until every allocation is released or the
context is destroyed. Do not copy an active context or reinitialize it.
Destroy can release outstanding objects; their pointers then become invalid.
Explicit contexts preserve the sample ELF policy of no writable globals.
These are prefixed OurOS functions, not a standard libc or binary-compatible
implementation of the standard library.

| Property | Contract |
| --- | --- |
| Alignment | 16 bytes for every returned pointer |
| Initial contents | Both malloc and calloc return zeroed requested bytes |
| Small allocations | Classes 16, 32, 64, 128, 256, 512, 1024 and 2048; a class page contains multiple slots |
| Larger allocations | Rounded to a contiguous virtual run of 4096-byte pages |
| Limits | 64 live objects, 16 groups per context; 16 backing pages/64 KiB per process, shared with raw page allocations and other contexts |
| Empty/invalid sizes | malloc(0), a zero-size calloc, calloc overflow or a request above 65536 return NULL |
| free | Returns true on success, including NULL; rejects untracked, interior, cross-context and immediate double-free pointers |
| realloc(NULL, n) | Same as malloc(n) |
| realloc(p, 0) | Attempts free and returns NULL; a provider release failure retains ownership, so inspect status or destroy |
| Resize in place | Keeps the address when capacity suffices; newly exposed bytes are zeroed and truncated logical bytes wiped |
| Moving resize | Acquires replacement capacity before releasing the old object; copies old requested bytes and zeroes growth |
| Failure | Returns NULL without losing the original allocation; moving resize needs spare pages, a spare record and a suitable virtual run |
| Reclamation | Last object in a group returns its backing run; shrinking alone retains that group's capacity |

`uos_allocator_stats` reports live objects, requested bytes, per-object capacity,
backing pages, peak pages and operation/failure counters. Metadata lives in the
application's stack/context and is not included in backing-page totals. A
successful destroy clears the context, including these statistics.

`uos_allocator_setup` also accepts a provider for model tests. Its allocation
must return a fresh, aligned, disjoint run. Failed release must leave ownership
and bytes intact; successful release wipes and relinquishes the run. Failed
moving-resize cleanup rolls back the candidate. If that cleanup also fails,
the context retains both runs and becomes poisoned; normal operations refuse,
and destroy can retry release. A partial destroy similarly retains failed runs.
The kernel teardown remains responsible for allocations leaked by a process.

All metadata and pointer checking here belong to the application, not a kernel
security boundary. Do not call raw `uos_free_pages` on allocator-owned pages.
Freed small slots may remain mapped while neighbors are live, and addresses
can be reused. A stale pointer whose address has been reallocated cannot be
distinguished from the new pointer. Kernel page isolation and RW/NX protection
continue to apply between processes. This runtime is not thread-safe; SMP,
larger virtual memory, shared mappings and a general libc remain separate work.

## Verification and delivery

`test-user-allocator.ps1` compiles the real allocator against an independent
page provider. It checks zeroing/alignment, shared pages, record/quota/overflow
limits, in-place and moving resize, exact-pointer rejection, allocation/release
failure and rollback, retryable destroy, and 6000 model operations. Every live
byte, requested-byte count and final page baseline is checked.

`test-shell-qemu.ps1 -UserAllocator` runs the actual EL0 applications on
Cortex-A53/EL1 and Cortex-A76/EL2. Coverage includes three simultaneous
PID-pattern checks, leaked child cleanup across exit/exec/fault/timeout,
failed-exec preservation, Ctrl+C, released-page faults and non-executable heap
faults. HEAPDEMO also runs through a pipe, redirected persistent output and
installed application lookup after a fresh emulator boot. Source delivery and
boot/unused sectors must remain unchanged; owned handles and kernel memory
must return to their baselines. Logs use `build/phase-ualloc-*.log` and
`build/handoff-test/ualloc-*.log`.

All of these checks pass on both emulator profiles. All 49 delivered ELF
applications pass the native loader checks, along with 25000 parser mutations
and eight disk corruption/profile rejection checks. The default HEAPCHK and
HEAPDEMO now also pass on Pi, within the hardware scope recorded above.

The full image is still available as the regular 134217728-byte
[IMG](../image_files/OurOS-Pi5-header-experimental-sd.img) and verified
[full-image ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip). Previous
images remain archived flat in `image_files`. Full-image reflashing replaces
`/data`; the two-ELF update above avoids that operation for this phase.

Delivery sizes: HEAPCHK 6416 bytes; HEAPDEMO 3680; application ZIP 8223;
full-image ZIP 885237. The Pi kernel remains 446196 bytes, reserved end
`0x1d0000` below the firmware DTB at `0x200000`; SHELL.ELF remains 8094 bytes.
Full-image SHA-256:
`a9e8199de534f8dcf4d44c67d576883719608e74128152fbc4cbf5c2152e246d`.
Application ZIP SHA-256:
`cae00bcfdced6184c5df065b6d8b3646d578af9b7a587c0184f206d2bda8ae3e`.
