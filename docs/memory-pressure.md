# Memory accounting and allocation pressure

OurOS reports physical memory, the kernel heap, and the calling
application's private heap. Each report measures a different pool.

| Command | Measurements |
| --- | --- |
| Monitor `meminfo` | Current physical page totals, peak allocated pages, and allocation attempts refused because the physical allocator had zero free pages |
| Monitor `heapinfo` | Current and peak kernel heap use, failures at its 128-page limit, and failed requests for a backing page |
| Shell `run /boot/MEMDEMO.ELF pressure` | Free pages and the largest contiguous free run in the reporting app, followed by its failure counters |
| Shell `run /boot/MEMCHK.ELF pressure` | Bounded test of private quota exhaustion, fragmentation, preservation of live bytes, and reuse after freeing pages |

A fresh reporting app normally has 16 free private heap pages, a largest
run of 16 and zero failures. An application can call the same API within
its own process to inspect its allocations. The report calls read
allocator counters without changing them.

## Application API

`uos_memory_pressure(&report)` uses syscall 45. It fills a writable
48-byte `struct uos_memory_pressure` and returns 0, or returns
`UOS_EFAULT` for an invalid output range. The fields are unsigned
64-bit values in this order.

| Field | Meaning |
| --- | --- |
| `free_pages` | Unused slots in the 16-page private heap |
| `largest_run` | Longest consecutive range of unused virtual heap slots |
| `quota_failures` | Heap growth/allocate attempts that exceeded available private page capacity |
| `fragmentation_failures` | Attempts with sufficient total free pages but no suitable consecutive range. In-place growth needs slots immediately after the run. |
| `backing_failures` | Attempts that found a virtual range but could not acquire all the required backing pages |
| `mapping_failures` | Attempts whose page-table update failed before publication |

The three pressure failures continue to return `UOS_ENOMEM`. An invalid
request keeps the existing error result. The diagnostic counters follow
the process across `exec` and start at zero for a new process.

## Verification

Build and verify a matched kernel and application update, then use the
Windows boot updater. On the Pi, run these commands from the shell.

```text
run /boot/MEMDEMO.ELF pressure
run /boot/MEMCHK.ELF pressure
exit
meminfo
heapinfo
```

`MEMCHK.ELF pressure` should report one quota failure, one fragmentation
failure and `child exit=0`. The test allocates up to 16 pages, preserves
data in the pages it keeps, and checks zeroed reuse after releasing them.
Physical exhaustion and page-table failures are injected in the native
fixtures. The Pi test uses only the private arena.
[Boot update workflow](boot-updates-tharun_verified.md)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-memory.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-heap.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-user-heap.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -MemoryPressure
```

The QEMU test runs on Cortex-A53/EL1 and Cortex-A76/EL2. It compares
physical page, kernel heap and file-handle counts before and after the
application tests, and compares the source and scratch disk hashes.

Native allocator and both QEMU pressure checks passed. The boot updater
preview against the Phase 0 image lists `OUROS.IMG`, `MEMCHK.ELF` and
`MEMDEMO.ELF`. Use the updater's generated PC backup to roll back those
files if needed. The boot update ZIP SHA-256 is
`ac361f8435fdc0f2662c7268e17ac6a236097cad38c3ea81daab3d412677a8ce`.
Phase 1 also passed on the Pi. Both programs exited with code 0. The
before/after checks matched at two allocated physical pages, two kernel
heap pages with three live objects, and zero open files. Both data banks
validated, with generation 14 still mounted. [Pi capture](../proofs/pi5-memory-pressure-2026-11-21.txt)
