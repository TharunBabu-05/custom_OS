# Physical memory discovery and pages

**Current OS phase: [exceptions and timer IRQs](interrupts.md).** MMU protection
and USB/FAT now have Pi evidence. The [kernel heap](kernel-heap.md)
has Pi self-test and counter-restoration evidence. Small kernel buffers now
use shared size-class pages. RAMFS's three seed files own three buffers but
only two PMM pages; `heapinfo`/`heaptest` inspect and exercise the new layer.
MMU identity protection is opt-in and needs Pi validation; synchronization and
process address spaces remain future work.

**Current bitmap layout:** the [foreground Wi-Fi revision](wifi-session.md#allocator-bitmap-relocation)
removes the static 1 MiB bitmap from kernel BSS and claims a separately reserved,
aligned RAM arena from the DT map. `meminfo` shows it and `memmap` lists
`reason=physical page bitmap`. The 32 GiB address limit is unchanged. Managed
RAM can be 1 MiB below older screenshots because that cost is now explicit.
The [12 November Pi capture](../proofs/pi5-live-session-internet-ping-2026-11-12.txt)
confirms the new arena at `0x40000000`, managed RAM 7167 MiB, and a passing
page test before successful USB/FAT and live Wi-Fi/Internet pings.
The historical measurements below predate this change.

OurOS has shared firmware-DTB RAM discovery and a single-core 4 KiB physical
page allocator. The user's Pi screenshot confirms memory discovery, page
patterns, allocation/release counters and double-free rejection on hardware;
see [hardware evidence](pi5-hardware-verification.md). The subsequent
[RAM filesystem revision](ramfs.md) also has a physical capture, including the
MB/MiB display. The latest [storage/mount revision](storage.md) awaits its own test.

## Monitor commands

| Command | Behaviour |
| --- | --- |
| `meminfo` | Firmware-described RAM, managed/free/allocated pages and excluded bytes |
| `memmap` | RAM banks and each recorded reservation with its reason |
| `pagealloc` | Allocate one physical page into the first free monitor slot (0..15) |
| `pagefree <slot>` | Release a page owned by that slot; reject invalid or already freed slots |
| `pagetest` | Allocate four pages, write/read distinct full-page patterns, release them and check counts |

`meminfo` now shows reported, managed, free, allocated and unavailable memory in
decimal **MB** (1,000,000 bytes) and binary **MiB** (1,048,576 bytes), to three
truncated decimal places. Exact byte/page counters remain below those summaries.
The captured board describes 8,585,740,288 bytes: **8585.740 MB (8188.000 MiB)**,
with **7516.192 MB (7168.000 MiB)** initially managed. The conservative reservation
window explains the difference; it is not all missing or consumed application RAM.

`pagealloc` returns a physical address and does not zero the page. The monitor
does not allow arbitrary addresses to be freed. `pagetest` touches only pages
it acquired and preserves the monitor's other outstanding allocations.

Suggested hardware test after installing the rebuilt header image:

```text
meminfo
memmap
pagetest
pagealloc
pagealloc
meminfo
pagefree 0
pagefree 0
pagefree 1
meminfo
```

Expect `Memory: ready`, two additional allocated pages after the allocations,
rejection of the repeated free, and the original free-page count after both pages
are released. The heap-backed RAMFS boot files now share two pages, so the
allocated count changes from 2 to 4 and returns to 2 in an otherwise idle boot.
`pagetest` should print `PASS: four distinct pages written/read/freed; counters restored.`
Save the complete serial log, especially the map if allocation is disabled.

## What is discovered and protected

`kernel/memory/fdt_memory.c` accepts DTB v17-compatible headers with bounded
sections, a terminated reservation block, 1- or 2-cell root addresses/sizes,
multiple enabled memory banks and multiple reg tuples. Overlapping RAM reports
are merged before counting. It reads:

- The DTB memory reservation block.
- Enabled fixed `/reserved-memory` child ranges, including no-map/reusable ones.
- Chosen initrd start/end, when both are present and valid.
- Dynamic reserved-memory requests with alloc-ranges: the entire allowed window
  is conservatively excluded. A request without a bounded window disables the
  allocator instead of silently treating the region as available.

`boot_memory.c` additionally protects the kernel image footprint (including BSS,
stack and allocator bitmap), the DTB's full advertised size, and the first 2 MiB
of the lowest RAM bank as an early firmware/boot guard. Reservations may overlap;
their excluded bytes are counted once through subtraction from the RAM banks.

Pi's DT template contains a dynamic CMA request with a low-memory alloc-ranges
window. Excluding its whole window may withhold much more than the requested
CMA size; this is deliberate until dynamic contiguous reservations are planned
properly. A small board can therefore have no allocatable pages. `memmap` makes
that exclusion visible. We do not claim that every byte of installed RAM is free.

The implementation follows the [DTB format](https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html)
and [memory node requirements](https://devicetree-specification.readthedocs.io/en/stable/devicenodes.html).
This is a memory-only reader, independent of the existing QEMU UART/clock reader.
It is not a general bus translation or peripheral discovery library.

## Allocator limits

- Separately reserved 1 MiB bitmap arena, covering physical addresses below **32 GiB**.
- RAM beyond the limit is reported in described RAM but remains unavailable.
- Only whole 4 KiB pages within declared RAM and outside every reservation are managed.
- Up to 16 RAM tuples before merging, 64 reservations and 128 usable extents.
- The page-backed kernel heap is now implemented. MMU, virtual address mapping,
  synchronization and process ownership remain future work.
- The allocator rejects reserved, unaligned, out-of-range and already freed pages.
- Invalid/unsupported DT structure or exceeded capacities disable allocation;
  the UART monitor remains available to report the problem.
- Kexec-style usable-memory restrictions and translated/nested reserved-memory
  layouts are rejected instead of being ignored.

The bitmap occupies its own reserved RAM arena, selected from the DT map;
it is no longer part of kernel BSS and cannot be allocated or freed by the PMM.
The Pi linker still enforces that the full kernel footprint stays below the
DTB window at `0x200000`. Nothing writes to physical storage during these tests.

## Build and verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-memory.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target virt
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-qemu.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-qemu.ps1 -Handoff
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header
```

Host tests use native MSYS2 GCC and Win32 guard pages. They exercise cell widths,
multiple/high RAM banks, overlaps, reservations, dynamic windows, initrd,
arithmetic overflow, unsupported translations, capacity limits, every truncation,
5000 mutations, allocation uniqueness/exhaustion/reuse and invalid frees.
Reference QEMU and pinned Pi DTBs are parsed when available; a Pi template is
not a measurement of the board's actual RAM. QEMU tests write/read/free pages
and verify balanced counters on A53/A76 and EL1/EL2 handoff configurations.

Before replacing the local image, the previous hardware-observed image was
preserved as `build/rpi5-header/OurOS-Pi5-header-boot-verified.img` with its
checksum and manifest. That baseline does not include these new memory features.
